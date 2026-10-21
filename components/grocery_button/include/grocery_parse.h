/*
 * Reading a transcript: which store (if any) it names, the grocery item with
 * the filler and store words taken out, and whether it ends the session
 * ("that's it"). Plain C with no ESP-IDF dependencies, so the host tests
 * build it too.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GROCERY_STORE_NONE = 0,
    GROCERY_STORE_COSTCO,
    GROCERY_STORE_KROGER,
} grocery_store_t;

#define GROCERY_ITEM_MAX 96

typedef struct {
    grocery_store_t store;          /* NONE when neither or both are named */
    char item[GROCERY_ITEM_MAX];    /* "" when nothing is left once store and filler words go */
    bool ends_session;              /* "that's it", "no thanks", "nope"... */
    bool has_words;                 /* any letters at all */
} grocery_heard_t;

void grocery_parse(const char *transcript, grocery_heard_t *out);

/* "Costco", "Kroger" (or "" for NONE). */
const char *grocery_store_name(grocery_store_t store);

#ifdef __cplusplus
}
#endif
