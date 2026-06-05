#pragma once
#include "esp_err.h"

// Allocate per-channel per-band IIR state (prefers PSRAM).
// Must be called once before biquad_process_*.
esp_err_t biquad_init(void);

// Process one frame (CSI_N_CH samples) through the breathing-band IIR cascade.
// State persists across calls; filter builds temporal frequency response over time.
void biquad_process_resp (const float *in, float *out);

// Process one frame through the heart-band IIR cascade.
void biquad_process_heart(const float *in, float *out);
