// Network (MJPEG over HTTP) + JPEG decode pipeline.
//
//  net task (core 0)                         decode task (core 1)
//  TCP read -> find FFD8..FFD9 -> frame ---> JPEGDEC -> ui_present_frame
//  Three frame buffers; only the newest frame is kept (old ones are dropped).

#include "mjpeg_client.h"

#include <Arduino.h>
#include <JPEGDEC.h>
#include <WiFi.h>
#include <atomic>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "board.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mataki_config.h"
#include "mbedtls/base64.h"
#include "ui.h"
#include "wifi_link.h"

#if !__has_include("secrets.h")
#error "secrets.h is missing. Copy secrets.example.h to secrets.h and fill it in."
#endif
#include "secrets.h"

struct FrameRef {
    int idx;
    size_t len;
};

struct Assembler {
    int cur;
    size_t len;
    bool in_frame;
    uint8_t prev;
};

static uint8_t *s_bufs[3];
static QueueHandle_t s_ready_q;  // FrameRef, length 1 (newest frame only)
static QueueHandle_t s_free_q;   // int, buffer indexes
static std::atomic<bool> s_reconnect{false};
static std::atomic<bool> s_need_live_state{true};

void mjpeg_request_reconnect(void) { s_reconnect = true; }

// ------------------------------------------------------------------ network
static bool make_basic_auth(char *out, size_t out_size)
{
    char raw[128];
    int n = snprintf(raw, sizeof(raw), "%s:%s", CAMERA_USER, CAMERA_PASSWORD);
    if (n <= 0 || n >= (int)sizeof(raw)) return false;
    unsigned char b64[200];
    size_t olen = 0;
    if (mbedtls_base64_encode(b64, sizeof(b64), &olen, (const unsigned char *)raw, n) != 0) return false;
    snprintf(out, out_size, "Basic %.*s", (int)olen, (const char *)b64);
    memset(raw, 0, sizeof(raw));
    return true;
}

static bool read_line(WiFiClient &c, char *buf, size_t cap, uint32_t timeout_ms)
{
    size_t n = 0;
    uint32_t t = millis();
    while (millis() - t < timeout_ms) {
        if (!c.connected() && !c.available()) return false;
        if (c.available()) {
            int ch = c.read();
            if (ch < 0) continue;
            t = millis();
            if (ch == '\n') {
                buf[n] = 0;
                return true;
            }
            if (ch != '\r' && n < cap - 1) buf[n++] = (char)ch;
        } else {
            delay(2);
        }
    }
    return false;
}

static void deliver(Assembler &a)
{
    FrameRef old;
    if (xQueueReceive(s_ready_q, &old, 0) == pdTRUE) {
        xQueueSend(s_free_q, &old.idx, 0);  // drop the stale frame
    }
    FrameRef f = {a.cur, a.len};
    xQueueSend(s_ready_q, &f, 0);
    xQueueReceive(s_free_q, &a.cur, portMAX_DELAY);  // always one available
    a.len = 0;
}

static void feed(Assembler &a, const uint8_t *p, int n)
{
    for (int i = 0; i < n; i++) {
        const uint8_t b = p[i];
        if (!a.in_frame) {
            if (a.prev == 0xFF && b == 0xD8) {
                a.in_frame = true;
                s_bufs[a.cur][0] = 0xFF;
                s_bufs[a.cur][1] = 0xD8;
                a.len = 2;
            }
        } else {
            if (a.len >= FRAME_BUFFER_BYTES) {
                Serial.println("[mjpeg] frame larger than FRAME_BUFFER_BYTES, dropped");
                a.in_frame = false;
                a.len = 0;
            } else {
                s_bufs[a.cur][a.len++] = b;
                if (a.prev == 0xFF && b == 0xD9) {
                    a.in_frame = false;
                    deliver(a);
                }
            }
        }
        a.prev = b;
    }
}

static void run_stream(void)
{
    char auth[256];
    if (!make_basic_auth(auth, sizeof(auth))) {
        ui_set_state(UiState::Error, "Camera login is too long. Check secrets.h.");
        return;
    }

    WiFiClient client;
    if (!client.connect(CAMERA_HOST, CAMERA_PORT, HTTP_TIMEOUT_MS)) {
        ui_set_state(UiState::Error, "Camera not reachable at " CAMERA_HOST ". Retrying...");
        memset(auth, 0, sizeof(auth));
        return;
    }
    client.setNoDelay(true);

    client.printf("GET %s HTTP/1.1\r\nHost: %s\r\nAuthorization: %s\r\nAccept: */*\r\nConnection: keep-alive\r\n\r\n",
                  CAMERA_PATH, CAMERA_HOST, auth);
    memset(auth, 0, sizeof(auth));

    char line[160];
    int status = 0;
    bool chunked = false;
    if (!read_line(client, line, sizeof(line), HTTP_TIMEOUT_MS) || sscanf(line, "HTTP/%*d.%*d %d", &status) != 1) {
        ui_set_state(UiState::Error, "Camera sent no valid answer.");
        client.stop();
        return;
    }
    while (read_line(client, line, sizeof(line), HTTP_TIMEOUT_MS)) {
        if (line[0] == 0) break;  // end of headers
        if (strcasestr(line, "transfer-encoding") && strcasestr(line, "chunked")) chunked = true;
    }

    if (status != 200) {
        char msg[96];
        if (status == 401) {
            snprintf(msg, sizeof(msg), "Camera rejected the login (401). Check secrets.h.");
        } else {
            snprintf(msg, sizeof(msg), "Camera answered HTTP %d.", status);
        }
        ui_set_state(UiState::Error, msg);
        client.stop();
        return;
    }
    if (chunked) {
        ui_set_state(UiState::Error, "Chunked stream not supported by this camera mode.");
        client.stop();
        return;
    }

    Serial.println("[mjpeg] stream open");
    s_need_live_state = true;

    uint8_t *chunk = (uint8_t *)heap_caps_malloc(HTTP_RX_CHUNK_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!chunk) {
        client.stop();
        return;
    }

    Assembler a = {0, 0, false, 0};
    xQueueReceive(s_free_q, &a.cur, portMAX_DELAY);  // take a buffer for assembling

    uint32_t last_data = millis();
    while (!s_reconnect && (client.connected() || client.available())) {
        int avail = client.available();
        if (avail > 0) {
            int n = client.read(chunk, avail < HTTP_RX_CHUNK_BYTES ? avail : HTTP_RX_CHUNK_BYTES);
            if (n > 0) {
                last_data = millis();
                feed(a, chunk, n);
            }
        } else {
            if (millis() - last_data > STALL_TIMEOUT_MS) {
                Serial.println("[mjpeg] stream stalled");
                break;
            }
            delay(2);
        }
    }

    xQueueSend(s_free_q, &a.cur, 0);  // give the buffer back
    free(chunk);
    client.stop();
}

static void net_task(void *arg)
{
    for (;;) {
        if (!wifi_is_connected()) {
            ui_set_state(UiState::WifiConnecting, "Waiting for the router...");
            while (!wifi_wait_connected(1000)) {
            }
        }
        s_reconnect = false;
        ui_set_state(UiState::CameraConnecting, CAMERA_HOST);
        run_stream();
        if (!s_reconnect) {
            if (wifi_is_connected()) {
                ui_set_state(UiState::Error, "Stream lost. Retrying...");
            }
            vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
        }
    }
}

// ------------------------------------------------------------------ decode
static JPEGDEC s_jpeg;
static uint16_t *s_out;
static int s_out_w, s_out_h;

static int draw_cb(JPEGDRAW *d)
{
    if (d->x < 0 || d->y < 0 || d->x >= s_out_w || d->y >= s_out_h) return 1;
    int w = d->iWidth;
    int h = d->iHeight;
    if (d->x + w > s_out_w) w = s_out_w - d->x;  // clip MCU overhang at the edges
    if (d->y + h > s_out_h) h = s_out_h - d->y;
    for (int row = 0; row < h; row++) {
        memcpy(&s_out[(d->y + row) * s_out_w + d->x], d->pPixels + row * d->iWidth, w * sizeof(uint16_t));
    }
    return 1;
}

static bool decode_frame(uint8_t *data, size_t len, int *ow, int *oh)
{
    if (!s_jpeg.openRAM(data, (int)len, draw_cb)) return false;
    const int w = s_jpeg.getWidth();
    const int h = s_jpeg.getHeight();

    int option = 0;
    int sc = 1;
    if (w > LCD_H_RES || h > LCD_V_RES) {
        if ((w + 1) / 2 <= LCD_H_RES && (h + 1) / 2 <= LCD_V_RES) { option = JPEG_SCALE_HALF; sc = 2; }
        else if ((w + 3) / 4 <= LCD_H_RES && (h + 3) / 4 <= LCD_V_RES) { option = JPEG_SCALE_QUARTER; sc = 4; }
        else if ((w + 7) / 8 <= LCD_H_RES && (h + 7) / 8 <= LCD_V_RES) { option = JPEG_SCALE_EIGHTH; sc = 8; }
        else { s_jpeg.close(); return false; }
    }
    s_out_w = (w + sc - 1) / sc;
    s_out_h = (h + sc - 1) / sc;
#if JPEG_BIG_ENDIAN
    s_jpeg.setPixelType(RGB565_BIG_ENDIAN);
#else
    s_jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
#endif
    const int ok = s_jpeg.decode(0, 0, option);
    s_jpeg.close();
    *ow = s_out_w;
    *oh = s_out_h;
    return ok == 1;
}

static void decode_task(void *arg)
{
    s_out = (uint16_t *)heap_caps_malloc(LCD_H_RES * LCD_V_RES * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_out) {
        ui_set_state(UiState::Error, "Out of memory (decode buffer).");
        vTaskDelete(NULL);
        return;
    }

    int frames = 0;
    int64_t second_start = millis();
    uint32_t last_decode = 0;

    for (;;) {
        FrameRef f;
        xQueueReceive(s_ready_q, &f, portMAX_DELAY);

        const uint32_t now = millis();
#if TARGET_FPS_CAP > 0
        const bool skip = (now - last_decode) < (1000u / TARGET_FPS_CAP);
#else
        const bool skip = false;
#endif
        if (!skip) {
            last_decode = now;
            int w = 0, h = 0;
            if (decode_frame(s_bufs[f.idx], f.len, &w, &h)) {
                ui_present_frame(s_out, w, h);
                if (s_need_live_state.exchange(false)) {
                    ui_set_state(UiState::Live, "");
                }
                frames++;
            } else {
                Serial.println("[mjpeg] JPEG decode failed, frame skipped");
            }
        }
        xQueueSend(s_free_q, &f.idx, 0);

        if (millis() - second_start >= 1000) {
            ui_set_fps(frames);
            frames = 0;
            second_start = millis();
        }
    }
}

// ------------------------------------------------------------------ start
void mjpeg_start(void)
{
    for (int i = 0; i < 3; i++) {
        s_bufs[i] = (uint8_t *)heap_caps_malloc(FRAME_BUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_bufs[i]) {
            ui_set_state(UiState::Error, "Out of memory. Set Tools > PSRAM > OPI PSRAM.");
            return;
        }
    }
    s_ready_q = xQueueCreate(1, sizeof(FrameRef));
    s_free_q = xQueueCreate(3, sizeof(int));
    for (int i = 0; i < 3; i++) {
        xQueueSend(s_free_q, &i, 0);
    }
    xTaskCreatePinnedToCore(net_task, "net", 8192, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(decode_task, "decode", 12288, NULL, 3, NULL, 1);
}
