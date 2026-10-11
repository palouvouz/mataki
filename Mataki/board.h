#pragma once
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

#define LCD_H_RES 800
#define LCD_V_RES 480

// Initialises I2C, the CH422G IO expander, the RGB panel, the GT911 touch
// controller and switches the backlight on.
esp_err_t board_init(esp_lcd_panel_handle_t *panel);

// Returns true while a finger is on the screen (coordinates in panel pixels).
bool board_touch_read(uint16_t *x, uint16_t *y);
