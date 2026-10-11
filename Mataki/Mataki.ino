// Mataki - IP camera monitor for the Waveshare ESP32-S3 Touch LCD 4.3"
// Arduino IDE sketch. See README.md and docs/BUILD_APK.md.

#include "board.h"
#include "mjpeg_client.h"
#include "ui.h"
#include "wifi_link.h"

void setup()
{
    Serial.begin(115200);

    esp_lcd_panel_handle_t panel = NULL;
    if (board_init(&panel) != ESP_OK) {
        Serial.println("[main] board init failed");
        return;
    }

    ui_init(panel);
    ui_set_state(UiState::Booting, "Starting up...");
    ui_set_reconnect_cb(mjpeg_request_reconnect);

    wifi_init();
    mjpeg_start();
}

void loop()
{
    delay(1000);  // all work happens in FreeRTOS tasks
}
