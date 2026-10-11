#include "board.h"

#include <Arduino.h>
#include <Wire.h>
#include <string.h>

#include "esp_lcd_panel_rgb.h"

#define PIN_I2C_SDA    8
#define PIN_I2C_SCL    9
#define PIN_TOUCH_INT  4

// CH422G IO expander: the "register" is the I2C address itself.
#define CH422G_ADDR_MODE  0x24
#define CH422G_ADDR_OUT   0x38
#define EXIO_TP_RST   (1 << 1)
#define EXIO_LCD_BL   (1 << 2)
#define EXIO_LCD_RST  (1 << 3)
#define EXIO_SD_CS    (1 << 4)

static uint8_t s_exio_out = 0;
static uint8_t s_touch_addr = 0;

static bool exio_write(uint8_t value)
{
    s_exio_out = value;
    Wire.beginTransmission(CH422G_ADDR_OUT);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

static bool i2c_probe(uint8_t addr)
{
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

static bool gt_read(uint16_t reg, uint8_t *buf, uint8_t n)
{
    Wire.beginTransmission(s_touch_addr);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)s_touch_addr, (int)n) != n) return false;
    for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
    return true;
}

static void gt_write(uint16_t reg, uint8_t value)
{
    Wire.beginTransmission(s_touch_addr);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    Wire.write(value);
    Wire.endTransmission();
}

static void expander_and_touch_reset(void)
{
    Wire.beginTransmission(CH422G_ADDR_MODE);  // output mode for IO0..IO7
    Wire.write((uint8_t)0x01);
    Wire.endTransmission();

    exio_write(0x00);  // touch + LCD in reset, backlight off

    // GT911 picks its I2C address from the INT level at the end of reset: INT low -> 0x14
    pinMode(PIN_TOUCH_INT, OUTPUT);
    digitalWrite(PIN_TOUCH_INT, LOW);
    delay(10);
    exio_write(EXIO_TP_RST | EXIO_LCD_RST | EXIO_SD_CS);
    delay(10);
    pinMode(PIN_TOUCH_INT, INPUT);
    delay(60);
}

static esp_err_t panel_setup(esp_lcd_panel_handle_t *out)
{
    esp_lcd_rgb_panel_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.clk_src = LCD_CLK_SRC_DEFAULT;
    cfg.timings.pclk_hz = 16 * 1000 * 1000;
    cfg.timings.h_res = LCD_H_RES;
    cfg.timings.v_res = LCD_V_RES;
    cfg.timings.hsync_pulse_width = 4;
    cfg.timings.hsync_back_porch = 8;
    cfg.timings.hsync_front_porch = 8;
    cfg.timings.vsync_pulse_width = 4;
    cfg.timings.vsync_back_porch = 8;
    cfg.timings.vsync_front_porch = 8;
    cfg.timings.flags.pclk_active_neg = 1;
    cfg.data_width = 16;
    cfg.bits_per_pixel = 16;
    cfg.num_fbs = 1;
    cfg.bounce_buffer_size_px = LCD_H_RES * 10;
    cfg.psram_trans_align = 64;
    cfg.hsync_gpio_num = 46;
    cfg.vsync_gpio_num = 3;
    cfg.de_gpio_num = 5;
    cfg.pclk_gpio_num = 7;
    cfg.disp_gpio_num = -1;
    // B3..B7, G2..G7, R3..R7
    const int data[16] = {14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40};
    for (int i = 0; i < 16; i++) {
        cfg.data_gpio_nums[i] = data[i];
    }
    cfg.flags.fb_in_psram = 1;

    esp_err_t r = esp_lcd_new_rgb_panel(&cfg, out);
    if (r != ESP_OK) return r;
    r = esp_lcd_panel_reset(*out);
    if (r != ESP_OK) return r;
    return esp_lcd_panel_init(*out);
}

esp_err_t board_init(esp_lcd_panel_handle_t *panel)
{
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
    expander_and_touch_reset();

    if (i2c_probe(0x14)) {
        s_touch_addr = 0x14;
    } else if (i2c_probe(0x5D)) {
        s_touch_addr = 0x5D;
    } else {
        Serial.println("[board] GT911 touch not found - running without touch");
    }
    Serial.printf("[board] touch address: 0x%02X\n", s_touch_addr);

    esp_err_t r = panel_setup(panel);
    if (r != ESP_OK) {
        Serial.printf("[board] RGB panel init failed (%d). Check Tools > PSRAM = OPI PSRAM.\n", (int)r);
        return r;
    }

    exio_write(s_exio_out | EXIO_LCD_BL);  // backlight on
    return ESP_OK;
}

bool board_touch_read(uint16_t *x, uint16_t *y)
{
    static bool last_pressed = false;
    static uint16_t last_x = 0, last_y = 0;

    if (s_touch_addr) {
        uint8_t status = 0;
        if (gt_read(0x814E, &status, 1) && (status & 0x80)) {  // fresh data
            const uint8_t points = status & 0x0F;
            last_pressed = false;
            uint8_t d[4];
            if (points > 0 && points <= 5 && gt_read(0x8150, d, 4)) {
                const uint16_t px = d[0] | (d[1] << 8);
                const uint16_t py = d[2] | (d[3] << 8);
                last_x = px < LCD_H_RES ? px : LCD_H_RES - 1;
                last_y = py < LCD_V_RES ? py : LCD_V_RES - 1;
                last_pressed = true;
            }
            gt_write(0x814E, 0x00);
        }
    }
    *x = last_x;
    *y = last_y;
    return last_pressed;
}
