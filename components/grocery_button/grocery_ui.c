/*
 * The round status screen (grocery_ui.h). 466 px circle on the AMOLED: black
 * is "off", so the background stays black and only the status glows. Text
 * stays inside the middle ~340 px so the circle never clips it.
 */
#include "grocery_ui.h"

#include <stdio.h>
#include <string.h>

#if LV_FONT_MONTSERRAT_40
#define FONT_TITLE (&lv_font_montserrat_40)
#else
#define FONT_TITLE (&lv_font_montserrat_28)
#endif
#define FONT_SUB (&lv_font_montserrat_28)
#define FONT_ITEM (&lv_font_montserrat_20)
#define FONT_SMALL (&lv_font_montserrat_16)

#define COLOR_TEXT 0xFFFFFF
#define COLOR_DIM 0x8E8E93
#define COLOR_GREEN 0x34C759
#define COLOR_RED 0xFF453A
#define COLOR_LISTEN 0x40C8E0   /* the recording rim */
#define COLOR_COSTCO 0xE8414F   /* Costco's red, lifted to read on black */
#define COLOR_KROGER 0x4C8DF6   /* Kroger's blue, likewise */
#define LOW_BATTERY_PCT 15

/* Vertical places, from the centre: icon, title, store line, item. */
#define Y_ICON -70
#define Y_TITLE 0
#define Y_SUB 62
#define Y_ITEM 122
#define Y_ITEM_NO_SUB 78

static lv_obj_t *s_root, *s_ring, *s_icon, *s_title, *s_sub, *s_item, *s_battery;

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, int width, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, width);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, y);
    lv_label_set_text(l, "");
    return l;
}

void grocery_ui_create(lv_obj_t *parent)
{
    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);

    /* Recording time, around the rim while the button is held. */
    s_ring = lv_arc_create(s_root);
    lv_obj_set_size(s_ring, lv_pct(96), lv_pct(96));
    lv_obj_center(s_ring);
    lv_arc_set_rotation(s_ring, 270);
    lv_arc_set_bg_angles(s_ring, 0, 360);
    lv_arc_set_range(s_ring, 0, 1000);
    lv_arc_set_value(s_ring, 0);
    lv_obj_remove_style(s_ring, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_ring, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_ring, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_ring, lv_color_hex(0x1C1C1E), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_ring, lv_color_hex(COLOR_LISTEN), LV_PART_INDICATOR);
    lv_obj_add_flag(s_ring, LV_OBJ_FLAG_HIDDEN);

    s_icon = label(s_root, FONT_TITLE, 120, Y_ICON);
    s_title = label(s_root, FONT_TITLE, 340, Y_TITLE);
    s_sub = label(s_root, FONT_SUB, 320, Y_SUB);
    s_item = label(s_root, FONT_ITEM, 300, Y_ITEM);
    s_battery = label(s_root, FONT_SMALL, 120, -186);
    lv_obj_set_style_text_color(s_battery, lv_color_hex(COLOR_DIM), 0);

}

static void set_text(lv_obj_t *l, const char *text)
{
    const char *now = lv_label_get_text(l);
    if (!now || strcmp(now, text)) {
        lv_label_set_text(l, text);
    }
}

static const char *battery_symbol(int pct)
{
    if (pct >= 90) return LV_SYMBOL_BATTERY_FULL;
    if (pct >= 60) return LV_SYMBOL_BATTERY_3;
    if (pct >= 35) return LV_SYMBOL_BATTERY_2;
    if (pct >= LOW_BATTERY_PCT) return LV_SYMBOL_BATTERY_1;
    return LV_SYMBOL_BATTERY_EMPTY;
}

static void draw_battery(const grocery_ui_state_t *st)
{
    char buf[24];
    if (st->battery_pct < 0) {
        buf[0] = '\0';
    } else {
        snprintf(buf, sizeof(buf), "%s%s", st->charging ? LV_SYMBOL_CHARGE " " : "", battery_symbol(st->battery_pct));
    }
    set_text(s_battery, buf);
    bool low = st->battery_pct >= 0 && st->battery_pct < LOW_BATTERY_PCT && !st->charging;
    lv_obj_set_style_text_color(s_battery, lv_color_hex(low ? COLOR_RED : COLOR_DIM), 0);
}

static void show(lv_obj_t *l, bool on)
{
    if (on) {
        lv_obj_remove_flag(l, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    }
}

/* "Thinking" with dots that count up while it waits. */
static void dots(char *out, size_t cap, const char *word)
{
    int n = (int)(lv_tick_get() / 400 % 4);
    snprintf(out, cap, "%s%.*s", word, n, "...");
}

void grocery_ui_update(const grocery_ui_state_t *st)
{
    if (!s_root) {
        return;
    }
    draw_battery(st);

    const char *icon = "", *title = "", *sub = "", *item = "";
    uint32_t icon_color = COLOR_TEXT, sub_color = COLOR_DIM;
    char title_buf[48];
    const lv_font_t *item_font = FONT_ITEM;

    switch (st->screen) {
    case GROCERY_SCREEN_SETUP:
        icon = LV_SYMBOL_SETTINGS;
        icon_color = COLOR_DIM;
        title = "Set me up";
        item = st->detail;
        break;
    case GROCERY_SCREEN_READY:
        title = "Ready";
        sub = "Hold the top button";
        break;
    case GROCERY_SCREEN_LISTENING:
        title = "Listening...";   /* the rim shows it's recording */
        break;
    case GROCERY_SCREEN_CONNECTING:
        icon = LV_SYMBOL_WIFI;
        icon_color = COLOR_DIM;
        dots(title_buf, sizeof(title_buf), "Connecting");
        title = title_buf;
        break;
    case GROCERY_SCREEN_THINKING:
        dots(title_buf, sizeof(title_buf), "Thinking");
        title = title_buf;
        break;
    case GROCERY_SCREEN_ADDED:
        icon = LV_SYMBOL_OK;
        icon_color = COLOR_GREEN;
        title = "Added";
        sub = grocery_store_name(st->store);
        sub_color = st->store == GROCERY_STORE_COSTCO ? COLOR_COSTCO : COLOR_KROGER;
        item = st->item;
        break;
    case GROCERY_SCREEN_ASK_STORE:
        title = "Costco or Kroger?";
        item = st->item;
        break;
    case GROCERY_SCREEN_RETRY:
        icon = LV_SYMBOL_REFRESH;
        icon_color = COLOR_DIM;
        title = "Try again";
        sub = st->detail[0] ? st->detail : "Hold and speak";
        break;
    case GROCERY_SCREEN_OFFLINE:
        icon = LV_SYMBOL_WIFI;
        icon_color = COLOR_RED;
        title = "Can't connect";
        item = st->detail[0] ? st->detail : "Check the Wi-Fi";
        break;
    case GROCERY_SCREEN_DONE:
        icon = LV_SYMBOL_OK;
        icon_color = COLOR_GREEN;
        title = "Got it";
        break;
    case GROCERY_SCREEN_LOW_BATTERY:
        icon = LV_SYMBOL_BATTERY_EMPTY;
        icon_color = COLOR_RED;
        title = "Charge me";
        sub = "Battery's low";
        break;
    }

    set_text(s_icon, icon);
    lv_obj_set_style_text_color(s_icon, lv_color_hex(icon_color), 0);
    set_text(s_title, title);
    set_text(s_sub, sub);
    lv_obj_set_style_text_color(s_sub, lv_color_hex(sub_color), 0);
    set_text(s_item, item);
    lv_obj_set_style_text_font(s_item, item_font, 0);
    lv_obj_set_style_text_color(s_item, lv_color_hex(st->screen == GROCERY_SCREEN_SETUP ? COLOR_TEXT : COLOR_DIM), 0);
    /* Long items stay to two lines; setup text gets room to wrap. */
    lv_label_set_long_mode(s_item, st->screen == GROCERY_SCREEN_SETUP ? LV_LABEL_LONG_MODE_WRAP : LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(s_item, st->screen == GROCERY_SCREEN_SETUP ? LV_SIZE_CONTENT : 2 * lv_font_get_line_height(item_font) + 4);
    /* A two-line title (the store question) pushes the rest down. */
    lv_obj_update_layout(s_title);
    int title_h = lv_obj_get_height(s_title);
    int extra = title_h > lv_font_get_line_height(FONT_TITLE) + 8 ? title_h / 2 : 0;
    lv_obj_align(s_title, LV_ALIGN_CENTER, 0, Y_TITLE + extra / 2);
    lv_obj_align(s_sub, LV_ALIGN_CENTER, 0, Y_SUB + extra);
    lv_obj_align(s_item, LV_ALIGN_CENTER, 0, (sub[0] ? Y_ITEM : Y_ITEM_NO_SUB) + extra);
    lv_obj_align(s_icon, LV_ALIGN_CENTER, 0, Y_ICON - extra / 2);

    bool ring = st->screen == GROCERY_SCREEN_LISTENING;
    show(s_ring, ring);
    if (ring) {
        float p = st->progress < 0 ? 0 : st->progress > 1 ? 1 : st->progress;
        lv_arc_set_value(s_ring, (int32_t)(p * 1000));
        /* The rim brightens with the voice. */
        float lv = st->level < 0 ? 0 : st->level > 1 ? 1 : st->level;
        lv_obj_set_style_arc_opa(s_ring, (lv_opa_t)(140 + 115 * lv), LV_PART_INDICATOR);
    }
}
