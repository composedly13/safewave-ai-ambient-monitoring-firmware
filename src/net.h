#pragma once
#include <stddef.h>
#include "esp_err.h"

// Create UDP socket and resolve gateway IP.
// Call after WiFi is connected and IP is assigned.
esp_err_t net_init(void);

// Send len bytes to RPi (TARGET_IP:TARGET_PORT). Non-blocking.
void net_udp_send(const void *data, size_t len);

// Spawn the 100 Hz FreeRTOS task (CSI read → filter → pack → send).
void net_send_task_start(void);
