/*
 * Between the voice task (grocery_voice.c) and the rest of the device glue
 * (grocery_glue.c): the screen, the voice inbox, power and pairing state.
 * ESP-IDF only; private to the component.
 */
#pragma once

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "grocery_parse.h"
#include "grocery_ui.h"

/* Starts the push-to-talk loop (muse_ext voice_start). */
int grocery_voice_start(QueueHandle_t queue);

/* ---- screen (any task) ---- */

/* What the screen shows next; battery and setup state are filled in by the glue. */
void grocery_view(grocery_screen_t screen, grocery_store_t store, const char *item, const char *detail);
/* While listening: recording progress and mic level, 0..1. */
void grocery_view_listening(float progress, float level);

/* ---- voice inbox ---- */

/* submit_item(text, store): queued in flash, then sent to Muse. */
void grocery_inbox_submit(const char *item, grocery_store_t store);
/* Items not yet acknowledged by Muse. */
bool grocery_inbox_pending(void);
/* Sends what's waiting when Muse is in reach; call often from the voice task. */
void grocery_inbox_poll(void);

/* ---- power and pairing ---- */

/* Paired with Muse (an SDK token and Muse configured). */
bool grocery_paired(void);
/* Below the low-battery mark, on battery. */
bool grocery_battery_low(void);
/* Too low to take an item without risking a mid-sentence shutdown. */
bool grocery_battery_critical(void);
/* This boot is a wake from deep sleep by the talk button, still held. */
bool grocery_woke_by_press(void);
/* The talk button's level, read straight from its pin (for the press that woke the chip). */
bool grocery_talk_held(void);
/* End of session: deep sleep on battery (doesn't return), else the screen goes dark. */
void grocery_sleep(void);
