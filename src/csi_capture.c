#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "config.h"
#include "csi_capture.h"

static const char *TAG = "csi";

// Shared between WiFi-task callback (writer) and send task (reader).
// portMUX spinlock gives safe dual-core access with minimal hold time.
static struct {
    float   amp[CSI_N_CH];
    int16_t rssi;
    bool    fresh;
} s_latest = { .rssi = -70, .fresh = false };

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static void csi_rx_cb(void *ctx, wifi_csi_info_t *info)
{
    if (!info || !info->buf || info->len < 2) return;

    // ASSUMPTION: buf is int8 I/Q pairs in sub-carrier index order.
    //
    // HT20 LLTF layout (802.11-2016, §21.3.7.5):
    //   52 non-null sub-carriers: -26..−1, +1..+26 (DC and guard bands = 0).
    //   ESP-IDF packs them as 64 pairs (indices −32..+31 mapped to buf[0..127]).
    //   Null sub-carriers produce I=Q=0 → amplitude=0, which is correct.
    //
    // MUST VERIFY on target hardware:
    //   1. Print info->len at boot; expect 128 (64 × 2 bytes).
    //   2. Check which buf[] indices carry non-zero amplitude.
    //   3. If fewer than 64 pairs arrive (info->len < 128), missing channels
    //      are zero-padded below — update mapping comment after validation.

    int n_pairs = info->len / 2;
    int n       = (n_pairs < CSI_N_CH) ? n_pairs : CSI_N_CH;

    float amp[CSI_N_CH] = {0.0f};
    for (int k = 0; k < n; k++) {
        float I = (float)info->buf[2 * k];
        float Q = (float)info->buf[2 * k + 1];
        amp[k] = sqrtf(I * I + Q * Q);
    }
    // amp[n..CSI_N_CH-1] remain 0 (zero-pad when n_pairs < 64)

    int16_t rssi = (int16_t)info->rx_ctrl.rssi;

    portENTER_CRITICAL(&s_mux);
    memcpy(s_latest.amp, amp, sizeof(amp));
    s_latest.rssi  = rssi;
    s_latest.fresh = true;
    portEXIT_CRITICAL(&s_mux);
}

esp_err_t csi_capture_init(void)
{
    wifi_csi_config_t cfg = {
        .lltf_en           = true,
        .htltf_en          = false,   // LLTF only — 64 sub-carriers
        .stbc_htltf2_en    = false,
        .ltf_merge_en      = true,
        .channel_filter_en = false,   // raw per-sub-carrier (no adjacent smoothing)
        .manu_scale        = false,
        .shift             = 0,
    };
    ESP_ERROR_CHECK(esp_wifi_set_csi_config(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(csi_rx_cb, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_csi(true));

    ESP_LOGI(TAG, "CSI capture ready — %d channels, HT20 LLTF", CSI_N_CH);
    return ESP_OK;
}

bool csi_capture_get_frame(float *amp_out, int16_t *rssi_out)
{
    portENTER_CRITICAL(&s_mux);
    bool fresh = s_latest.fresh;
    memcpy(amp_out, s_latest.amp, CSI_N_CH * sizeof(float));
    if (rssi_out) *rssi_out = s_latest.rssi;
    s_latest.fresh = false;
    portEXIT_CRITICAL(&s_mux);
    return fresh;
}
