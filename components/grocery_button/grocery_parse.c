/*
 * Transcript reading for the grocery button (grocery_parse.h).
 *
 * The transcript is split into words. Each word keeps its original spelling
 * (for the item, which is spoken back and sent to Muse) and a normalised form
 * for matching: lower case, letters, digits and apostrophes only.
 */
#include "grocery_parse.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define MAX_WORDS 48
#define WORD_MAX 40

typedef struct {
    const char *start;   /* in the transcript */
    size_t len;
    char norm[WORD_MAX];
    bool drop;           /* not part of the item */
} word_t;

typedef struct {
    word_t w[MAX_WORDS];
    int n;
} words_t;

static void normalise(const char *s, size_t len, char *out, size_t cap)
{
    size_t o = 0;
    for (size_t i = 0; i < len && o + 1 < cap; i++) {
        unsigned char c = (unsigned char)s[i];
        /* A curly apostrophe (U+2019, E2 80 99) counts as a straight one. */
        if (c == 0xE2 && i + 2 < len && (unsigned char)s[i + 1] == 0x80 && (unsigned char)s[i + 2] == 0x99) {
            out[o++] = '\'';
            i += 2;
        } else if (isalnum(c) || c == '\'') {
            out[o++] = (char)tolower(c);
        } else if (c >= 0x80) {
            out[o++] = (char)c;   /* other UTF-8 letters pass through */
        }
    }
    out[o] = '\0';
}

static void split(const char *text, words_t *ws)
{
    ws->n = 0;
    const char *p = text;
    while (*p && ws->n < MAX_WORDS) {
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        if (!*p) {
            break;
        }
        const char *s = p;
        while (*p && !isspace((unsigned char)*p)) {
            p++;
        }
        word_t *w = &ws->w[ws->n];
        w->start = s;
        w->len = (size_t)(p - s);
        w->drop = false;
        normalise(s, w->len, w->norm, sizeof(w->norm));
        if (w->norm[0]) {
            ws->n++;
        }
    }
}

static bool is_any(const char *s, const char *const *list)
{
    for (; *list; list++) {
        if (!strcmp(s, *list)) {
            return true;
        }
    }
    return false;
}

static const char *const COSTCO[] = { "costco", "costcos", "costco's", NULL };
/* "Kruger" and friends are how speech recognition often hears "Kroger". */
static const char *const KROGER[] = { "kroger", "krogers", "kroger's", "kruger", "krugers", "kruger's",
                                      "krueger", "kroeger", NULL };
static const char *const STORE_PREP[] = { "on", "to", "at", "from", "for", "in", "into", NULL };

/* Finds and drops the store words (and "to the ... list" around them); returns the store. */
static grocery_store_t take_stores(words_t *ws)
{
    bool costco = false, kroger = false;
    for (int i = 0; i < ws->n; i++) {
        word_t *w = &ws->w[i];
        int span = 0;
        if (is_any(w->norm, COSTCO)) {
            costco = true;
            span = 1;
        } else if (is_any(w->norm, KROGER)) {
            kroger = true;
            span = 1;
        } else if (!strcmp(w->norm, "cost") && i + 1 < ws->n && !strcmp(ws->w[i + 1].norm, "co")) {
            costco = true;
            span = 2;
        }
        if (!span) {
            continue;
        }
        for (int k = 0; k < span; k++) {
            ws->w[i + k].drop = true;
        }
        int b = i - 1;
        if (b >= 0 && !strcmp(ws->w[b].norm, "the")) {
            ws->w[b--].drop = true;
        }
        if (b >= 0 && is_any(ws->w[b].norm, STORE_PREP)) {
            ws->w[b].drop = true;
        }
        int a = i + span;
        if (a < ws->n && (!strcmp(ws->w[a].norm, "list") || !strcmp(ws->w[a].norm, "please"))) {
            ws->w[a].drop = true;
        }
        i += span - 1;
    }
    if (costco == kroger) {
        return GROCERY_STORE_NONE;   /* neither, or both: ask */
    }
    return costco ? GROCERY_STORE_COSTCO : GROCERY_STORE_KROGER;
}

/* Phrases (space-separated normalised words) trimmed off the front or back of the item. */
static const char *const LEADING[] = {
    "please", "add", "can you add", "could you add", "can you put", "could you put", "put", "get", "buy",
    "grab", "pick up", "i need", "we need", "need", "i want", "we're out of", "were out of", "we are out of",
    "out of", "and", "also", "um", "uh", "oh", "okay", "ok", "so", "let's add", "lets add", "let's get",
    NULL,
};
static const char *const TRAILING[] = {
    "please", "thanks", "thank you", "to the list", "on the list", "to my list", "on my list",
    "to the grocery list", "on the grocery list", "to the shopping list", "on the shopping list",
    "to the grocery lists", "to my grocery list", NULL,
};

/* The kept words, in order, as indexes into ws. */
static int kept(const words_t *ws, int *idx)
{
    int n = 0;
    for (int i = 0; i < ws->n; i++) {
        if (!ws->w[i].drop) {
            idx[n++] = i;
        }
    }
    return n;
}

/* Number of words in `phrase` if the kept words from position `at` match it, else 0. */
static int match_at(const words_t *ws, const int *idx, int n, int at, const char *phrase)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%s", phrase);
    int count = 0;
    for (char *tok = strtok(buf, " "); tok; tok = strtok(NULL, " ")) {
        if (at + count >= n || strcmp(ws->w[idx[at + count]].norm, tok)) {
            return 0;
        }
        count++;
    }
    return count;
}

static int phrase_words(const char *phrase)
{
    int n = 1;
    for (; *phrase; phrase++) {
        n += *phrase == ' ';
    }
    return n;
}

static void trim_fillers(words_t *ws)
{
    int idx[MAX_WORDS];
    bool changed = true;
    while (changed) {
        changed = false;
        int n = kept(ws, idx);
        for (const char *const *p = LEADING; *p && n; p++) {
            int m = match_at(ws, idx, n, 0, *p);
            if (m && m < n) {   /* never the whole utterance: "get" alone is left to the caller */
                for (int k = 0; k < m; k++) {
                    ws->w[idx[k]].drop = true;
                }
                changed = true;
                break;
            }
        }
        if (changed) {
            continue;
        }
        for (const char *const *p = TRAILING; *p && n; p++) {
            int len = phrase_words(*p);
            if (len < n && match_at(ws, idx, n, n - len, *p)) {
                for (int k = n - len; k < n; k++) {
                    ws->w[idx[k]].drop = true;
                }
                changed = true;
                break;
            }
        }
    }
}

/* Everything said, normalised and joined, for matching whole-utterance phrases. */
static void joined(const words_t *ws, int from, int to, char *out, size_t cap)
{
    size_t o = 0;
    out[0] = '\0';
    for (int i = from; i < to; i++) {
        int n = snprintf(out + o, cap - o, "%s%s", o ? " " : "", ws->w[i].norm);
        if (n < 0 || (size_t)n >= cap - o) {
            break;
        }
        o += (size_t)n;
    }
}

static const char *const END_LEAD[] = { "no", "nope", "nah", "ok", "okay", "um", "uh", "oh", "and", "so", NULL };
static const char *const END_PHRASES[] = {
    "that's it", "thats it", "that is it", "that's all", "thats all", "that is all", "that'll do",
    "that's everything", "thats everything", "that's it for now", "that's all for now", "nothing",
    "nothing else", "nothing more", "no more", "done", "i'm done", "im done", "all done", "we're done",
    "i'm good", "im good", "all set", "i'm all set", "we're good", "that'll be it", "i think that's it",
    "i think that's all", "goodbye", "bye", NULL,
};

static bool ends_session(const words_t *ws)
{
    int from = 0, to = ws->n;
    bool stripped = false;
    while (from < to && is_any(ws->w[from].norm, END_LEAD)) {
        from++;
        stripped = true;
    }
    for (;;) {
        if (to - from >= 1 && (!strcmp(ws->w[to - 1].norm, "thanks") || !strcmp(ws->w[to - 1].norm, "please"))) {
            to--;
            stripped = true;
        } else if (to - from >= 2 && !strcmp(ws->w[to - 2].norm, "thank") && !strcmp(ws->w[to - 1].norm, "you")) {
            to -= 2;
            stripped = true;
        } else {
            break;
        }
    }
    if (from >= to) {
        return stripped;   /* "no", "nope, thanks", "thank you" */
    }
    char s[128];
    joined(ws, from, to, s, sizeof(s));
    return is_any(s, END_PHRASES);
}

/* Cuts `s` to at most cap - 1 bytes without splitting a UTF-8 character. */
static void copy_utf8(char *out, size_t cap, const char *s, size_t len)
{
    if (len > cap - 1) {
        len = cap - 1;
        while (len && ((unsigned char)s[len] & 0xC0) == 0x80) {
            len--;
        }
    }
    memcpy(out, s, len);
    out[len] = '\0';
}

static bool trim_char(char c)
{
    return c && strchr(" .,!?;:\"-", c) != NULL;
}

static void build_item(const words_t *ws, char *out, size_t cap)
{
    char buf[256];
    size_t o = 0;
    for (int i = 0; i < ws->n; i++) {
        const word_t *w = &ws->w[i];
        if (w->drop) {
            continue;
        }
        if (o && o + 1 < sizeof(buf)) {
            buf[o++] = ' ';
        }
        size_t take = w->len < sizeof(buf) - 1 - o ? w->len : sizeof(buf) - 1 - o;
        memcpy(buf + o, w->start, take);
        o += take;
    }
    buf[o] = '\0';
    char *s = buf;
    while (trim_char(*s)) {
        s++;
    }
    copy_utf8(out, cap, s, strlen(s));
    size_t len = strlen(out);
    while (len && trim_char(out[len - 1])) {   /* also after a cut */
        out[--len] = '\0';
    }
}

void grocery_parse(const char *transcript, grocery_heard_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!transcript) {
        return;
    }
    for (const unsigned char *p = (const unsigned char *)transcript; *p; p++) {
        if (isalpha(*p) || *p >= 0x80) {
            out->has_words = true;
            break;
        }
    }
    static words_t ws;   /* ~2 KB: kept off small task stacks */
    split(transcript, &ws);
    out->ends_session = ends_session(&ws);
    out->store = take_stores(&ws);
    trim_fillers(&ws);
    build_item(&ws, out->item, sizeof(out->item));
}

const char *grocery_store_name(grocery_store_t store)
{
    switch (store) {
    case GROCERY_STORE_COSTCO: return "Costco";
    case GROCERY_STORE_KROGER: return "Kroger";
    default: return "";
    }
}
