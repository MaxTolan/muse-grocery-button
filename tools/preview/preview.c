/*
 * Renders the grocery button's round status screen (components/grocery_button
 * /grocery_ui.c) in each state, headless, at the panel's 466 x 466, and writes
 * PNGs: one per state plus a contact sheet. Outside the round glass is grey.
 *
 *   grocery_preview OUT_DIR
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

#include "grocery_ui.h"
#include "lvgl.h"

#define W 466
#define H 466
#define COLS 4

static uint16_t s_draw[W * H];
static uint8_t s_rgb[W * H * 3];
static uint32_t s_tick = 800;   /* "Thinking.." shows two dots */

static uint32_t tick(void)
{
    return s_tick;
}

static void flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    const uint16_t *src = (const uint16_t *)px;
    for (int y = area->y1; y <= area->y2; y++) {
        for (int x = area->x1; x <= area->x2; x++) {
            uint16_t c = *src++;
            uint8_t *o = &s_rgb[(y * W + x) * 3];
            o[0] = (uint8_t)(((c >> 11) & 0x1F) * 255 / 31);
            o[1] = (uint8_t)(((c >> 5) & 0x3F) * 255 / 63);
            o[2] = (uint8_t)((c & 0x1F) * 255 / 31);
        }
    }
    lv_display_flush_ready(disp);
}

/* ---- a minimal PNG writer: RGB, compressed with zlib ---- */

static uint32_t s_crc_table[256];

static void crc_init(void)
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) {
            c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        }
        s_crc_table[n] = c;
    }
}

static uint32_t crc(uint32_t c, const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        c = s_crc_table[(c ^ buf[i]) & 0xFF] ^ (c >> 8);
    }
    return c;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void chunk(FILE *f, const char *type, const uint8_t *data, size_t len)
{
    uint8_t hdr[8];
    put32(hdr, (uint32_t)len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (len) {
        fwrite(data, 1, len, f);
    }
    uint32_t c = crc(0xFFFFFFFFu, (const uint8_t *)type, 4);
    c = crc(c, data, len) ^ 0xFFFFFFFFu;
    uint8_t t[4];
    put32(t, c);
    fwrite(t, 1, 4, f);
}

static int write_png(const char *path, const uint8_t *rgb, int w, int h)
{
    size_t raw_len = (size_t)h * (1 + (size_t)w * 3);
    uint8_t *raw = malloc(raw_len);
    for (int y = 0; y < h; y++) {
        raw[y * (1 + w * 3)] = 0;   /* no filter */
        memcpy(raw + y * (1 + w * 3) + 1, rgb + (size_t)y * w * 3, (size_t)w * 3);
    }
    uLongf z_cap = compressBound((uLong)raw_len);
    uint8_t *z = malloc(z_cap);
    if (compress2(z, &z_cap, raw, (uLong)raw_len, 9) != Z_OK) {
        free(raw);
        free(z);
        return -1;
    }
    uint8_t *o = z + z_cap;
    FILE *f = fopen(path, "wb");
    if (!f) {
        free(raw);
        free(z);
        return -1;
    }
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    put32(ihdr, (uint32_t)w);
    put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;    /* bit depth */
    ihdr[9] = 2;    /* RGB */
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (size_t)(o - z));
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
    return 0;
}

/* ---- states ---- */

typedef struct {
    const char *name;
    grocery_ui_state_t st;
} shot_t;

static void round_mask(uint8_t *rgb)
{
    const float r = W / 2.0f;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            float dx = x + 0.5f - r, dy = y + 0.5f - r;
            if (dx * dx + dy * dy > r * r) {
                uint8_t *p = &rgb[(y * W + x) * 3];
                p[0] = p[1] = p[2] = 0x3A;
            }
        }
    }
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : ".";
    crc_init();
    lv_init();
    lv_tick_set_cb(tick);
    lv_display_t *disp = lv_display_create(W, H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, s_draw, NULL, sizeof(s_draw), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(disp, flush);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    grocery_ui_create(scr);

    const shot_t shots[] = {
        /* Now: Muse voice notes, text replies. */
        { "idle", { .screen = GROCERY_SCREEN_IDLE, .battery_pct = 82, .t = 1.0f } },
        { "listening", { .screen = GROCERY_SCREEN_LISTENING, .progress = 0.35f, .level = 0.7f, .battery_pct = 82 } },
        { "thinking", { .screen = GROCERY_SCREEN_THINKING, .battery_pct = 82, .detail = "\"Eggs and Fairlife milk\"" } },
        { "reply", { .screen = GROCERY_SCREEN_REPLY, .battery_pct = 82,
                     .reply = "Added eggs and Fairlife skim milk to your Kroger list." } },
        { "idle-status", { .screen = GROCERY_SCREEN_IDLE, .battery_pct = 12, .t = 2.4f, .detail = "No Wi-Fi" } },
        { "setup", { .screen = GROCERY_SCREEN_SETUP, .battery_pct = 60, .charging = true,
                     .detail = "In the Muse app: Settings > Devices > Add Device, then pick MuseGadget-42464C." } },
        /* With Google speech (issue #1). */
        { "ready", { .screen = GROCERY_SCREEN_READY, .battery_pct = 82 } },
        { "added-kroger", { .screen = GROCERY_SCREEN_ADDED, .store = GROCERY_STORE_KROGER,
                            .item = "Fairlife skim milk, 52 ounce", .battery_pct = 82 } },
        { "ask-store", { .screen = GROCERY_SCREEN_ASK_STORE, .item = "Eggs", .battery_pct = 82 } },
        { "added-costco", { .screen = GROCERY_SCREEN_ADDED, .store = GROCERY_STORE_COSTCO, .item = "Eggs",
                            .battery_pct = 82 } },
        { "done", { .screen = GROCERY_SCREEN_DONE, .battery_pct = 82 } },
        { "retry", { .screen = GROCERY_SCREEN_RETRY, .battery_pct = 82 } },
        { "connecting", { .screen = GROCERY_SCREEN_CONNECTING, .battery_pct = 82 } },
        { "offline", { .screen = GROCERY_SCREEN_OFFLINE, .detail = "No Wi-Fi", .battery_pct = 82 } },
        { "low-battery", { .screen = GROCERY_SCREEN_LOW_BATTERY, .battery_pct = 4 } },
    };
    const int n = (int)(sizeof(shots) / sizeof(shots[0]));
    const int rows = (n + COLS - 1) / COLS;
    const int sw = COLS * W, sh = rows * H;
    uint8_t *sheet = calloc((size_t)sw * sh * 3, 1);

    for (int i = 0; i < n; i++) {
        grocery_ui_update(&shots[i].st);
        lv_obj_invalidate(scr);
        lv_refr_now(disp);
        round_mask(s_rgb);
        char path[512];
        snprintf(path, sizeof(path), "%s/%s.png", out, shots[i].name);
        if (write_png(path, s_rgb, W, H)) {
            fprintf(stderr, "can't write %s\n", path);
            return 1;
        }
        int cx = (i % COLS) * W, cy = (i / COLS) * H;
        for (int y = 0; y < H; y++) {
            memcpy(sheet + ((size_t)(cy + y) * sw + cx) * 3, s_rgb + (size_t)y * W * 3, (size_t)W * 3);
        }
    }
    for (int i = n; i < rows * COLS; i++) {   /* empty cells */
        int cx = (i % COLS) * W, cy = (i / COLS) * H;
        for (int y = 0; y < H; y++) {
            memset(sheet + ((size_t)(cy + y) * sw + cx) * 3, 0x3A, (size_t)W * 3);
        }
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/contact-sheet.png", out);
    write_png(path, sheet, sw, sh);
    printf("wrote %d previews and contact-sheet.png to %s\n", n, out);
    return 0;
}
