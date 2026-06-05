#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// Configure WiFi CSI, register callback, enable collection.
// Call after WiFi STA is connected.
esp_err_t csi_capture_init(void);

// Copy the latest amplitude frame into amp_out[CSI_N_CH].
// Returns true if data is fresh since the last call (false → reusing previous frame).
// rssi_out: last RSSI in dBm, may be NULL.
bool csi_capture_get_frame(float *amp_out, int16_t *rssi_out);
