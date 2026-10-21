/*
 * Device glue for the grocery button (ESP-IDF only): hooks the voice loop,
 * the round screen and the voice inbox into the Muse SDK through muse_ext.h.
 *
 *   - voice_start: the SDK's push-to-talk loop is replaced by grocery_voice.c
 *   - ui_build: the status screen on the face tile once paired; the SDK's own
 *     screens handle pairing and Wi-Fi setup (Muse app > Settings > Devices)
 *   - the voice inbox: confirmed items wait in flash (grocery_outbox.c) and go
 *     to the owner's Muse as one typed chat message; they leave the outbox
 *     only on Muse's acknowledgement ("@chat ack", SDK patch 0004)
 *   - power: deep sleep between sessions on battery, woken by the talk button
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs.h"
#include "sdkconfig.h"

#include "muse_audio.h"
#include "muse_ble.h"
#include "muse_board.h"
#include "muse_chat.h"
#include "muse_chat_priv.h"
#include "muse_ext.h"
#include "muse_link.h"
#include "muse_state.h"
#include "muse_ui.h"

#include "grocery_device.h"
#include "grocery_outbox.h"

#if CONFIG_GROCERY_BUTTON

static const char *TAG = "grocery";

#define NVS_NS "grocery"
#define NVS_OUTBOX "outbox"
#define FRAME_MS 40
#define LOW_BATTERY_PCT 15
#define CRITICAL_BATTERY_PCT 5
#define ACK_TIMEOUT_US (30 * 1000000LL)   /* sent, but no word from Muse */
#define RETRY_MIN_US (15 * 1000000LL)
#define RETRY_MAX_US (120 * 1000000LL)
#define TALK_GPIO ((gpio_num_t)CONFIG_GROCERY_TALK_GPIO)

/* ---- screen state: written by the voice task, drawn by the LVGL task ---- */

static portMUX_TYPE s_view_mux = portMUX_INITIALIZER_UNLOCKED;
static grocery_ui_state_t s_view = { .screen = GROCERY_SCREEN_READY, .battery_pct = -1 };
static lv_obj_t *s_root;
static bool s_visible;

void grocery_view(grocery_screen_t screen, grocery_store_t store, const char *item, const char *detail)
{
    taskENTER_CRITICAL(&s_view_mux);
    s_view.screen = screen;
    s_view.store = store;
    strlcpy(s_view.item, item ? item : "", sizeof(s_view.item));
    strlcpy(s_view.detail, detail ? detail : "", sizeof(s_view.detail));
    s_view.progress = 0;
    s_view.level = 0;
    taskEXIT_CRITICAL(&s_view_mux);
}

void grocery_view_listening(float progress, float level)
{
    taskENTER_CRITICAL(&s_view_mux);
    s_view.progress = progress;
    /* Fast attack, slow release, so the rim doesn't flicker. */
    s_view.level = level > s_view.level ? level : s_view.level * 0.85f + level * 0.15f;
    taskEXIT_CRITICAL(&s_view_mux);
}

/* ---- power and pairing ---- */

static bool s_woke_ext1;   /* this boot is a wake from deep sleep by the talk button */
static bool s_woke_by_press;

bool grocery_paired(void)
{
    return muse_hatch_configured();
}

bool grocery_battery_low(void)
{
    muse_power_t p = muse_state_power();
    return p.battery_pct >= 0 && p.battery_pct < LOW_BATTERY_PCT && !p.usb && !p.charging;
}

bool grocery_battery_critical(void)
{
    muse_power_t p = muse_state_power();
    return p.battery_pct >= 0 && p.battery_pct <= CRITICAL_BATTERY_PCT && !p.usb && !p.charging;
}

bool grocery_talk_held(void)
{
    return gpio_get_level(TALK_GPIO) == 1;   /* PWR, through an inverter: high while held */
}

bool grocery_woke_by_press(void)
{
    return s_woke_by_press;
}

/* ---- the voice inbox ---- */

typedef enum { SEND_IDLE, SEND_WAITING } send_state_t;

static SemaphoreHandle_t s_lock;   /* the outbox and its send state */
static EXT_RAM_BSS_ATTR grocery_outbox_t s_outbox;
static send_state_t s_send;
static int s_sent_n;               /* items the message in flight covers */
static int64_t s_sent_us, s_next_try_us, s_backoff_us = RETRY_MIN_US;
static bool s_in_send, s_refused;

/* Flash writes stall the caches, so they can't run on the voice task (its
 * stack is in PSRAM): a small task with an internal stack saves the outbox. */
static TaskHandle_t s_saver;
static volatile uint32_t s_save_want, s_save_done;

static void lock(void)
{
    xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGiveRecursive(s_lock);
}

static void request_save(void)
{
    s_save_want++;
    if (s_saver) {
        xTaskNotifyGive(s_saver);
    }
}

static void saver_task(void *arg)
{
    (void)arg;
    static char blob[GROCERY_BLOB_MAX];
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uint32_t want = s_save_want;
        lock();
        grocery_outbox_save(&s_outbox, blob, sizeof(blob));
        unlock();
        nvs_handle_t h;
        esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
        if (err == ESP_OK) {
            err = nvs_set_str(h, NVS_OUTBOX, blob);
            if (err == ESP_OK) {
                err = nvs_commit(h);
            }
            nvs_close(h);
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "couldn't save the outbox: %s", esp_err_to_name(err));
        }
        s_save_done = want;
    }
}

static void restore_outbox(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t len = 0;
    if (nvs_get_str(h, NVS_OUTBOX, NULL, &len) == ESP_OK && len) {
        char *blob = malloc(len);
        if (blob && nvs_get_str(h, NVS_OUTBOX, blob, &len) == ESP_OK) {
            grocery_outbox_load(&s_outbox, blob);
            if (s_outbox.count) {
                ESP_LOGI(TAG, "%d item%s still to send to Muse", s_outbox.count, s_outbox.count == 1 ? "" : "s");
            }
        }
        free(blob);
    }
    nvs_close(h);
}

void grocery_inbox_submit(const char *item, grocery_store_t store)
{
    lock();
    bool ok = grocery_outbox_add(&s_outbox, item, store);
    s_next_try_us = 0;   /* send now, not after a backoff */
    unlock();
    if (!ok) {
        ESP_LOGE(TAG, "outbox full: \"%s\" not queued", item);
        return;
    }
    request_save();
}

bool grocery_inbox_pending(void)
{
    lock();
    bool pending = s_outbox.count > 0;
    unlock();
    return pending;
}

static void back_off(void)
{
    s_send = SEND_IDLE;
    s_next_try_us = esp_timer_get_time() + s_backoff_us;
    s_backoff_us = s_backoff_us * 2 > RETRY_MAX_US ? RETRY_MAX_US : s_backoff_us * 2;
}

void grocery_inbox_poll(void)
{
    char *msg = NULL;
    lock();
    int64_t now = esp_timer_get_time();
    if (s_send == SEND_WAITING && now - s_sent_us > ACK_TIMEOUT_US) {
        ESP_LOGW(TAG, "no word from Muse on the grocery message; will try again");
        back_off();
    }
    if (s_send == SEND_IDLE && s_outbox.count && now >= s_next_try_us && muse_hatch_configured()
        && muse_hatch_ready()) {
        msg = heap_caps_malloc(GROCERY_MESSAGE_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (msg) {
            s_sent_n = grocery_outbox_message(&s_outbox, msg, GROCERY_MESSAGE_MAX);
            s_send = SEND_WAITING;
            s_sent_us = now;
            s_in_send = true;
            s_refused = false;
            ESP_LOGI(TAG, "sending %d item%s to Muse", s_sent_n, s_sent_n == 1 ? "" : "s");
        }
    }
    unlock();
    if (!msg) {
        return;
    }
    /* A refusal ("MUSE NOT SET UP", "BUSY") is reported from inside this call. */
    muse_hatch_text_turn(msg);   /* takes and frees it */
    lock();
    s_in_send = false;
    if (s_refused) {
        back_off();
    }
    unlock();
}

/* Every "@chat" line of a typed turn (ours, or tools/muse/chat.py's). */
static void chat_event(const char *type, const char *text)
{
    lock();
    if (s_send != SEND_WAITING) {
        if (!strcmp(type, "final") && text) {
            ESP_LOGI(TAG, "Muse replied: %s", text);
        }
        unlock();
        return;
    }
    if (!strcmp(type, "ack")) {
        grocery_outbox_delivered(&s_outbox, s_sent_n);
        ESP_LOGI(TAG, "Muse has %d item%s (%d left)", s_sent_n, s_sent_n == 1 ? "" : "s", s_outbox.count);
        s_send = SEND_IDLE;
        s_next_try_us = 0;
        s_backoff_us = RETRY_MIN_US;
        unlock();
        request_save();
        return;
    }
    if (!strcmp(type, "error")) {
        ESP_LOGW(TAG, "grocery message not sent: %s", text ? text : "?");
        if (s_in_send) {
            s_refused = true;
        } else {
            back_off();
        }
    }
    unlock();
}

/* ---- sleep ---- */

static void wait_saved(void)
{
    for (int i = 0; i < 100 && s_save_done != s_save_want; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void grocery_sleep(void)
{
    wait_saved();
#if CONFIG_GROCERY_DEEP_SLEEP
    bool deep = muse_state_on_battery() && grocery_paired();
#else
    bool deep = false;
#endif
    muse_state_set_asleep(true);
    if (!deep) {
        ESP_LOGI(TAG, "screen off (on USB power: no deep sleep)");
        return;
    }
    /* Let the UI take the panel dark, then stop it and send touch to sleep. */
    for (int i = 0; i < 50 && !muse_ui_dark(); i++) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (muse_board->display_pause) {
        muse_board->display_pause(true);
    }
    muse_audio_power(false);
#if CONFIG_GROCERY_AMP_GPIO >= 0
    /* Unheld, the amp's enable would float through deep sleep. */
    gpio_set_level((gpio_num_t)CONFIG_GROCERY_AMP_GPIO, 0);
    gpio_hold_en((gpio_num_t)CONFIG_GROCERY_AMP_GPIO);
    gpio_deep_sleep_hold_en();
#endif
    /* A press still down would wake it at once. */
    for (int i = 0; i < 1000 && grocery_talk_held(); i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGI(TAG, "deep sleep until the talk button");
    esp_sleep_enable_ext1_wakeup_io(1ULL << TALK_GPIO, ESP_EXT1_WAKEUP_ANY_HIGH);
    esp_deep_sleep_start();
}

/* ---- screen ---- */

/* The SDK's own screens take over while the Muse app is pairing. */
static bool want_visible(void)
{
    muse_link_state_t st = muse_link_state();
    return st != MUSE_LINK_PAIRING && st != MUSE_LINK_CONFIRM;
}

static void setup_hint(char *out, size_t cap)
{
    muse_ble_status_t ble;
    muse_ble_status(&ble);
    snprintf(out, cap, "In the Muse app: Settings > Devices > Add Device, then pick %s.",
             ble.name[0] ? ble.name : "this button");
}

static void frame(lv_timer_t *t)
{
    (void)t;
    bool visible = want_visible();
    if (visible != s_visible) {
        s_visible = visible;
        if (visible) {
            lv_obj_remove_flag(s_root, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (!visible || muse_state_asleep()) {
        return;
    }
    grocery_ui_state_t st;
    taskENTER_CRITICAL(&s_view_mux);
    st = s_view;
    taskEXIT_CRITICAL(&s_view_mux);
    muse_power_t p = muse_state_power();
    st.battery_pct = p.battery_pct;
    st.charging = p.charging;
    if (!grocery_paired()) {
        st.screen = GROCERY_SCREEN_SETUP;
        setup_hint(st.detail, sizeof(st.detail));
    }
    grocery_ui_update(&st);
}

static void ui_build(lv_obj_t *face)
{
    s_root = lv_obj_create(face);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    grocery_ui_create(s_root);
    s_visible = true;
    lv_timer_create(frame, FRAME_MS, NULL);
    ESP_LOGI(TAG, "status screen built");
}

static bool ui_covers_face(void)
{
    return s_visible;
}

/* ---- start ---- */

static int voice_start(QueueHandle_t queue)
{
    if (!CONFIG_GROCERY_GOOGLE_API_KEY[0]) {
        ESP_LOGW(TAG, "no Google API key in this build: put it in google_api_key.local and rebuild");
    }
    /* The board has set the pin up by now. Still held: the wake was a press, and it's speech. */
    s_woke_by_press = s_woke_ext1 && grocery_talk_held();
    restore_outbox();
    if (xTaskCreate(saver_task, "grocery_save", 3072, NULL, 4, &s_saver) != pdPASS) {
        ESP_LOGE(TAG, "no saver task: the outbox won't survive sleep");
    }
    return grocery_voice_start(queue);
}

static const muse_ext_t EXT = {
    .ui_build = ui_build,
    .ui_covers_face = ui_covers_face,
    .chat_event = chat_event,
    .voice_start = voice_start,
};

/* Registered before app_main, so it's in place before the UI and voice start. */
__attribute__((constructor)) static void grocery_register(void)
{
    s_lock = xSemaphoreCreateRecursiveMutex();
    if (esp_sleep_get_wakeup_causes() & BIT(ESP_SLEEP_WAKEUP_EXT1)) {
        /* The wake pin is still routed to the RTC; hand it back before the board reads it. */
        rtc_gpio_deinit(TALK_GPIO);
        s_woke_ext1 = true;
    }
#if CONFIG_GROCERY_AMP_GPIO >= 0
    gpio_hold_dis((gpio_num_t)CONFIG_GROCERY_AMP_GPIO);
    gpio_deep_sleep_hold_dis();
#endif
    muse_ext_register(&EXT);
}

#endif /* CONFIG_GROCERY_BUTTON */
