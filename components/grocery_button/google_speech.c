/*
 * Google Cloud speech over HTTPS (google_speech.h).
 *
 *   POST https://speech.googleapis.com/v1/speech:recognize
 *     LINEAR16 at 16 kHz, model latest_short (made for short commands), with
 *     the store names as phrase hints.
 *   POST https://texttospeech.googleapis.com/v1/text:synthesize
 *     LINEAR16 at 16 kHz, the speaker's own rate, so the reply plays as is.
 *
 * Bodies and responses live in PSRAM: a full 8 s clip is ~340 KB of base64.
 */
#include "google_speech.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "grocery_speech_util.h"

static const char *TAG = "speech";

#define STT_URL "https://speech.googleapis.com/v1/speech:recognize"
#define TTS_URL "https://texttospeech.googleapis.com/v1/text:synthesize"
#define RATE 16000
#define RESPONSE_MAX (1024 * 1024)
#define TIMEOUT_MS 10000

typedef struct {
    const char *url;
    esp_http_client_handle_t client;
} conn_t;

static conn_t s_stt = { .url = STT_URL };
static conn_t s_tts = { .url = TTS_URL };

static void *big_alloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n);
}

static void conn_close(conn_t *c)
{
    if (c->client) {
        esp_http_client_cleanup(c->client);
        c->client = NULL;
    }
}

void google_speech_close(void)
{
    conn_close(&s_stt);
    conn_close(&s_tts);
}

static bool conn_ready(conn_t *c)
{
    if (c->client) {
        return true;
    }
    esp_http_client_config_t cfg = {
        .url = c->url,
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = TIMEOUT_MS,
        .keep_alive_enable = true,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
    };
    c->client = esp_http_client_init(&cfg);
    if (!c->client) {
        return false;
    }
    esp_http_client_set_header(c->client, "Content-Type", "application/json; charset=utf-8");
    esp_http_client_set_header(c->client, "X-Goog-Api-Key", CONFIG_GROCERY_GOOGLE_API_KEY);
    return true;
}

/* One attempt: writes the body, reads the response into *resp (terminated). Returns the HTTP status, or -1. */
static int attempt(conn_t *c, const char *body, size_t len, char **resp, size_t *resp_len)
{
    if (!conn_ready(c)) {
        return -1;
    }
    if (esp_http_client_open(c->client, (int)len) != ESP_OK) {
        return -1;
    }
    for (size_t off = 0; off < len;) {
        int w = esp_http_client_write(c->client, body + off, (int)(len - off));
        if (w <= 0) {
            return -1;
        }
        off += (size_t)w;
    }
    int64_t content = esp_http_client_fetch_headers(c->client);
    int status = esp_http_client_get_status_code(c->client);
    if (status <= 0) {
        return -1;
    }
    size_t cap = content > 0 && content < RESPONSE_MAX ? (size_t)content + 1 : 16 * 1024;
    char *buf = big_alloc(cap);
    if (!buf) {
        return -1;
    }
    size_t n = 0;
    for (;;) {
        if (n + 1 >= cap) {
            if (cap >= RESPONSE_MAX) {
                break;
            }
            size_t ncap = cap * 2 > RESPONSE_MAX ? RESPONSE_MAX : cap * 2;
            char *nb = heap_caps_realloc(buf, ncap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!nb) {
                break;
            }
            buf = nb;
            cap = ncap;
        }
        int r = esp_http_client_read(c->client, buf + n, (int)(cap - 1 - n));
        if (r < 0) {
            free(buf);
            return -1;
        }
        if (r == 0) {
            break;
        }
        n += (size_t)r;
    }
    buf[n] = '\0';
    if (!esp_http_client_is_complete_data_received(c->client)) {
        /* The connection can't carry another request cleanly. */
        conn_close(c);
    }
    *resp = buf;
    *resp_len = n;
    return status;
}

/* POSTs body; on a broken kept-alive connection, reconnects once. */
static int request(conn_t *c, const char *body, size_t len, char **resp, size_t *resp_len)
{
    bool reused = c->client != NULL;
    int status = attempt(c, body, len, resp, resp_len);
    if (status < 0) {
        conn_close(c);
        if (reused) {
            status = attempt(c, body, len, resp, resp_len);
            if (status < 0) {
                conn_close(c);
            }
        }
    }
    return status;
}

static bool have_key(char *why, size_t why_cap)
{
    if (CONFIG_GROCERY_GOOGLE_API_KEY[0]) {
        return true;
    }
    snprintf(why, why_cap, "no Google API key in this build");
    return false;
}

/* The reason a non-200 response gives, for the log and the screen. */
static void failed(int status, const char *resp, char *why, size_t why_cap)
{
    char msg[96];
    if (resp && grocery_api_error(resp, msg, sizeof(msg))) {
        snprintf(why, why_cap, "%d: %s", status, msg);
    } else {
        snprintf(why, why_cap, status < 0 ? "no connection" : "HTTP %d", status);
    }
}

esp_err_t google_stt(const int16_t *pcm, size_t frames, char *text, size_t cap, float *confidence, char *why,
                     size_t why_cap)
{
    text[0] = '\0';
    *confidence = 0;
    why[0] = '\0';
    if (!have_key(why, why_cap)) {
        return ESP_ERR_INVALID_STATE;
    }
    static const char HEAD[] =
        "{\"config\":{\"encoding\":\"LINEAR16\",\"sampleRateHertz\":16000,\"languageCode\":\"" CONFIG_GROCERY_LANGUAGE
        "\",\"model\":\"" CONFIG_GROCERY_STT_MODEL "\",\"enableAutomaticPunctuation\":true,\"maxAlternatives\":1,"
        "\"speechContexts\":[{\"phrases\":[\"Costco\",\"Kroger\",\"that's it\",\"that's all\"]}]},"
        "\"audio\":{\"content\":\"";
    static const char TAIL[] = "\"}}";
    size_t bytes = frames * sizeof(int16_t);
    size_t len = sizeof(HEAD) - 1 + GROCERY_B64_LEN(bytes) + sizeof(TAIL) - 1;
    char *body = big_alloc(len + 1);
    if (!body) {
        snprintf(why, why_cap, "out of memory");
        return ESP_ERR_NO_MEM;
    }
    size_t o = 0;
    memcpy(body, HEAD, sizeof(HEAD) - 1);
    o += sizeof(HEAD) - 1;
    o += grocery_b64_encode((const uint8_t *)pcm, bytes, body + o);
    memcpy(body + o, TAIL, sizeof(TAIL) - 1);
    o += sizeof(TAIL) - 1;

    int64_t t0 = esp_timer_get_time();
    char *resp = NULL;
    size_t resp_len = 0;
    int status = request(&s_stt, body, o, &resp, &resp_len);
    free(body);
    esp_err_t ret = ESP_OK;
    if (status != 200) {
        failed(status, resp, why, why_cap);
        ret = status < 0 ? ESP_FAIL : ESP_ERR_INVALID_RESPONSE;
    } else if (!grocery_stt_transcript(resp, text, cap, confidence)) {
        snprintf(why, why_cap, "unreadable transcript");
        ret = ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG, "transcribed %.1fs of speech in %.2fs: %s%s%s", (double)frames / RATE,
             (esp_timer_get_time() - t0) / 1e6, ret == ESP_OK ? "\"" : "failed, ", ret == ESP_OK ? text : why,
             ret == ESP_OK ? "\"" : "");
    free(resp);
    return ret;
}

esp_err_t google_tts(const char *text, int16_t **pcm, size_t *frames, char *why, size_t why_cap)
{
    *pcm = NULL;
    *frames = 0;
    why[0] = '\0';
    if (!have_key(why, why_cap)) {
        return ESP_ERR_INVALID_STATE;
    }
    char esc[2 * 256];
    if (!grocery_json_escape(text, esc, sizeof(esc))) {
        snprintf(why, why_cap, "text too long");
        return ESP_ERR_INVALID_SIZE;
    }
    char body[768];
    int len = snprintf(body, sizeof(body),
                       "{\"input\":{\"text\":\"%s\"},\"voice\":{\"languageCode\":\"" CONFIG_GROCERY_LANGUAGE
                       "\",\"name\":\"" CONFIG_GROCERY_TTS_VOICE "\"},\"audioConfig\":{\"audioEncoding\":\"LINEAR16\","
                       "\"sampleRateHertz\":16000}}",
                       esc);
    if (len < 0 || (size_t)len >= sizeof(body)) {
        snprintf(why, why_cap, "text too long");
        return ESP_ERR_INVALID_SIZE;
    }

    int64_t t0 = esp_timer_get_time();
    char *resp = NULL;
    size_t resp_len = 0;
    int status = request(&s_tts, body, (size_t)len, &resp, &resp_len);
    if (status != 200) {
        failed(status, resp, why, why_cap);
        free(resp);
        ESP_LOGW(TAG, "speech for \"%s\" failed: %s", text, why);
        return status < 0 ? ESP_FAIL : ESP_ERR_INVALID_RESPONSE;
    }
    size_t b64_len;
    const char *b64 = grocery_json_string(resp, "audioContent", &b64_len);
    size_t off, pcm_len;
    uint32_t rate = 0;
    if (b64) {
        /* Decode in place: the audio ends up at the start of the response buffer. */
        size_t wav_len = grocery_b64_decode(b64, b64_len, (uint8_t *)resp);
        if (grocery_wav_pcm((const uint8_t *)resp, wav_len, &off, &pcm_len, &rate) && rate == RATE) {
            memmove(resp, resp + off, pcm_len);
            *pcm = (int16_t *)resp;
            *frames = pcm_len / sizeof(int16_t);
            ESP_LOGI(TAG, "%.1fs of speech in %.2fs for \"%s\"", (double)*frames / RATE,
                     (esp_timer_get_time() - t0) / 1e6, text);
            return ESP_OK;
        }
    }
    if (rate && rate != RATE) {
        snprintf(why, why_cap, "speech at %u Hz", (unsigned)rate);
    } else {
        snprintf(why, why_cap, "no audio in the reply");
    }
    ESP_LOGW(TAG, "speech for \"%s\": %s", text, why);
    free(resp);
    return ESP_ERR_INVALID_RESPONSE;
}
