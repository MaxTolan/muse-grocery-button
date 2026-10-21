/*
 * The source tag on every message the button sends to Muse (issue #2): the
 * text message sent just ahead of each voice note (CONFIG_MUSE_VOICE_NOTE_PRELUDE,
 * checked at build time by this component's CMakeLists.txt; Muse strips text
 * from the note itself) and the outbox's item batches. Always the same
 * string, always first, followed by one space, so Muse can key on it. It
 * labels the message, not the groceries: it's never spoken, shown on the
 * screen, or put in an item.
 */
#pragma once

#define GROCERY_SOURCE_TAG "[fridge-button]"

#include <string.h>

/* s without a leading source tag (and the space after it), e.g. if Muse echoes it. */
static inline const char *grocery_untagged(const char *s)
{
    size_t n = sizeof(GROCERY_SOURCE_TAG) - 1;
    if (s && strncmp(s, GROCERY_SOURCE_TAG, n) == 0) {
        s += n;
        while (*s == ' ' || *s == ':') {
            s++;
        }
    }
    return s;
}
