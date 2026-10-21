/* The voice inbox's outbox (grocery_outbox.h). */
#include "grocery_outbox.h"

#include <stdio.h>
#include <string.h>

void grocery_outbox_init(grocery_outbox_t *ob)
{
    memset(ob, 0, sizeof(*ob));
}

/* Copies an item, turning tabs and newlines into spaces so it stays one line. */
static void copy_line(char *out, size_t cap, const char *s)
{
    size_t o = 0;
    for (; *s && o + 1 < cap; s++) {
        out[o++] = (*s == '\t' || *s == '\n' || *s == '\r') ? ' ' : *s;
    }
    /* Don't leave half a UTF-8 character at the end. */
    if (((unsigned char)*s & 0xC0) == 0x80) {   /* cut inside a character */
        while (o && ((unsigned char)out[o - 1] & 0xC0) == 0x80) {
            o--;
        }
        if (o) {
            o--;   /* and its lead byte */
        }
    }
    out[o] = '\0';
}

bool grocery_outbox_add(grocery_outbox_t *ob, const char *item, grocery_store_t store)
{
    if (!item || !item[0] || ob->count >= GROCERY_OUTBOX_MAX) {
        return false;
    }
    grocery_entry_t *e = &ob->e[ob->count++];
    e->store = store == GROCERY_STORE_COSTCO ? GROCERY_STORE_COSTCO : GROCERY_STORE_KROGER;
    copy_line(e->item, sizeof(e->item), item);
    return true;
}

int grocery_outbox_message(const grocery_outbox_t *ob, char *out, size_t cap)
{
    if (!ob->count || cap < 64) {
        if (cap) {
            out[0] = '\0';
        }
        return 0;
    }
    size_t o = (size_t)snprintf(out, cap, "Grocery button (fridge): please put %s on my grocery lists.",
                                ob->count == 1 ? "this" : "these");
    int n = 0;
    for (; n < ob->count; n++) {
        const grocery_entry_t *e = &ob->e[n];
        int w = snprintf(out + o, cap - o, "\n- %s: %s", grocery_store_name(e->store), e->item);
        if (w < 0 || (size_t)w >= cap - o) {
            out[o] = '\0';   /* the rest go in the next message */
            break;
        }
        o += (size_t)w;
    }
    return n;
}

void grocery_outbox_delivered(grocery_outbox_t *ob, int n)
{
    if (n <= 0) {
        return;
    }
    if (n >= ob->count) {
        ob->count = 0;
        return;
    }
    memmove(ob->e, ob->e + n, (size_t)(ob->count - n) * sizeof(ob->e[0]));
    ob->count -= n;
}

size_t grocery_outbox_save(const grocery_outbox_t *ob, char *out, size_t cap)
{
    size_t o = 0;
    if (cap) {
        out[0] = '\0';
    }
    for (int i = 0; i < ob->count; i++) {
        int w = snprintf(out + o, cap - o, "%c\t%s\n", ob->e[i].store == GROCERY_STORE_COSTCO ? 'C' : 'K',
                         ob->e[i].item);
        if (w < 0 || (size_t)w >= cap - o) {
            out[o] = '\0';
            break;
        }
        o += (size_t)w;
    }
    return o;
}

void grocery_outbox_load(grocery_outbox_t *ob, const char *blob)
{
    grocery_outbox_init(ob);
    const char *p = blob;
    while (p && *p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len > 2 && p[1] == '\t' && (p[0] == 'C' || p[0] == 'K')) {
            char item[GROCERY_ITEM_MAX];
            size_t n = len - 2 < sizeof(item) - 1 ? len - 2 : sizeof(item) - 1;
            memcpy(item, p + 2, n);
            item[n] = '\0';
            grocery_outbox_add(ob, item, p[0] == 'C' ? GROCERY_STORE_COSTCO : GROCERY_STORE_KROGER);
        }
        p = nl ? nl + 1 : NULL;
    }
}
