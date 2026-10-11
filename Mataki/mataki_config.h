#pragma once
// Mataki - non-secret settings. Secrets (Wi-Fi, camera login) live in secrets.h

#define CAMERA_HOST            "192.168.1.47"
#define CAMERA_PORT            80
#define CAMERA_PATH            "/video/mjpg.cgi"

#define FRAME_BUFFER_BYTES     (384 * 1024)  // max size of ONE jpeg frame
#define HTTP_RX_CHUNK_BYTES    4096
#define HTTP_TIMEOUT_MS        5000
#define STALL_TIMEOUT_MS       8000          // no data for this long -> reconnect
#define RECONNECT_DELAY_MS     3000

#define TARGET_FPS_CAP         15            // 0 = no limit
#define JPEG_BIG_ENDIAN        0             // set to 1 if colours look wrong
#define OVERLAY_AUTO_HIDE_MS   5000
