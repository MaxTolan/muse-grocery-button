/*
 * Google Cloud Speech-to-Text and Text-to-Speech over HTTPS, straight from the
 * device (no relay): owner decision, Oct 21 2026 (docs/05). The API key comes
 * from CONFIG_GROCERY_GOOGLE_API_KEY, which tools/fw.sh fills from the
 * git-ignored google_api_key.local; it goes in a request header, never the URL.
 *
 * Both calls block (call them from the voice task) and keep their HTTPS
 * connection open between calls so a session pays for the TLS handshake once.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 16 kHz mono speech -> text. ESP_OK with text "" when nothing was recognised.
 * ESP_ERR_INVALID_STATE: no API key built in. Otherwise `why` says what failed. */
esp_err_t google_stt(const int16_t *pcm, size_t frames, char *text, size_t cap, float *confidence, char *why,
                     size_t why_cap);

/* Text -> 16 kHz mono speech, in a buffer the caller frees with free(). */
esp_err_t google_tts(const char *text, int16_t **pcm, size_t *frames, char *why, size_t why_cap);

/* Closes the connections (end of session). */
void google_speech_close(void);

#ifdef __cplusplus
}
#endif
