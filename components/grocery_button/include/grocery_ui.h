/*
 * The round 1.75" status screen (docs/03, "Display"): Jollybot while waiting,
 * "Listening..." / "Thinking...", Muse's reply, or with Google speech (issue
 * #1) "Added" with the store and the store question; plus a small battery
 * icon. Never a grocery list. LVGL only (no ESP-IDF), so the host preview
 * tool renders it too.
 */
#pragma once

#include <stdbool.h>

#include "grocery_parse.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GROCERY_SCREEN_SETUP,       /* not paired with Muse yet: how to set up */
    GROCERY_SCREEN_IDLE,        /* waiting: Jollybot, with any status line under it */
    GROCERY_SCREEN_REPLY,       /* Muse's reply text */
    GROCERY_SCREEN_READY,       /* awake, waiting for a press */
    GROCERY_SCREEN_LISTENING,   /* the button is held */
    GROCERY_SCREEN_CONNECTING,  /* recorded, waiting for Wi-Fi */
    GROCERY_SCREEN_THINKING,    /* transcribing */
    GROCERY_SCREEN_ADDED,       /* "Added" + store, item underneath */
    GROCERY_SCREEN_ASK_STORE,   /* "Costco or Kroger?" */
    GROCERY_SCREEN_RETRY,       /* didn't catch it / too long */
    GROCERY_SCREEN_OFFLINE,     /* no internet */
    GROCERY_SCREEN_DONE,        /* "Got it" */
    GROCERY_SCREEN_LOW_BATTERY, /* too low to take an item */
} grocery_screen_t;

#define GROCERY_UI_TEXT_MAX 160
#define GROCERY_UI_REPLY_MAX 400   /* a page of reply (MUSE_CAPTION_MAX) */
/* A reply page that fits the round screen: lines of up to this many characters. */
#define GROCERY_UI_REPLY_COLS 20
#define GROCERY_UI_REPLY_LINES 6

typedef struct {
    grocery_screen_t screen;
    grocery_store_t store;                /* ADDED */
    char item[GROCERY_ITEM_MAX];          /* ADDED, ASK_STORE */
    char detail[GROCERY_UI_TEXT_MAX];     /* SETUP: how to pair; OFFLINE / RETRY / IDLE: why; THINKING: what was heard */
    char reply[GROCERY_UI_REPLY_MAX];     /* REPLY */
    float t;                              /* seconds: Jollybot's blinking and bobbing */
    float happy;                          /* 0..1: Jollybot's happy hop */
    float progress;                       /* LISTENING: share of the recording cap used, 0..1 */
    float level;                          /* LISTENING: mic level, 0..1 */
    int battery_pct;                      /* -1: no battery reading */
    bool charging;
} grocery_ui_state_t;

/* Builds the screen as a full-size child of parent. */
void grocery_ui_create(lv_obj_t *parent);
/* Redraws whatever changed. */
void grocery_ui_update(const grocery_ui_state_t *st);

#ifdef __cplusplus
}
#endif
