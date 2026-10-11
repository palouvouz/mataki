#include "wifi_link.h"

#include <Arduino.h>
#include <WiFi.h>

#if !__has_include("secrets.h")
#error "secrets.h is missing. Copy secrets.example.h to secrets.h and fill it in."
#endif
#include "secrets.h"

void wifi_init(void)
{
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);  // full speed for video
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

bool wifi_is_connected(void)
{
    return WiFi.status() == WL_CONNECTED;
}

bool wifi_wait_connected(uint32_t timeout_ms)
{
    const uint32_t start = millis();
    while (!wifi_is_connected()) {
        if (millis() - start >= timeout_ms) return false;
        delay(100);
    }
    return true;
}
