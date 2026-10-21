/*
 * The conversation script (docs/03-conversation-design.md) as a state
 * machine: the voice task reports what happened on each press (or that an
 * answer never came), and gets back what to say, what to show, whether an
 * item is confirmed, and whether the session is over. No I/O, no timers of
 * its own, no ESP-IDF: the host tests drive it directly.
 *
 * One session runs from the press that wakes the button until it goes back
 * to sleep. The voice task owns the session's clock: the 30 s of silence
 * that end a session quietly, and the answer window (answer_ms) after which
 * it reports GROCERY_EV_NO_ANSWER.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "grocery_parse.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GROCERY_EV_TAP,         /* a short press, not a hold: ends the session */
    GROCERY_EV_SILENT,      /* held and let go without speaking */
    GROCERY_EV_TOO_LONG,    /* held past the recording cap */
    GROCERY_EV_HEARD,       /* a transcript ("" when nothing usable came back) */
    GROCERY_EV_OFFLINE,     /* transcription couldn't be reached */
    GROCERY_EV_NO_ANSWER,   /* answer_ms passed with no press */
} grocery_event_t;

/* What the round screen shows (docs/03: status only). */
typedef enum {
    GROCERY_VIEW_READY,      /* session open, waiting for a press */
    GROCERY_VIEW_ADDED,      /* "Added" + store; item underneath */
    GROCERY_VIEW_ASK_STORE,  /* "Costco or Kroger?"; item underneath */
    GROCERY_VIEW_RETRY,      /* didn't catch it / too long: try again */
    GROCERY_VIEW_OFFLINE,    /* can't reach the internet */
    GROCERY_VIEW_DONE,       /* "Got it" */
} grocery_view_t;

#define GROCERY_SAY_MAX 192
#define GROCERY_ANSWER_MS 10000   /* how long a question waits for a press */

typedef struct {
    char say[GROCERY_SAY_MAX];        /* speak this ("" for nothing) */
    grocery_view_t view;
    grocery_store_t view_store;       /* for VIEW_ADDED */
    char view_item[GROCERY_ITEM_MAX]; /* for VIEW_ADDED / VIEW_ASK_STORE */
    bool submit;                      /* an item is confirmed: */
    char item[GROCERY_ITEM_MAX];      /*   submit_item(item, store) */
    grocery_store_t store;
    uint32_t answer_ms;               /* > 0: report NO_ANSWER if no press comes within this */
    bool end;                         /* after saying it, the session is over */
} grocery_action_t;

typedef struct {
    char pending[GROCERY_ITEM_MAX];   /* an item waiting for its store */
    int asks;                         /* store questions asked for it */
    int misses;                       /* didn't-catch-its in a row */
    int added;                        /* items confirmed this session */
} grocery_script_t;

void grocery_script_start(grocery_script_t *s);
void grocery_script_step(grocery_script_t *s, grocery_event_t ev, const char *transcript, grocery_action_t *out);

#ifdef __cplusplus
}
#endif
