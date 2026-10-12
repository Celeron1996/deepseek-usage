#include "ui.h"

#include <stdio.h>

#include "esp_lvgl_port.h"
#include "lvgl.h"

#define COLOR_BG        lv_color_hex(0x0B1220)
#define COLOR_HEADER    lv_color_hex(0x111C2E)
#define COLOR_ACCENT    lv_color_hex(0x38BDF8)
#define COLOR_TEXT      lv_color_hex(0xE5E7EB)
#define COLOR_TEXT_DIM  lv_color_hex(0x9CA3AF)
#define COLOR_DIVIDER   lv_color_hex(0x1F2A3A)

static lv_obj_t *s_list;    /* flex column container acting as the list */
static lv_obj_t *s_status;
static lv_obj_t *s_spinner;

/* One full width row: an icon label plus a text label (modern replacement of
 * the deprecated lv_list widget, see the LVGL "flex list" example). */
static lv_obj_t *add_row(const char *icon, const char *text)
{
    lv_obj_t *row = lv_button_create(s_list);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(row, COLOR_DIVIDER, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(row, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(row, 7, LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, 8, LV_PART_MAIN);

    lv_obj_t *icon_label = lv_label_create(row);
    lv_label_set_text(icon_label, icon);
    lv_obj_set_style_text_color(icon_label, COLOR_ACCENT, LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_flex_grow(label, 1);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);

    return row;
}

void ui_init(void)
{
    lvgl_port_lock(0);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, COLOR_BG, LV_PART_MAIN);
    lv_obj_set_scrollable(scr, false);

    /* header: title + scanning spinner */
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), 38);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, COLOR_HEADER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_left(header, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_right(header, 6, LV_PART_MAIN);
    lv_obj_set_scrollable(header, false);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Wi-Fi Scanner");
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    s_spinner = lv_spinner_create(header);
    lv_obj_set_size(s_spinner, 22, 22);
    lv_obj_set_style_arc_color(s_spinner, COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_spinner, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_spinner, 3, LV_PART_MAIN);
    lv_obj_align(s_spinner, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_clickable(s_spinner, false);
    lv_obj_set_hidden(s_spinner, true);
    lv_spinner_set_anim_params(s_spinner, 1000, 60);

    /* AP list: a flex column, the modern way to build lists in LVGL 9.6 */
    s_list = lv_obj_create(scr);
    lv_obj_set_size(s_list, LV_PCT(100), 176);
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, 38);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_list, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_list, 0, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_list, &lv_font_montserrat_12, LV_PART_MAIN);

    lv_obj_t *hint = lv_label_create(s_list);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_label_set_text(hint, "Waiting for scan...");
    lv_obj_set_style_text_color(hint, COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_pad_all(hint, 8, LV_PART_MAIN);

    /* status bar */
    s_status = lv_label_create(scr);
    lv_label_set_text(s_status, "Ready");
    lv_obj_set_style_text_color(s_status, COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_align(s_status, LV_ALIGN_BOTTOM_MID, 0, -4);

    lvgl_port_unlock();
}

void ui_update_ap_list(const wifi_ap_record_t *records, uint16_t count)
{
    lvgl_port_lock(0);

    lv_obj_clean(s_list);

    if (count == 0) {
        add_row(LV_SYMBOL_WARNING, "No network found");
    } else {
        char item[64];
        for (uint16_t i = 0; i < count; i++) {
            const char *ssid = (const char *)records[i].ssid;
            if (ssid[0] == '\0') {
                ssid = "<hidden>";
            }
            snprintf(item, sizeof(item), "%s  %d dBm", ssid, (int)records[i].rssi);
            add_row(LV_SYMBOL_WIFI, item);
        }
    }

    lv_label_set_text_fmt(s_status, "%d networks - press button to rescan", (int)count);
    lvgl_port_unlock();
}

void ui_set_status(const char *text)
{
    lvgl_port_lock(0);
    lv_label_set_text(s_status, text);
    lvgl_port_unlock();
}

void ui_set_scanning(bool scanning)
{
    lvgl_port_lock(0);
    lv_obj_set_hidden(s_spinner, !scanning);
    lvgl_port_unlock();
}
