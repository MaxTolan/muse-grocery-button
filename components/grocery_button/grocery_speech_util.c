/* Offline helpers for the Google speech APIs (grocery_speech_util.h). */
#include "grocery_speech_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t grocery_b64_encode(const uint8_t *in, size_t n, char *out)
{
    char *o = out;
    size_t i = 0;
    for (; i + 2 < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16 | (uint32_t)in[i + 1] << 8 | in[i + 2];
        *o++ = B64[v >> 18];
        *o++ = B64[v >> 12 & 63];
        *o++ = B64[v >> 6 & 63];
        *o++ = B64[v & 63];
    }
    if (i < n) {
        uint32_t v = (uint32_t)in[i] << 16 | (i + 1 < n ? (uint32_t)in[i + 1] << 8 : 0);
        *o++ = B64[v >> 18];
        *o++ = B64[v >> 12 & 63];
        *o++ = i + 1 < n ? B64[v >> 6 & 63] : '=';
        *o++ = '=';
    }
    return (size_t)(o - out);
}

static int b64_value(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;   /* '-' and '_': the URL-safe alphabet */
    if (c == '/' || c == '_') return 63;
    return -1;
}

size_t grocery_b64_decode(const char *in, size_t n, uint8_t *out)
{
    uint32_t acc = 0;
    int bits = 0;
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        char c = in[i];
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            continue;
        }
        if (c == '\\' && i + 1 < n && in[i + 1] == '/') {
            continue;   /* JSON may write '/' as "\/" */
        }
        int v = b64_value(c);
        if (v < 0) {
            break;
        }
        acc = acc << 6 | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[o++] = (uint8_t)(acc >> bits);
        }
    }
    return o;
}

bool grocery_json_escape(const char *s, char *out, size_t cap)
{
    size_t o = 0;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        char esc[8];
        const char *add;
        if (c == '"') add = "\\\"";
        else if (c == '\\') add = "\\\\";
        else if (c == '\n') add = "\\n";
        else if (c == '\r') add = "\\r";
        else if (c == '\t') add = "\\t";
        else if (c < 0x20) {
            snprintf(esc, sizeof(esc), "\\u%04x", c);
            add = esc;
        } else {
            esc[0] = (char)c;
            esc[1] = '\0';
            add = esc;
        }
        size_t len = strlen(add);
        if (o + len + 1 > cap) {
            if (cap) out[o] = '\0';
            return false;
        }
        memcpy(out + o, add, len);
        o += len;
    }
    if (cap) out[o] = '\0';
    return true;
}

/* Points past `"key"` and the colon, at the value; NULL if not found. */
static const char *find_value(const char *json, const char *key)
{
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) {
        return NULL;
    }
    p += strlen(pat);
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
    if (*p != ':') {
        return NULL;
    }
    p++;
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
    return p;
}

const char *grocery_json_string(const char *json, const char *key, size_t *len)
{
    const char *p = find_value(json, key);
    if (!p || *p != '"') {
        return NULL;
    }
    p++;
    const char *e = p;
    while (*e && *e != '"') {
        e += (*e == '\\' && e[1]) ? 2 : 1;
    }
    if (*e != '"') {
        return NULL;
    }
    *len = (size_t)(e - p);
    return p;
}

static void put_utf8(char *out, size_t cap, size_t *o, uint32_t cp)
{
    char b[4];
    int n;
    if (cp < 0x80) { b[0] = (char)cp; n = 1; }
    else if (cp < 0x800) { b[0] = (char)(0xC0 | cp >> 6); b[1] = (char)(0x80 | (cp & 63)); n = 2; }
    else if (cp < 0x10000) { b[0] = (char)(0xE0 | cp >> 12); b[1] = (char)(0x80 | (cp >> 6 & 63)); b[2] = (char)(0x80 | (cp & 63)); n = 3; }
    else { b[0] = (char)(0xF0 | cp >> 18); b[1] = (char)(0x80 | (cp >> 12 & 63)); b[2] = (char)(0x80 | (cp >> 6 & 63)); b[3] = (char)(0x80 | (cp & 63)); n = 4; }
    if (*o + (size_t)n < cap) {
        memcpy(out + *o, b, (size_t)n);
        *o += (size_t)n;
    }
}

/* Appends the unescaped JSON string s[0..len) to out at *o. */
static void unescape_into(const char *s, size_t len, char *out, size_t cap, size_t *o)
{
    for (size_t i = 0; i < len;) {
        char c = s[i++];
        if (c != '\\' || i >= len) {
            if (*o + 1 < cap) out[(*o)++] = c;
            continue;
        }
        char e = s[i++];
        switch (e) {
        case 'n': c = '\n'; break;
        case 't': c = '\t'; break;
        case 'r': c = '\r'; break;
        case 'b': c = '\b'; break;
        case 'f': c = '\f'; break;
        case 'u': {
            if (i + 4 > len) return;
            char hex[5] = { s[i], s[i + 1], s[i + 2], s[i + 3], 0 };
            uint32_t cp = (uint32_t)strtoul(hex, NULL, 16);
            i += 4;
            if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= len && s[i] == '\\' && s[i + 1] == 'u') {
                char lo[5] = { s[i + 2], s[i + 3], s[i + 4], s[i + 5], 0 };
                uint32_t l = (uint32_t)strtoul(lo, NULL, 16);
                if (l >= 0xDC00 && l < 0xE000) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (l - 0xDC00);
                    i += 6;
                }
            }
            put_utf8(out, cap, o, cp);
            continue;
        }
        default: c = e; break;   /* \" \\ \/ */
        }
        if (*o + 1 < cap) out[(*o)++] = c;
    }
}

bool grocery_stt_transcript(const char *json, char *out, size_t cap, float *confidence)
{
    out[0] = '\0';
    *confidence = 0;
    if (!json || !strchr(json, '{') || grocery_json_string(json, "message", &(size_t){ 0 })) {
        return false;   /* not JSON, or an error response */
    }
    size_t o = 0;
    bool first = true;
    const char *p = json;
    for (;;) {
        const char *alts = strstr(p, "\"alternatives\"");
        if (!alts) {
            break;
        }
        size_t len;
        const char *t = grocery_json_string(alts, "transcript", &len);
        if (!t) {
            break;
        }
        /* Only the top alternative: skip to the next result's alternatives. */
        if (first) {
            const char *c = find_value(alts, "confidence");
            const char *next = strstr(alts + 1, "\"alternatives\"");
            if (c && (!next || c < next)) {
                *confidence = strtof(c, NULL);
            }
        }
        if (o && o + 1 < cap && out[o - 1] != ' ' && t[0] != ' ') {
            out[o++] = ' ';
        }
        unescape_into(t, len, out, cap, &o);
        first = false;
        p = t + len;
    }
    out[o] = '\0';
    /* Trim the leading space Google puts on later results. */
    size_t lead = 0;
    while (out[lead] == ' ') lead++;
    if (lead) memmove(out, out + lead, strlen(out + lead) + 1);
    return true;
}

bool grocery_api_error(const char *json, char *out, size_t cap)
{
    if (!json || !strstr(json, "\"error\"")) {
        return false;
    }
    size_t len, o = 0;
    const char *m = grocery_json_string(json, "message", &len);
    if (!m) {
        return false;
    }
    unescape_into(m, len, out, cap, &o);
    out[o] = '\0';
    return true;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

bool grocery_wav_pcm(const uint8_t *wav, size_t n, size_t *off, size_t *len, uint32_t *rate)
{
    if (n < 12 || memcmp(wav, "RIFF", 4) || memcmp(wav + 8, "WAVE", 4)) {
        return false;
    }
    bool fmt_ok = false;
    size_t p = 12;
    while (p + 8 <= n) {
        uint32_t size = le32(wav + p + 4);
        if (!memcmp(wav + p, "fmt ", 4) && p + 8 + 16 <= n) {
            uint16_t format = le16(wav + p + 8), channels = le16(wav + p + 10), bits = le16(wav + p + 22);
            *rate = le32(wav + p + 12);
            fmt_ok = format == 1 && channels == 1 && bits == 16;
        } else if (!memcmp(wav + p, "data", 4)) {
            if (!fmt_ok) {
                return false;
            }
            *off = p + 8;
            /* Streamed WAVs can give 0 or 0xFFFFFFFF: take whatever is there. */
            size_t avail = n - *off;
            *len = (size == 0 || size > avail) ? avail : size;
            *len &= ~(size_t)1;
            return true;
        }
        if (size > n - p - 8) {
            return false;   /* a chunk running past the end (and no data chunk before it) */
        }
        p += 8 + size + (size & 1);
    }
    return false;
}
