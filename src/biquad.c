#include <stdlib.h>
#include <assert.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "config.h"
#include "biquad_coeffs.h"
#include "biquad.h"

static const char *TAG = "biquad";

// Guard: biquad_coeffs.h (gen_sos.py) must agree with config.h constant.
static_assert(SOS_N_SECTIONS == BIQUAD_N_SECTIONS,
    "SOS_N_SECTIONS != BIQUAD_N_SECTIONS: re-run tools/gen_sos.py with order=4");

// DF-II Transposed delay state for one second-order section
typedef struct { float w[2]; } SecState;
// Per-channel state (cascade of SOS_N_SECTIONS biquads)
typedef struct { SecState s[SOS_N_SECTIONS]; } ChState;
// All 64 channels for one filter band — allocated in PSRAM
typedef struct { ChState ch[CSI_N_CH]; } BandState;

static BandState *s_resp;
static BandState *s_heart;

esp_err_t biquad_init(void)
{
    // 64ch × 4sec × 2 delays × 4 bytes = 2048 B per band; prefer PSRAM
    s_resp  = heap_caps_calloc(1, sizeof(BandState), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_heart = heap_caps_calloc(1, sizeof(BandState), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (!s_resp) {
        ESP_LOGW(TAG, "PSRAM unavailable for resp state, falling back to internal RAM");
        s_resp = calloc(1, sizeof(BandState));
    }
    if (!s_heart) {
        ESP_LOGW(TAG, "PSRAM unavailable for heart state, falling back to internal RAM");
        s_heart = calloc(1, sizeof(BandState));
    }
    if (!s_resp || !s_heart) {
        ESP_LOGE(TAG, "OOM: cannot allocate biquad state");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Biquad state: 2 × %u B  (%d ch × %d sections × 2 delays)",
             (unsigned)sizeof(BandState), CSI_N_CH, SOS_N_SECTIONS);
    return ESP_OK;
}

// DF-II Transposed biquad — one sample, one section.
// sos[]: [b0, b1, b2, 1.0, a1, a2]   (scipy 'sos' row format, a0 normalised)
// y       = b0·x  + w[0]
// w[0]    = b1·x  − a1·y  + w[1]
// w[1]    = b2·x  − a2·y
static inline float biquad_tick(const float sos[6], float *w, float x)
{
    float y = sos[0] * x + w[0];
    w[0]    = sos[1] * x - sos[4] * y + w[1];
    w[1]    = sos[2] * x - sos[5] * y;
    return y;
}

static void process(const float sos[SOS_N_SECTIONS][6],
                    BandState *st,
                    const float *in, float *out)
{
    for (int ch = 0; ch < CSI_N_CH; ch++) {
        float x = in[ch];
        for (int s = 0; s < SOS_N_SECTIONS; s++) {
            x = biquad_tick(sos[s], st->ch[ch].s[s].w, x);
        }
        out[ch] = x;
    }
}

void biquad_process_resp (const float *in, float *out) { process(g_sos_resp,  s_resp,  in, out); }
void biquad_process_heart(const float *in, float *out) { process(g_sos_heart, s_heart, in, out); }
