#pragma once
#include <stdint.h>

void wifi_init(void);
bool wifi_wait_connected(uint32_t timeout_ms);
bool wifi_is_connected(void);
