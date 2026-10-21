/*
 * Confirmed items on their way to Muse (the voice inbox, docs/02 step 5).
 * submit_item(text, store) lands here; the device glue saves the outbox to
 * flash on every change and sends everything in it as one chat message to
 * the owner's Muse, which files the items on the Costco and Kroger lists.
 * The message starts with GROCERY_SOURCE_TAG (grocery_source.h). Items
 * leave only once Muse has acknowledged the message, so nothing is
 * lost to a dropped connection, a dead battery or deep sleep.
 *
 * Plain C, no ESP-IDF: the host tests build it.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "grocery_parse.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GROCERY_OUTBOX_MAX 32
#define GROCERY_MESSAGE_MAX (128 + GROCERY_OUTBOX_MAX * (GROCERY_ITEM_MAX + 16))
#define GROCERY_BLOB_MAX (GROCERY_OUTBOX_MAX * (GROCERY_ITEM_MAX + 4))

typedef struct {
    grocery_store_t store;
    char item[GROCERY_ITEM_MAX];
} grocery_entry_t;

typedef struct {
    grocery_entry_t e[GROCERY_OUTBOX_MAX];
    int count;
} grocery_outbox_t;

void grocery_outbox_init(grocery_outbox_t *ob);
/* False if the outbox is full or the item is empty. */
bool grocery_outbox_add(grocery_outbox_t *ob, const char *item, grocery_store_t store);
/* The chat message for every item in the outbox; returns how many it covers (0: nothing to send). */
int grocery_outbox_message(const grocery_outbox_t *ob, char *out, size_t cap);
/* Muse has the message covering the first n items. */
void grocery_outbox_delivered(grocery_outbox_t *ob, int n);

/* One "C\titem\n" or "K\titem\n" line per item, for flash. */
size_t grocery_outbox_save(const grocery_outbox_t *ob, char *out, size_t cap);
void grocery_outbox_load(grocery_outbox_t *ob, const char *blob);

#ifdef __cplusplus
}
#endif
