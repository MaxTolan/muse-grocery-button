/*
 * The grocery button's push-to-talk loop. It replaces the SDK's own voice loop
 * (muse_voice.c) through muse_ext's voice_start hook:
 *
 *   hold -> record (16 kHz mono, a light noise gate, capped at RECORD_MAX_S)
 *        -> Google Speech-to-Text -> the script (grocery_script.c)
 *        -> Google Text-to-Speech through the speaker
 *        -> confirmed items to the voice inbox (grocery_inbox_submit)
 *
 * A session starts with the press that wakes the button and ends with "Got
 * it", a tap, or SESSION_IDLE_MS of silence; then the button sleeps
 * (grocery_sleep). Presses come from the SDK's input task as
 * muse_input_event_t on the queue; the press that woke the chip from deep
 * sleep isn't posted (the input task starts after it), so that one is read
 * from the pin.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "muse_audio.h"
#include "muse_input.h"
#include "muse_mem.h"
#include "muse_settings.h"
#include "muse_state.h"
#include "muse_wifi.h"

#include "google_speech.h"
#include "grocery_device.h"
#include "grocery_script.h"

static const char *TAG = "grocery";

/*
 * The talk button is PWR, and the AXP2101 cuts the power after a 10 s hold
 * (its longest setting), so recordings stop well short of that. docs/02 asks
 * for ~15 s; grocery items take a few seconds.
 */
#define RECORD_MAX_S 8
#define RECORD_MAX_FRAMES (MUSE_AUDIO_RATE * RECORD_MAX_S)
#define CHUNK MUSE_AUDIO_CHUNK                       /* 20 ms */
#define MAX_CHUNKS (RECORD_MAX_FRAMES / CHUNK)
#define TAP_MS 300                                   /* shorter presses are taps */
#define TAIL_FRAMES (MUSE_AUDIO_RATE * 12 / 100)     /* capture runs behind; people let go on the last syllable */
#define PRE_CHUNKS 16                                /* 320 ms kept from before the press */
#define SETTLE_CHUNKS 10                             /* after the speaker, 200 ms of capture is its own echo */
#define SESSION_IDLE_MS 30000                        /* docs/03: 30 s of silence ends it quietly */
#define WIFI_WAIT_MS 8000                            /* after a wake, for Wi-Fi to come up */
#define INBOX_WAIT_MS 20000                          /* before sleeping, for Muse to take waiting items */
#define END_HOLD_MS 1500                             /* the last screen stays up this long */
#define LOW_CONFIDENCE 0.3f                          /* below this a transcript counts as not caught */
/* Noise gate: a chunk is speech when it's this far above the recording's quietest, and loud enough. */
#define GATE_ABOVE_FLOOR_DB 12.0f
#define GATE_MIN_DB -50.0f
#define GATE_MIN_CHUNKS 8                            /* 160 ms of speech */

#define SAY_LOW_BATTERY "Battery's getting low \xE2\x80\x94 time for a charge."
#define SAY_CRITICAL "Battery's too low \xE2\x80\x94 time for a charge."

static QueueHandle_t s_queue;
static int16_t *s_rec;            /* RECORD_MAX_FRAMES */
static float *s_chunk_db;         /* MAX_CHUNKS + PRE_CHUNKS */
static int16_t *s_pre;            /* PRE_CHUNKS * CHUNK, a ring */
static size_t s_pre_next, s_pre_fill;
static int s_settle;
static int16_t s_chunk[CHUNK];

static grocery_script_t s_script;
static bool s_in_session;
static bool s_warned_battery;
static int64_t s_last_us;         /* last press or speech: the idle clock */
static int64_t s_answer_us;       /* a question waits until then (0: none) */

static int64_t now_us(void)
{
    return esp_timer_get_time();
}

/* ---- mic ---- */

static void pre_reset(void)
{
    s_pre_fill = 0;
    s_settle = SETTLE_CHUNKS;
}

/* One 20 ms read while waiting: feeds the pre-roll ring (and paces the loop). */
static void idle_capture(void)
{
    if (muse_audio_read(s_chunk, CHUNK) != ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(20));
        return;
    }
    if (s_settle > 0) {
        s_settle--;
        return;
    }
    memcpy(s_pre + s_pre_next * CHUNK, s_chunk, sizeof(s_chunk));
    s_pre_next = (s_pre_next + 1) % PRE_CHUNKS;
    s_pre_fill = s_pre_fill < PRE_CHUNKS ? s_pre_fill + 1 : PRE_CHUNKS;
}

/* Non-blocking: true if an event of `type` is waiting (others are dropped). */
static bool got_event(muse_ptt_t type, bool *wake)
{
    muse_input_event_t ev;
    bool hit = false;
    while (xQueueReceive(s_queue, &ev, 0) == pdTRUE) {
        muse_state_poke();
        if (ev.type == type) {
            hit = true;
            if (wake) {
                *wake = ev.wake;
            }
        }
    }
    return hit;
}

typedef struct {
    size_t frames;     /* in s_rec */
    int held_ms;       /* from the press (or recording start) to the release */
    bool too_long;
    bool speech;       /* the noise gate opened */
} recording_t;

/*
 * Records until release or the cap. `from_pin`: the press that woke the chip,
 * whose release only the pin shows.
 */
static recording_t record(bool from_pin)
{
    recording_t r = { 0 };
    size_t n = 0, chunks = 0;
    if (!from_pin) {
        for (size_t i = 0; i < s_pre_fill; i++) {
            const int16_t *c = s_pre + ((s_pre_next + PRE_CHUNKS - s_pre_fill + i) % PRE_CHUNKS) * CHUNK;
            memcpy(s_rec + n, c, sizeof(s_chunk));
            s_chunk_db[chunks++] = muse_audio_dbfs(c, CHUNK);
            n += CHUNK;
        }
    }
    size_t pre = n;
    size_t stop_at = RECORD_MAX_FRAMES;
    bool released = false;
    int low_reads = 0;
    grocery_view(GROCERY_SCREEN_LISTENING, GROCERY_STORE_NONE, NULL, NULL);
    while (n + CHUNK <= stop_at) {
        if (muse_audio_read(s_rec + n, CHUNK) != ESP_OK) {
            break;
        }
        float db = muse_audio_dbfs(s_rec + n, CHUNK);
        s_chunk_db[chunks++] = db;
        n += CHUNK;
        grocery_view_listening((float)(n - pre) / (RECORD_MAX_FRAMES - pre),
                               muse_audio_level(s_rec + n - CHUNK, CHUNK));
        if (!released) {
            bool up;
            if (from_pin) {
                low_reads = grocery_talk_held() ? 0 : low_reads + 1;
                up = low_reads >= 2;   /* 40 ms low: debounced */
                got_event(MUSE_PTT_UP, NULL);   /* the input task may still report it */
            } else {
                up = got_event(MUSE_PTT_UP, NULL);
            }
            if (up) {
                released = true;
                r.held_ms = (int)((n - pre) * 1000 / MUSE_AUDIO_RATE);
                stop_at = n + TAIL_FRAMES < RECORD_MAX_FRAMES ? n + TAIL_FRAMES : RECORD_MAX_FRAMES;
            }
        }
    }
    if (!released) {
        r.too_long = true;
        r.held_ms = (int)((n - pre) * 1000 / MUSE_AUDIO_RATE);
    }
    r.frames = n;

    /* Light noise gate: enough chunks clearly above the quietest one. */
    float floor_db = 0;
    for (size_t i = 0; i < chunks; i++) {
        floor_db = (i == 0 || s_chunk_db[i] < floor_db) ? s_chunk_db[i] : floor_db;
    }
    float gate = floor_db + GATE_ABOVE_FLOOR_DB > GATE_MIN_DB ? floor_db + GATE_ABOVE_FLOOR_DB : GATE_MIN_DB;
    int loud = 0;
    float peak = -100;
    for (size_t i = 0; i < chunks; i++) {
        loud += s_chunk_db[i] > gate;
        peak = s_chunk_db[i] > peak ? s_chunk_db[i] : peak;
    }
    r.speech = loud >= GATE_MIN_CHUNKS;
    ESP_LOGI(TAG, "recorded %.2fs%s (held %d ms): floor %.0f dBFS, peak %.0f dBFS, %d loud chunks -> %s",
             (double)n / MUSE_AUDIO_RATE, r.too_long ? " (cap)" : "", r.held_ms, floor_db, peak, loud,
             r.speech ? "speech" : "silence");
    return r;
}

/* ---- speaker ---- */

/*
 * Says `text` through Google TTS. A press while it plays cuts it short and
 * is left for the loop (returns true). Without speech (offline), the screen
 * already says it; give it a moment.
 */
static bool speak(const char *text)
{
    if (!text || !text[0]) {
        return false;
    }
    ESP_LOGI(TAG, "say: %s", text);
    int16_t *pcm = NULL;
    size_t frames = 0;
    char why[96];
    if (google_tts(text, &pcm, &frames, why, sizeof(why)) != ESP_OK) {
        for (int i = 0; i < 100; i++) {   /* 2 s, still taking presses */
            idle_capture();
            if (got_event(MUSE_PTT_DOWN, NULL)) {
                return true;
            }
        }
        return false;
    }
    static const int16_t silence[CHUNK];
    bool interrupted = false;
    for (size_t i = 0; i < frames; i += CHUNK) {
        size_t n = frames - i < CHUNK ? frames - i : CHUNK;
        muse_audio_write(pcm + i, n);
        if (got_event(MUSE_PTT_DOWN, NULL)) {
            ESP_LOGI(TAG, "cut short by a press");
            interrupted = true;
            break;
        }
    }
    /* Keep the speaker fed briefly so it doesn't replay stale DMA. */
    for (int i = 0; i < 5; i++) {
        muse_audio_write(silence, CHUNK);
    }
    free(pcm);
    pre_reset();
    return interrupted;
}

/* ---- session ---- */

static void session_begin(void)
{
    if (s_in_session) {
        return;
    }
    s_in_session = true;
    s_warned_battery = false;
    s_answer_us = 0;
    grocery_script_start(&s_script);
    muse_wifi_power(MUSE_WIFI_FULL);
    if (!muse_wifi_connected()) {
        muse_wifi_apply();   /* now, not after the backoff */
    }
    ESP_LOGI(TAG, "session started");
}

/* Waits for Wi-Fi (just woken, it's still joining); false if it doesn't come. */
static bool wait_wifi(void)
{
    if (muse_wifi_connected()) {
        return true;
    }
    grocery_view(GROCERY_SCREEN_CONNECTING, GROCERY_STORE_NONE, NULL, NULL);
    int64_t give_up = now_us() + WIFI_WAIT_MS * 1000LL;
    while (now_us() < give_up) {
        vTaskDelay(pdMS_TO_TICKS(50));
        if (muse_wifi_connected()) {
            vTaskDelay(pdMS_TO_TICKS(300));   /* DHCP and DNS settle */
            return true;
        }
    }
    return false;
}

/*
 * Ends the session: lets Muse take any waiting items, then sleeps. A press
 * while it waits starts the next turn instead (returns true).
 */
static bool session_end(void)
{
    ESP_LOGI(TAG, "session over");
    int64_t until = now_us() + END_HOLD_MS * 1000LL;
    int64_t inbox_until = now_us() + INBOX_WAIT_MS * 1000LL;
    while (now_us() < until || (grocery_inbox_pending() && grocery_paired() && now_us() < inbox_until)) {
        grocery_inbox_poll();
        idle_capture();
        if (got_event(MUSE_PTT_DOWN, NULL)) {
            grocery_script_start(&s_script);   /* pressed again before sleeping: a fresh start */
            s_warned_battery = false;
            return true;
        }
    }
    if (grocery_inbox_pending()) {
        ESP_LOGW(TAG, "Muse hasn't taken the waiting items yet; they go next time");
    }
    google_speech_close();
    s_in_session = false;
    s_answer_us = 0;
    grocery_sleep();   /* deep sleep doesn't return */
    grocery_view(GROCERY_SCREEN_READY, GROCERY_STORE_NONE, NULL, NULL);
    pre_reset();
    s_last_us = now_us();
    return false;
}

static grocery_screen_t screen_for(grocery_view_t v)
{
    switch (v) {
    case GROCERY_VIEW_ADDED: return GROCERY_SCREEN_ADDED;
    case GROCERY_VIEW_ASK_STORE: return GROCERY_SCREEN_ASK_STORE;
    case GROCERY_VIEW_RETRY: return GROCERY_SCREEN_RETRY;
    case GROCERY_VIEW_OFFLINE: return GROCERY_SCREEN_OFFLINE;
    case GROCERY_VIEW_DONE: return GROCERY_SCREEN_DONE;
    default: return GROCERY_SCREEN_READY;
    }
}

/* Carries out one step of the script. Returns true if a press is already waiting. */
static bool perform(const grocery_action_t *a, const char *detail)
{
    if (a->submit) {
        ESP_LOGI(TAG, "submit_item(\"%s\", %s)", a->item, grocery_store_name(a->store));
        grocery_inbox_submit(a->item, a->store);
    }
    grocery_view(screen_for(a->view), a->view_store, a->view_item, detail);
    s_answer_us = a->answer_ms ? now_us() + a->answer_ms * 1000LL : 0;
    char say[GROCERY_SAY_MAX + 64];
    snprintf(say, sizeof(say), "%s", a->say);
    if (a->end && say[0] && grocery_battery_low() && !s_warned_battery) {
        s_warned_battery = true;
        strlcat(say, " " SAY_LOW_BATTERY, sizeof(say));
    }
    bool pressed = speak(say);
    s_last_us = now_us();
    if (a->answer_ms) {
        s_answer_us = s_last_us + a->answer_ms * 1000LL;   /* from when the question finished */
    }
    if (!pressed && a->end) {
        pressed = session_end();
    }
    return pressed;
}

static bool step(grocery_event_t ev, const char *text, const char *detail)
{
    grocery_action_t a;
    grocery_script_step(&s_script, ev, text, &a);
    return perform(&a, detail);
}

/*
 * One press, from hold to reply. `waking`: the press that woke the button or
 * its screen, where a tap only wakes. Returns true if the next press is
 * already waiting.
 */
static bool handle_press(bool from_pin, bool waking)
{
    session_begin();
    muse_state_poke();
    s_answer_us = 0;   /* a press answers (or replaces) any question */
    recording_t r = record(from_pin);
    s_last_us = now_us();
    if (r.held_ms < TAP_MS && !r.too_long) {
        if (waking) {
            /* The tap that woke the button: just awake, nothing to end yet. */
            grocery_view(GROCERY_SCREEN_READY, GROCERY_STORE_NONE, NULL, NULL);
            pre_reset();
            return false;
        }
        return step(GROCERY_EV_TAP, NULL, NULL);
    }
    if (r.too_long) {
        return step(GROCERY_EV_TOO_LONG, NULL, NULL);
    }
    if (!r.speech) {
        return step(GROCERY_EV_SILENT, NULL, NULL);
    }
    if (!wait_wifi()) {
        return step(GROCERY_EV_OFFLINE, NULL, "No Wi-Fi");
    }
    grocery_view(GROCERY_SCREEN_THINKING, GROCERY_STORE_NONE, NULL, NULL);
    char text[256], why[96];
    float confidence;
    esp_err_t err = google_stt(s_rec, r.frames, text, sizeof(text), &confidence, why, sizeof(why));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "transcription failed: %s", why);
        /* A missing or rejected key shows as such; anything else is the connection. */
        bool key = err == ESP_ERR_INVALID_STATE || strstr(why, "API key") || strstr(why, "403");
        return step(GROCERY_EV_OFFLINE, NULL, key ? "Check the Google API key" : NULL);
    }
    if (text[0] && confidence > 0 && confidence < LOW_CONFIDENCE) {
        ESP_LOGI(TAG, "heard \"%s\" at %.2f confidence: not sure enough", text, (double)confidence);
        text[0] = '\0';
    }
    return step(GROCERY_EV_HEARD, text, NULL);
}

static void voice_task(void *arg)
{
    (void)arg;
    bool pending = grocery_woke_by_press();
    bool from_pin = pending, waking = pending;
    if (pending) {
        ESP_LOGI(TAG, "woken by the talk button: listening");
    }
    grocery_view(GROCERY_SCREEN_READY, GROCERY_STORE_NONE, NULL, NULL);
    s_last_us = now_us();
    for (;;) {
        grocery_inbox_poll();
        if (!pending) {
            idle_capture();   /* paces the loop: 20 ms */
            bool wake = false;
            if (got_event(MUSE_PTT_DOWN, &wake)) {
                pending = true;
                from_pin = false;
                waking = wake;
            } else {
                int64_t t = now_us();
                if (s_answer_us && t >= s_answer_us) {
                    s_answer_us = 0;
                    ESP_LOGI(TAG, "no answer");
                    pending = step(GROCERY_EV_NO_ANSWER, NULL, NULL);
                    from_pin = waking = false;
                } else if (!s_answer_us && t - s_last_us > SESSION_IDLE_MS * 1000LL && grocery_paired()
                           && (s_in_session || !muse_state_asleep())) {
                    /* 30 s of silence: sleep without a goodbye (docs/03). */
                    pending = session_end();
                    from_pin = waking = false;
                }
                if (s_in_session) {
                    muse_state_poke();   /* the session decides when the screen goes dark */
                }
                continue;
            }
        }
        pending = false;
        if (!grocery_paired()) {
            /* Until it's set up there's nobody to send items to; the screen says how. */
            s_last_us = now_us();
            continue;
        }
        if (grocery_battery_critical()) {
            session_begin();
            grocery_view(GROCERY_SCREEN_LOW_BATTERY, GROCERY_STORE_NONE, NULL, NULL);
            speak(SAY_CRITICAL);
            session_end();
            continue;
        }
        pending = handle_press(from_pin, waking);
        from_pin = waking = false;
    }
}

int grocery_voice_start(QueueHandle_t queue)
{
    s_queue = queue;
    s_rec = heap_caps_malloc(RECORD_MAX_FRAMES * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_chunk_db = heap_caps_malloc((MAX_CHUNKS + PRE_CHUNKS + 2) * sizeof(float), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_pre = heap_caps_malloc(PRE_CHUNKS * CHUNK * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_rec || !s_chunk_db || !s_pre) {
        ESP_LOGE(TAG, "no memory for recording");
        return ESP_ERR_NO_MEM;
    }
    if (muse_audio_init(muse_settings_volume(), muse_settings_mic_gain()) != ESP_OK) {
        ESP_LOGE(TAG, "audio init failed");
        return ESP_FAIL;
    }
    /* TLS needs a deeper stack than the SDK's voice task; it lives in PSRAM. */
    if (xTaskCreatePinnedToCoreWithCaps(voice_task, "grocery_voice", 12288, NULL, 6, NULL, MUSE_AUDIO_CORE,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
