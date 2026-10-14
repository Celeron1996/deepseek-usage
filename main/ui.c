#include "ui.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_lvgl_port.h"
#include "lvgl.h"

#define COL_BG          lv_color_hex(0xFFFFFF) /* white page */
#define COL_HEADER      lv_color_hex(0x2563EB) /* blue header bar */
#define COL_ACCENT      lv_color_hex(0x2563EB) /* blue accent (ssid / url) */
#define COL_TEXT        lv_color_hex(0x1E293B) /* dark slate text */
#define COL_DIM         lv_color_hex(0x64748B) /* secondary text */
#define COL_HEADER_DIM  lv_color_hex(0xDBEAFE) /* light blue on header */
#define COL_AMOUNT      lv_color_hex(0x1D4ED8) /* strong blue amount */
#define COL_SPINNER_BG  lv_color_hex(0xDBEAFE) /* spinner track */
#define COL_WIFI_OK     lv_color_hex(0x16A34A) /* green: connected */
#define COL_WIFI_OFF    lv_color_hex(0x94A3B8) /* gray: disconnected */
#define COL_WIFI_SLASH  lv_color_hex(0xDC2626) /* red slash on the off icon */

/* The UI is English only; the status line may embed an SSID, which is
 * rendered with the default Montserrat font as well. */
#define SSID_FONT (&lv_font_montserrat_14)

/* DeepSeek mascot animations, pre-converted to 88x88 RGB565 frames living in
 * flash (32 frames @ 30 ms each). Drawing straight from flash costs no RAM.
 * normal = idle, charge = balance increased, expend = balance decreased. */
#define GIF_W          88
#define GIF_H          88
#define GIF_FRAMES     32
#define GIF_PERIOD_MS  30
#define GIF_SETS       3

/* charge/expend are one-shot scenes: they loop this many times (about
 * 4 seconds), then freeze and ask for an immediate balance re-check. */
#define GIF_SPECIAL_LOOPS 4

/* Full-screen static flash shown briefly when the balance changes. */
#define GIF_FLASH_MS   2000
#define FLASH_W        240
#define FLASH_H        240

/* Flash overlay zoom: the still grows in when it appears and shrinks out
 * again before the amount transition. 256 = the original size. */
#define FLASH_SCALE_MIN  64    /* start/end size of the zoom (1/4) */
#define FLASH_SCALE_MAX  256
#define FLASH_GROW_MS    300
#define FLASH_SHRINK_MS  250

/* Amount transition played after the flash: the new value slides in from
 * below (balance up) or from above (balance down). The slide starts a short
 * pause after the flash disappears so the old value stays readable. */
#define AMOUNT_DELAY_MS 500
#define AMOUNT_SLIDE_PX 28
#define AMOUNT_ANIM_MS  400

extern const uint8_t deepseek_charge_big_bin_start[] asm("_binary_deepseek_charge_big_bin_start");
extern const uint8_t deepseek_expend_big_bin_start[] asm("_binary_deepseek_expend_big_bin_start");

static const uint8_t *const s_flash_bin[2] = {
    deepseek_charge_big_bin_start,
    deepseek_expend_big_bin_start,
};
static lv_image_dsc_t s_flash_dsc[2];
static lv_obj_t *s_flash;
static lv_obj_t *s_flash_img;
static lv_timer_t *s_flash_timer;
static lv_timer_t *s_apply_timer;    /* pause between flash and amount slide */
static char s_pending_amount[40];    /* amount shown after the flash ends */
static ui_gif_t s_pending_gif;
static bool s_pending_valid;

extern const uint8_t deepseek_normal_frames_bin_start[] asm("_binary_deepseek_normal_frames_bin_start");
extern const uint8_t deepseek_charge_frames_bin_start[] asm("_binary_deepseek_charge_frames_bin_start");
extern const uint8_t deepseek_expend_frames_bin_start[] asm("_binary_deepseek_expend_frames_bin_start");

static const uint8_t *const s_gif_bin[GIF_SETS] = {
    deepseek_normal_frames_bin_start,
    deepseek_charge_frames_bin_start,
    deepseek_expend_frames_bin_start,
};

static lv_image_dsc_t s_gif_dsc[GIF_SETS][GIF_FRAMES];
static lv_obj_t *s_gif_img;
static uint32_t s_gif_frame;
static uint8_t s_gif_set = UI_GIF_NORMAL;
static uint8_t s_gif_loops;          /* completed loops of the current special set */
static void (*s_gif_done_cb)(void);  /* invoked once when a special set finishes */

static lv_obj_t *s_conn_status; /* screen 2 */
static lv_obj_t *s_amount;      /* screen 3 */
static lv_obj_t *s_wifi_icon;
static lv_obj_t *s_wifi_slash;
static lv_obj_t *s_api;
static lv_obj_t *s_url;
static lv_obj_t *s_clock;

static lv_obj_t *screen_prepare(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, COL_BG, LV_PART_MAIN);
    lv_obj_set_scrollable(scr, false);
    lv_obj_set_style_text_font(scr, &lv_font_montserrat_14, LV_PART_MAIN);

    s_conn_status = NULL;
    s_amount = NULL;
    s_wifi_icon = NULL;
    s_wifi_slash = NULL;
    s_api = NULL;
    s_url = NULL;
    s_clock = NULL;
    s_gif_img = NULL;
    s_flash = NULL;
    s_flash_img = NULL;
    if (s_flash_timer != NULL) {
        lv_timer_delete(s_flash_timer);
        s_flash_timer = NULL;
    }
    if (s_apply_timer != NULL) {
        lv_timer_delete(s_apply_timer);
        s_apply_timer = NULL;
    }
    s_pending_valid = false;
    return scr;
}

static lv_obj_t *make_header(lv_obj_t *scr, const char *title)
{
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), 38);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, COL_HEADER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_left(header, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_right(header, 10, LV_PART_MAIN);
    lv_obj_set_scrollable(header, false);

    lv_obj_t *label = lv_label_create(header);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    return header;
}

static void make_line(lv_obj_t *scr, const char *text, int y,
                      const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 12, y);
}

void ui_show_setup(const char *ap_ssid, const char *url)
{
    lvgl_port_lock(0);

    lv_obj_t *scr = screen_prepare();
    make_header(scr, "Wi-Fi Setup");

    make_line(scr, "1. Join Wi-Fi hotspot:", 48, &lv_font_montserrat_14, COL_TEXT);
    make_line(scr, ap_ssid, 70, &lv_font_montserrat_14, COL_ACCENT);
    make_line(scr, "2. Open in browser:", 98, &lv_font_montserrat_14, COL_TEXT);
    make_line(scr, url, 120, &lv_font_montserrat_14, COL_ACCENT);
    make_line(scr, "3. Fill in the form and save.", 148, &lv_font_montserrat_12, COL_DIM);
    make_line(scr, "No pop-up? Open the URL manually.", 168, &lv_font_montserrat_12, COL_DIM);

    lv_obj_t *note = lv_label_create(scr);
    lv_obj_set_style_text_color(note, COL_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(note, &lv_font_montserrat_10, LV_PART_MAIN);
    lv_label_set_text(note, "hold button 5s = factory reset");
    lv_obj_align(note, LV_ALIGN_BOTTOM_MID, 0, -4);

    lvgl_port_unlock();
}

void ui_show_connecting(void)
{
    lvgl_port_lock(0);

    lv_obj_t *scr = screen_prepare();
    make_header(scr, "Connecting Wi-Fi");

    lv_obj_t *spinner = lv_spinner_create(scr);
    lv_obj_set_size(spinner, 64, 64);
    lv_obj_set_style_arc_color(spinner, COL_SPINNER_BG, LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner, 6, LV_PART_MAIN);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_clickable(spinner, false);
    lv_spinner_set_anim_params(spinner, 1000, 60);

    s_conn_status = lv_label_create(scr);
    lv_obj_set_width(s_conn_status, 216);
    lv_label_set_long_mode(s_conn_status, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(s_conn_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_conn_status, SSID_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_conn_status, COL_TEXT, LV_PART_MAIN);
    lv_label_set_text(s_conn_status, "Connecting...");
    lv_obj_align(s_conn_status, LV_ALIGN_CENTER, 0, 55);

    lvgl_port_unlock();
}

void ui_connecting_status(const char *fmt, ...)
{
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    lvgl_port_lock(0);
    if (s_conn_status != NULL) {
        lv_label_set_text(s_conn_status, buf);
    }
    lvgl_port_unlock();
}

static void gif_frames_init(void)
{
    static bool inited = false;
    if (inited) {
        return;
    }
    inited = true;

    const size_t frame_size = GIF_W * GIF_H * 2;
    for (int s = 0; s < GIF_SETS; s++) {
        for (int i = 0; i < GIF_FRAMES; i++) {
            s_gif_dsc[s][i].header.magic = LV_IMAGE_HEADER_MAGIC;
            s_gif_dsc[s][i].header.cf = LV_COLOR_FORMAT_RGB565;
            s_gif_dsc[s][i].header.w = GIF_W;
            s_gif_dsc[s][i].header.h = GIF_H;
            s_gif_dsc[s][i].header.stride = GIF_W * 2;
            s_gif_dsc[s][i].data_size = frame_size;
            s_gif_dsc[s][i].data = s_gif_bin[s] + (size_t)i * frame_size;
        }
    }

    for (int i = 0; i < 2; i++) {
        s_flash_dsc[i].header.magic = LV_IMAGE_HEADER_MAGIC;
        s_flash_dsc[i].header.cf = LV_COLOR_FORMAT_RGB565;
        s_flash_dsc[i].header.w = FLASH_W;
        s_flash_dsc[i].header.h = FLASH_H;
        s_flash_dsc[i].header.stride = FLASH_W * 2;
        s_flash_dsc[i].data_size = FLASH_W * FLASH_H * 2;
        s_flash_dsc[i].data = s_flash_bin[i];
    }
}

void ui_show_balance(void)
{
    lvgl_port_lock(0);

    lv_obj_t *scr = screen_prepare();
    lv_obj_t *header = make_header(scr, "DeepSeek Balance");

    s_clock = lv_label_create(header);
    lv_obj_set_style_text_font(s_clock, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_clock, COL_HEADER_DIM, LV_PART_MAIN);
    lv_label_set_text(s_clock, "--:--:--");
    lv_obj_align(s_clock, LV_ALIGN_RIGHT_MID, 0, 0);

    s_amount = lv_label_create(scr);
    lv_obj_set_style_text_font(s_amount, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_amount, COL_AMOUNT, LV_PART_MAIN);
    lv_label_set_text(s_amount, "--");
    lv_obj_align(s_amount, LV_ALIGN_CENTER, 0, -55);

    /* mascot animation, centered below the balance */
    gif_frames_init();
    s_gif_set = UI_GIF_NORMAL;
    s_gif_frame = 0;
    s_gif_loops = 0;
    s_gif_img = lv_image_create(scr);
    lv_image_set_src(s_gif_img, &s_gif_dsc[s_gif_set][0]);
    lv_obj_align(s_gif_img, LV_ALIGN_CENTER, 0, 22);
    lv_obj_set_clickable(s_gif_img, false);

    /* small Wi-Fi state icon, top-right just below the header bar */
    lv_obj_t *icon_box = lv_obj_create(scr);
    lv_obj_remove_style_all(icon_box);
    lv_obj_set_size(icon_box, 22, 22);
    lv_obj_align(icon_box, LV_ALIGN_TOP_RIGHT, -8, 42);
    lv_obj_set_scrollable(icon_box, false);
    lv_obj_set_clickable(icon_box, false);

    s_wifi_icon = lv_label_create(icon_box);
    lv_obj_set_style_text_font(s_wifi_icon, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_wifi_icon, COL_WIFI_OFF, LV_PART_MAIN);
    lv_label_set_text(s_wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_center(s_wifi_icon);

    static lv_point_precise_t wifi_slash_pts[2] = { { 2, 18 }, { 20, 4 } };
    s_wifi_slash = lv_line_create(icon_box);
    lv_line_set_points(s_wifi_slash, wifi_slash_pts, 2);
    lv_obj_set_style_line_width(s_wifi_slash, 2, LV_PART_MAIN);
    lv_obj_set_style_line_color(s_wifi_slash, COL_WIFI_SLASH, LV_PART_MAIN);

    s_api = lv_label_create(scr);
    lv_obj_set_width(s_api, 216);
    lv_label_set_long_mode(s_api, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(s_api, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_api, COL_DIM, LV_PART_MAIN);
    lv_label_set_text(s_api, "Waiting for data...");
    lv_obj_align(s_api, LV_ALIGN_BOTTOM_MID, 0, -20);

    s_url = lv_label_create(scr);
    lv_obj_set_style_text_font(s_url, &lv_font_montserrat_10, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_url, COL_DIM, LV_PART_MAIN);
    lv_label_set_text(s_url, "config: waiting network");
    lv_obj_align(s_url, LV_ALIGN_BOTTOM_MID, 0, -3);

    /* full-screen flash overlay (hidden until the balance changes) */
    s_flash = lv_obj_create(scr);
    lv_obj_remove_style_all(s_flash);
    lv_obj_set_size(s_flash, LV_PCT(100), LV_PCT(100));
    lv_obj_align(s_flash, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_flash, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_flash, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(s_flash, false);
    lv_obj_set_clickable(s_flash, false);
    lv_obj_set_hidden(s_flash, true);

    s_flash_img = lv_image_create(s_flash);
    lv_image_set_src(s_flash_img, &s_flash_dsc[0]);
    lv_obj_center(s_flash_img);

    lvgl_port_unlock();
}

static void set_label_text(lv_obj_t **label, const char *text)
{
    lvgl_port_lock(0);
    if (*label != NULL) {
        lv_label_set_text(*label, text);
    }
    lvgl_port_unlock();
}

/* Slide-in transition of the amount label. Runs inside the LVGL task (started
 * from the flash timer), so it must not take the port lock. */
static void amount_anim_exec(void *var, int32_t offset)
{
    lv_obj_t *label = (lv_obj_t *)var;
    lv_obj_set_style_translate_y(label, offset, LV_PART_MAIN);
    int32_t dist = offset < 0 ? -offset : offset;
    lv_obj_set_style_opa(label, (lv_opa_t)(255 - (dist * 255) / AMOUNT_SLIDE_PX), LV_PART_MAIN);
}

static void amount_apply_locked(const char *text, bool animate, bool up)
{
    if (s_amount == NULL) {
        return;
    }
    lv_anim_delete(s_amount, NULL);
    lv_label_set_text(s_amount, text);

    if (!animate) {
        lv_obj_set_style_translate_y(s_amount, 0, LV_PART_MAIN);
        lv_obj_set_style_opa(s_amount, LV_OPA_COVER, LV_PART_MAIN);
        return;
    }

    /* increased: slide up from below; decreased: slide down from above */
    int32_t from = up ? AMOUNT_SLIDE_PX : -AMOUNT_SLIDE_PX;
    lv_obj_set_style_translate_y(s_amount, from, LV_PART_MAIN);
    lv_obj_set_style_opa(s_amount, LV_OPA_TRANSP, LV_PART_MAIN);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_amount);
    lv_anim_set_exec_cb(&anim, amount_anim_exec);
    lv_anim_set_values(&anim, from, 0);
    lv_anim_set_duration(&anim, AMOUNT_ANIM_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);
}

void ui_balance_set_amount(const char *text)
{
    lvgl_port_lock(0);
    amount_apply_locked(text, false, false);
    lvgl_port_unlock();
}

void ui_balance_set_wifi(bool connected)
{
    lvgl_port_lock(0);
    if (s_wifi_icon != NULL) {
        lv_obj_set_style_text_color(s_wifi_icon, connected ? COL_WIFI_OK : COL_WIFI_OFF, LV_PART_MAIN);
    }
    if (s_wifi_slash != NULL) {
        lv_obj_set_hidden(s_wifi_slash, connected);
    }
    lvgl_port_unlock();
}

/* Must be called with the port lock held (or from the LVGL task). */
static void gif_set_locked(ui_gif_t gif)
{
    if (s_gif_img == NULL || gif >= GIF_SETS) {
        return;
    }
    if (gif == UI_GIF_NORMAL && s_gif_set == UI_GIF_NORMAL) {
        return; /* idle animation is already running */
    }
    /* switching sets, or a new trigger of a special scene: play from the start */
    s_gif_set = (uint8_t)gif;
    s_gif_frame = 0;
    s_gif_loops = 0;
    lv_image_set_src(s_gif_img, &s_gif_dsc[s_gif_set][0]);
}

void ui_balance_set_gif(ui_gif_t gif)
{
    lvgl_port_lock(0);
    gif_set_locked(gif);
    lvgl_port_unlock();
}

void ui_set_special_gif_done_cb(void (*cb)(void))
{
    s_gif_done_cb = cb;
}

/* Second stage: applies the deferred amount update (slide transition).
 * Runs inside the LVGL task. */
static void apply_timer_cb(lv_timer_t *timer)
{
    s_apply_timer = NULL; /* the one-shot timer deletes itself afterwards */
    if (s_pending_valid) {
        s_pending_valid = false;
        amount_apply_locked(s_pending_amount, true, s_pending_gif == UI_GIF_CHARGE);
    }
}

/* Zoom of the full-screen still; runs inside the LVGL task. */
static void flash_scale_anim_cb(void *var, int32_t scale)
{
    lv_image_set_scale((lv_obj_t *)var, (uint32_t)scale);
}

/* Schedules the deferred amount update (one-shot timer, LVGL task only). */
static void schedule_apply_locked(void)
{
    if (s_apply_timer != NULL) {
        lv_timer_delete(s_apply_timer);
    }
    s_apply_timer = lv_timer_create(apply_timer_cb, AMOUNT_DELAY_MS, NULL);
    lv_timer_set_repeat_count(s_apply_timer, 1); /* auto-deleted after it runs */
}

/* The shrink-out finished: hide the overlay, start the special scene right
 * away (no idle frame may show in between) and continue with the deferred
 * amount update after a pause. Runs inside the LVGL task. */
static void flash_shrink_done_cb(lv_anim_t *anim)
{
    if (s_flash != NULL) {
        lv_obj_set_hidden(s_flash, true);
    }
    if (s_flash_img != NULL) {
        lv_image_set_scale(s_flash_img, FLASH_SCALE_MAX); /* reset for the next show */
    }
    if (s_pending_valid) {
        gif_set_locked(s_pending_gif);
        schedule_apply_locked();
    }
}

/* Runs inside the LVGL task; starts the shrink-out after GIF_FLASH_MS. */
static void flash_timer_cb(lv_timer_t *timer)
{
    s_flash_timer = NULL; /* the one-shot timer deletes itself afterwards */
    if (s_flash_img == NULL) {
        if (s_pending_valid) {
            gif_set_locked(s_pending_gif);
            schedule_apply_locked();
        }
        return;
    }
    lv_anim_delete(s_flash_img, NULL);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_flash_img);
    lv_anim_set_exec_cb(&anim, flash_scale_anim_cb);
    lv_anim_set_values(&anim, FLASH_SCALE_MAX, FLASH_SCALE_MIN);
    lv_anim_set_duration(&anim, FLASH_SHRINK_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&anim, flash_shrink_done_cb);
    lv_anim_start(&anim);
}

void ui_balance_flash_update(ui_gif_t gif, const char *amount_text)
{
    if (gif != UI_GIF_CHARGE && gif != UI_GIF_EXPEND) {
        return;
    }
    lvgl_port_lock(0);
    if (s_flash != NULL && s_flash_img != NULL && s_amount != NULL && amount_text != NULL) {
        strlcpy(s_pending_amount, amount_text, sizeof(s_pending_amount));
        s_pending_gif = gif;
        s_pending_valid = true;

        /* grow the still in from small to full size */
        lv_anim_delete(s_flash_img, NULL);
        lv_image_set_scale(s_flash_img, FLASH_SCALE_MIN);
        lv_image_set_src(s_flash_img, &s_flash_dsc[gif == UI_GIF_CHARGE ? 0 : 1]);
        lv_obj_move_foreground(s_flash);
        lv_obj_set_hidden(s_flash, false);

        lv_anim_t grow;
        lv_anim_init(&grow);
        lv_anim_set_var(&grow, s_flash_img);
        lv_anim_set_exec_cb(&grow, flash_scale_anim_cb);
        lv_anim_set_values(&grow, FLASH_SCALE_MIN, FLASH_SCALE_MAX);
        lv_anim_set_duration(&grow, FLASH_GROW_MS);
        lv_anim_set_path_cb(&grow, lv_anim_path_ease_out);
        lv_anim_start(&grow);

        if (s_flash_timer != NULL) {
            lv_timer_delete(s_flash_timer);
        }
        if (s_apply_timer != NULL) {
            lv_timer_delete(s_apply_timer);
            s_apply_timer = NULL;
        }
        s_flash_timer = lv_timer_create(flash_timer_cb, GIF_FLASH_MS, NULL);
        lv_timer_set_repeat_count(s_flash_timer, 1); /* auto-deleted after it runs */
    }
    lvgl_port_unlock();
}

void ui_balance_set_api(const char *text)
{
    set_label_text(&s_api, text);
}

void ui_balance_set_url(const char *text)
{
    set_label_text(&s_url, text);
}

/* Runs inside the LVGL task (lv_timer), no port lock needed. */
static void gif_timer_cb(lv_timer_t *timer)
{
    if (s_gif_img == NULL) {
        return;
    }
    if (s_gif_set != UI_GIF_NORMAL && s_gif_loops >= GIF_SPECIAL_LOOPS) {
        return; /* special scene finished; keep the last frame until the next update */
    }

    if (s_gif_frame + 1 >= GIF_FRAMES) {
        if (s_gif_set != UI_GIF_NORMAL) {
            s_gif_loops++;
            if (s_gif_loops >= GIF_SPECIAL_LOOPS) {
                /* played all loops: freeze on the last frame, ask for a re-check */
                if (s_gif_done_cb != NULL) {
                    s_gif_done_cb();
                }
                return;
            }
        }
        s_gif_frame = 0;
    } else {
        s_gif_frame++;
    }
    lv_image_set_src(s_gif_img, &s_gif_dsc[s_gif_set][s_gif_frame]);
}

/* Runs inside the LVGL task (lv_timer), no port lock needed. */
static void clock_timer_cb(lv_timer_t *timer)
{
    if (s_clock == NULL) {
        return;
    }
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    if (tm_now.tm_year + 1900 < 2024) {
        lv_label_set_text(s_clock, "--:--:--");
    } else {
        lv_label_set_text_fmt(s_clock, "%02d:%02d:%02d",
                              tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
    }
}

void ui_init(void)
{
    lv_timer_create(clock_timer_cb, 1000, NULL);
    lv_timer_create(gif_timer_cb, GIF_PERIOD_MS, NULL);
}
