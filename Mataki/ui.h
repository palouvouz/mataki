#pragma once
#include <stdint.h>
#include "esp_lcd_panel_ops.h"

enum class UiState { Booting, WifiConnecting, CameraConnecting, Live, Error };

void ui_init(esp_lcd_panel_handle_t panel);
void ui_set_state(UiState state, const char *detail);
void ui_present_frame(const uint16_t *rgb565, int w, int h);
void ui_set_fps(int fps);
void ui_set_reconnect_cb(void (*cb)(void));
