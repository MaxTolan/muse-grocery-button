/*
 * Pieces of the Google Speech-to-Text / Text-to-Speech exchange that don't
 * need a network: base64, JSON string escaping, pulling the transcript out of
 * a recognize response, and finding the samples in a WAV. Plain C, so the
 * host tests build it.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bytes needed for the base64 of n bytes (no terminator). */
#define GROCERY_B64_LEN(n) ((((n) + 2) / 3) * 4)

/* Writes the base64 of in[0..n) to out (GROCERY_B64_LEN(n) bytes, no terminator); returns that length. */
size_t grocery_b64_encode(const uint8_t *in, size_t n, char *out);
/* Decodes base64 (whitespace skipped, stops at '=' or any other character); out may equal in.
 * Returns the bytes written. */
size_t grocery_b64_decode(const char *in, size_t n, uint8_t *out);

/* JSON-escapes s into out (always terminated); false if it didn't fit. */
bool grocery_json_escape(const char *s, char *out, size_t cap);

/* The value of the first "key": "..." string in json: a pointer to its first
 * character, still escaped, and its length; NULL if absent. */
const char *grocery_json_string(const char *json, const char *key, size_t *len);

/*
 * A speech:recognize response: joins every result's top transcript (results
 * come in order), unescaped, into out; *confidence is the first result's
 * (0 when the response doesn't say). Returns false only for a response that
 * isn't a recognize result; "no speech" is true with out "".
 */
bool grocery_stt_transcript(const char *json, char *out, size_t cap, float *confidence);

/* A Google API error response's message ("API key not valid..."), or false. */
bool grocery_api_error(const char *json, char *out, size_t cap);

/* The 16-bit mono PCM in a WAV: offset and byte length of its data chunk, and
 * its sample rate. False if it isn't 16-bit mono PCM. */
bool grocery_wav_pcm(const uint8_t *wav, size_t n, size_t *off, size_t *len, uint32_t *rate);

#ifdef __cplusplus
}
#endif
