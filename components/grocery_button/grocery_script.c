/*
 * The conversation script (grocery_script.h). Each prompt is the wording from
 * docs/03-conversation-design.md.
 */
#include "grocery_script.h"

#include <stdio.h>
#include <string.h>

#define SAY_ADDED "Added %s to %s. Anything else?"
#define SAY_ASK "Costco or Kroger?"
#define SAY_ASK_AGAIN "Sorry \xE2\x80\x94 Costco or Kroger?"
#define SAY_DEFAULT "I'll put %s on the Kroger list."
#define SAY_MISS "I didn't catch that \xE2\x80\x94 try again?"
#define SAY_MISS_AGAIN "Let's try once more, nice and slow."
#define SAY_TOO_LONG "That was a bit long \xE2\x80\x94 try again?"
#define SAY_DONE "Got it."
#define SAY_OFFLINE "I can't reach the internet right now. Try again in a bit."

/* Copies src into dst[cap], always terminated. */
static void copy(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n > cap - 1) {
        n = cap - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void grocery_script_start(grocery_script_t *s)
{
    memset(s, 0, sizeof(*s));
}

static void say(grocery_action_t *a, const char *fmt, const char *x, const char *y)
{
    snprintf(a->say, sizeof(a->say), fmt, x ? x : "", y ? y : "");
}

static void confirm(grocery_script_t *s, grocery_action_t *a, const char *item, grocery_store_t store)
{
    a->submit = true;
    copy(a->item, sizeof(a->item), item);
    a->store = store;
    a->view = GROCERY_VIEW_ADDED;
    a->view_store = store;
    copy(a->view_item, sizeof(a->view_item), item);
    s->added++;
    s->misses = 0;
    s->pending[0] = '\0';
    s->asks = 0;
}

static void ask_store(grocery_script_t *s, grocery_action_t *a, bool again)
{
    s->asks++;
    say(a, again ? SAY_ASK_AGAIN : SAY_ASK, NULL, NULL);
    a->view = GROCERY_VIEW_ASK_STORE;
    copy(a->view_item, sizeof(a->view_item), s->pending);
    a->answer_ms = GROCERY_ANSWER_MS;
}

/* Kroger is the default: "I'll put eggs on the Kroger list." */
static void file_default(grocery_script_t *s, grocery_action_t *a)
{
    char item[GROCERY_ITEM_MAX];
    copy(item, sizeof(item), s->pending);
    confirm(s, a, item, GROCERY_STORE_KROGER);
    say(a, SAY_DEFAULT, item, NULL);
}

/* The store question went unanswered (or the answer wasn't a store): ask once more, then file to Kroger. */
static void no_store(grocery_script_t *s, grocery_action_t *a, bool walked_away)
{
    if (s->asks < 2) {
        ask_store(s, a, true);
        return;
    }
    file_default(s, a);
    a->end = walked_away;   /* nobody's there to say "anything else?" to */
}

/* Nothing usable heard: "try again?", then once more slowly, then sleep. */
static void missed(grocery_script_t *s, grocery_action_t *a)
{
    s->misses++;
    a->view = GROCERY_VIEW_RETRY;
    if (s->misses >= 2) {
        say(a, SAY_MISS_AGAIN, NULL, NULL);
        a->end = true;   /* don't nag: the next press starts afresh */
    } else {
        say(a, SAY_MISS, NULL, NULL);
    }
}

static void done(grocery_script_t *s, grocery_action_t *a)
{
    if (s->pending[0]) {
        file_default(s, a);   /* never drop an item that was waiting for its store */
    } else {
        say(a, SAY_DONE, NULL, NULL);
        a->view = GROCERY_VIEW_DONE;
    }
    a->end = true;
}

static void heard(grocery_script_t *s, grocery_action_t *a, const char *transcript)
{
    grocery_heard_t h;
    grocery_parse(transcript, &h);
    if (s->pending[0]) {
        if (h.store != GROCERY_STORE_NONE) {
            char item[GROCERY_ITEM_MAX];
            copy(item, sizeof(item), s->pending);
            confirm(s, a, item, h.store);
            say(a, SAY_ADDED, item, grocery_store_name(h.store));
        } else if (h.ends_session) {
            done(s, a);
        } else {
            no_store(s, a, false);
        }
        return;
    }
    if (h.ends_session) {   /* the whole utterance was "that's it" or the like */
        done(s, a);
        return;
    }
    if (!h.has_words || !h.item[0]) {
        missed(s, a);   /* nothing, noise, or only a store name */
        return;
    }
    if (h.store != GROCERY_STORE_NONE) {
        confirm(s, a, h.item, h.store);
        say(a, SAY_ADDED, h.item, grocery_store_name(h.store));
        return;
    }
    copy(s->pending, sizeof(s->pending), h.item);
    s->asks = 0;
    s->misses = 0;
    ask_store(s, a, false);
}

void grocery_script_step(grocery_script_t *s, grocery_event_t ev, const char *transcript, grocery_action_t *out)
{
    memset(out, 0, sizeof(*out));
    out->view = GROCERY_VIEW_READY;
    switch (ev) {
    case GROCERY_EV_TAP:
        done(s, out);
        break;
    case GROCERY_EV_SILENT:
        if (s->pending[0]) {
            no_store(s, out, false);
        } else if (s->added) {
            done(s, out);   /* let go without speaking: that's the end */
        } else {
            missed(s, out);
        }
        break;
    case GROCERY_EV_TOO_LONG:
        say(out, SAY_TOO_LONG, NULL, NULL);
        out->view = GROCERY_VIEW_RETRY;
        if (s->pending[0]) {
            out->answer_ms = GROCERY_ANSWER_MS;
        }
        break;
    case GROCERY_EV_HEARD:
        heard(s, out, transcript);
        break;
    case GROCERY_EV_OFFLINE:
        if (s->pending[0]) {
            file_default(s, out);   /* it goes to Muse once the button is back online */
        } else {
            say(out, SAY_OFFLINE, NULL, NULL);
            out->view = GROCERY_VIEW_OFFLINE;
        }
        out->end = true;
        break;
    case GROCERY_EV_NO_ANSWER:
        if (s->pending[0]) {
            no_store(s, out, true);
        }
        break;
    }
}
