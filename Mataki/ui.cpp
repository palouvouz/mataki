#include "ui.h"

#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "mataki_config.h"
#include <Arduino.h>
#include "esp_heap_caps.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <lvgl.h>



// Palette (dark, high contrast, one blue accent)
#define COL_BG_TOP     0x000000
#define COL_BG_BOTTOM  0x0A1A3A
#define COL_CARD       0x191C1F
#define COL_GLASS      0x1B1F24
#define COL_TEXT_DIM   0x8D969E
#define COL_BLUE       0x0666EB
#define COL_GREEN      0x00A87E
#define COL_RED        0xE23B4A

static esp_lcd_panel_handle_t s_panel;
static SemaphoreHandle_t s_mutex;
static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;
static lv_indev_drv_t s_indev_drv;

static uint16_t *s_canvas_buf;
static lv_obj_t *s_canvas, *s_card, *s_title, *s_detail, *s_card_dot;
static lv_obj_t *s_overlay, *s_overlay_dot, *s_fps_label;
static bool s_has_frame = false;
static bool s_overlay_visible = false;
static int s_last_w = 0, s_last_h = 0;
static uint32_t s_overlay_shown_at = 0;
static void (*s_reconnect_cb)(void) = nullptr;

static inline void lock(void) { xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY); }
static inline void unlock(void) { xSemaphoreGiveRecursive(s_mutex); }

// ---------------------------------------------------------------- LVGL glue
static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *c)
{
    esp_lcd_panel_draw_bitmap(s_panel, a->x1, a->y1, a->x2 + 1, a->y2 + 1, c);
    lv_disp_flush_ready(drv);
}

static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    uint16_t x = 0, y = 0;
    if (board_touch_read(&x, &y)) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

static void tick_cb(void *arg) { lv_tick_inc(2); }

static void lvgl_task(void *arg)
{
    for (;;) {
        lock();
        uint32_t wait = lv_timer_handler();
        unlock();
        if (wait < 5) wait = 5;
        if (wait > 50) wait = 50;
        vTaskDelay(pdMS_TO_TICKS(wait));
    }
}

// ---------------------------------------------------------------- widgets
static lv_obj_t *make_dot(lv_obj_t *parent, int d, uint32_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, d, d);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    return o;
}

static void style_glass(lv_obj_t *o, lv_color_t fill, lv_opa_t fill_opa)
{
    lv_obj_set_style_bg_color(o, fill, 0);
    lv_obj_set_style_bg_opa(o, fill_opa, 0);
    lv_obj_set_style_border_color(o, lv_color_white(), 0);
    lv_obj_set_style_border_opa(o, LV_OPA_30, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
}

static void set_overlay(bool show)
{
    s_overlay_visible = show;
    if (show) {
        s_overlay_shown_at = lv_tick_get();
        lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_fade_in(s_overlay, 200, 0);
    } else {
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static void on_video_click(lv_event_t *e) { set_overlay(!s_overlay_visible); }

static void on_reconnect_click(lv_event_t *e)
{
    s_overlay_shown_at = lv_tick_get();
    if (s_reconnect_cb) s_reconnect_cb();
}

static void overlay_tick_cb(lv_timer_t *t)
{
    if (s_overlay_visible && lv_tick_elaps(s_overlay_shown_at) > OVERLAY_AUTO_HIDE_MS) {
        set_overlay(false);
    }
}

static void build_ui(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG_TOP), 0);
    lv_obj_set_style_bg_grad_color(scr, lv_color_hex(COL_BG_BOTTOM), 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
    lv_obj_add_event_cb(scr, on_video_click, LV_EVENT_CLICKED, NULL);

    // Video layer (hidden until the first frame arrives)
    s_canvas = lv_canvas_create(scr);
    lv_canvas_set_buffer(s_canvas, s_canvas_buf, LCD_H_RES, LCD_V_RES, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(s_canvas, 0, 0);
    lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_canvas, on_video_click, LV_EVENT_CLICKED, NULL);

    // Status card
    s_card = lv_obj_create(scr);
    lv_obj_remove_style_all(s_card);
    lv_obj_set_size(s_card, 460, LV_SIZE_CONTENT);
    lv_obj_center(s_card);
    lv_obj_set_style_bg_opa(s_card, LV_OPA_90, 0);
    lv_obj_set_style_bg_color(s_card, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_radius(s_card, 28, 0);
    lv_obj_set_style_border_width(s_card, 1, 0);
    lv_obj_set_style_border_color(s_card, lv_color_white(), 0);
    lv_obj_set_style_border_opa(s_card, LV_OPA_20, 0);
    lv_obj_set_style_pad_all(s_card, 28, 0);
    lv_obj_set_style_pad_row(s_card, 10, 0);
    lv_obj_set_flex_flow(s_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(s_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *brand = lv_obj_create(s_card);
    lv_obj_remove_style_all(brand);
    lv_obj_set_size(brand, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(brand, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brand, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(brand, 10, 0);
    s_card_dot = make_dot(brand, 10, COL_BLUE);
    lv_obj_t *brand_label = lv_label_create(brand);
    lv_label_set_text(brand_label, "MATAKI");
    lv_obj_set_style_text_font(brand_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(brand_label, lv_color_hex(COL_TEXT_DIM), 0);
    lv_obj_set_style_text_letter_space(brand_label, 3, 0);

    s_title = lv_label_create(s_card);
    lv_label_set_text(s_title, "Starting");
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(s_title, lv_color_white(), 0);

    s_detail = lv_label_create(s_card);
    lv_label_set_long_mode(s_detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_detail, lv_pct(100));
    lv_label_set_text(s_detail, "");
    lv_obj_set_style_text_font(s_detail, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_detail, lv_color_hex(COL_TEXT_DIM), 0);

    // Glass control bar (shown on tap)
    s_overlay = lv_obj_create(scr);
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, 760, 60);
    lv_obj_align(s_overlay, LV_ALIGN_TOP_MID, 0, 16);
    style_glass(s_overlay, lv_color_hex(COL_GLASS), LV_OPA_60);
    lv_obj_set_style_pad_left(s_overlay, 24, 0);
    lv_obj_set_style_pad_right(s_overlay, 8, 0);
    lv_obj_set_flex_flow(s_overlay, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_overlay, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *left = lv_obj_create(s_overlay);
    lv_obj_remove_style_all(left);
    lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left, 12, 0);
    s_overlay_dot = make_dot(left, 10, COL_BLUE);
    s_fps_label = lv_label_create(left);
    lv_label_set_text(s_fps_label, "LIVE  0 fps");
    lv_obj_set_style_text_font(s_fps_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_fps_label, lv_color_white(), 0);
    lv_obj_set_style_text_letter_space(s_fps_label, 1, 0);

    lv_obj_t *btn = lv_btn_create(s_overlay);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 150, 44);
    style_glass(btn, lv_color_white(), LV_OPA_20);
    lv_obj_set_style_bg_opa(btn, LV_OPA_40, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, on_reconnect_click, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Reconnect");
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(btn_label, lv_color_white(), 0);
    lv_obj_set_style_text_letter_space(btn_label, 1, 0);
    lv_obj_center(btn_label);

    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_timer_create(overlay_tick_cb, 500, NULL);
}

// ---------------------------------------------------------------- public API
void ui_init(esp_lcd_panel_handle_t panel)
{
    s_panel = panel;
    s_mutex = xSemaphoreCreateRecursiveMutex();

    lv_init();

    const size_t px = LCD_H_RES * 40;
    lv_color_t *buf = (lv_color_t *)heap_caps_malloc(px * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_canvas_buf = (uint16_t *)heap_caps_calloc(LCD_H_RES * LCD_V_RES, sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf || !s_canvas_buf) {
        Serial.println("[ui] out of memory: set Tools > PSRAM > OPI PSRAM");
        abort();
    }

    lv_disp_draw_buf_init(&s_draw_buf, buf, NULL, px);
    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = LCD_H_RES;
    s_disp_drv.ver_res = LCD_V_RES;
    s_disp_drv.flush_cb = flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&s_disp_drv);

    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type = LV_INDEV_TYPE_POINTER;
    s_indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&s_indev_drv);

    esp_timer_create_args_t ta;
    memset(&ta, 0, sizeof(ta));
    ta.callback = tick_cb;
    ta.name = "lv_tick";
    esp_timer_handle_t th;
    ESP_ERROR_CHECK(esp_timer_create(&ta, &th));
    ESP_ERROR_CHECK(esp_timer_start_periodic(th, 2000));

    build_ui();
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, NULL, 4, NULL, 1);
}

void ui_set_state(UiState state, const char *detail)
{
    const char *title = "";
    uint32_t color = COL_BLUE;
    switch (state) {
        case UiState::Booting:          title = "Starting";             color = COL_BLUE;  break;
        case UiState::WifiConnecting:   title = "Joining Wi-Fi";        color = COL_BLUE;  break;
        case UiState::CameraConnecting: title = "Connecting to camera"; color = COL_BLUE;  break;
        case UiState::Live:             title = "Live";                 color = COL_GREEN; break;
        case UiState::Error:            title = "Connection problem";   color = COL_RED;   break;
    }

    lock();
    lv_label_set_text(s_title, title);
    lv_label_set_text(s_detail, detail ? detail : "");
    lv_obj_set_style_bg_color(s_card_dot, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(s_overlay_dot, lv_color_hex(color), 0);
    if (state == UiState::Live) {
        lv_obj_add_flag(s_card, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_card, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_fps_label, "OFFLINE");
    }
    unlock();
}

void ui_present_frame(const uint16_t *src, int w, int h)
{
    if (!s_canvas_buf || w <= 0 || h <= 0 || w > LCD_H_RES || h > LCD_V_RES) return;
    const int x0 = (LCD_H_RES - w) / 2;
    const int y0 = (LCD_V_RES - h) / 2;

    lock();
    if (w != s_last_w || h != s_last_h) {
        memset(s_canvas_buf, 0, LCD_H_RES * LCD_V_RES * sizeof(uint16_t));  // clear old bars
        s_last_w = w;
        s_last_h = h;
    }
    for (int y = 0; y < h; y++) {
        memcpy(&s_canvas_buf[(y0 + y) * LCD_H_RES + x0], src + y * w, w * sizeof(uint16_t));
    }
    if (!s_has_frame) {
        s_has_frame = true;
        lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_invalidate(s_canvas);
    unlock();
}

void ui_set_fps(int fps)
{
    lock();
    lv_label_set_text_fmt(s_fps_label, "LIVE  %d fps", fps);
    unlock();
}

void ui_set_reconnect_cb(void (*cb)(void)) { s_reconnect_cb = cb; }
