#pragma once

// ═══════════════════════════════════════════════════════════════════════
// SafeWave-AI CSI Node — all compile-time constants in one place.
// Edit here; do not scatter #defines across other files.
// ═══════════════════════════════════════════════════════════════════════

// ─── Network ────────────────────────────────────────────────────────────
// ipTIME AX2004T subnet 192.168.1.0/24, gateway 192.168.1.1 (auto-derived).
#define WIFI_SSID           "YOUR_2G_SSID"    // TODO: ipTIME 2.4GHz SSID (ESP32-S3 is 2.4GHz only)
#define WIFI_PASSWORD       "YOUR_PASS"       // TODO: set before flash (do NOT commit the real value)
#define TARGET_IP           "192.168.1.11"    // rohjinsan (노진산) — receiver host
#define TARGET_PORT         5005
#define DUMMY_TRIGGER_PORT  5006              // dummy UDP port on gateway (192.168.1.1)

// ─── Node identity ──────────────────────────────────────────────────────
// Overridable at build time via -DNODE_ID=n (PlatformIO env node1..node5,
// see platformio.ini). Falls back to 1 when not injected.
#ifndef NODE_ID
#define NODE_ID             1
#endif

// ─── Sampling ───────────────────────────────────────────────────────────
// CRITICAL: must equal RPi CSI_FS env-var AND M2 ONNX training fs.
//           Mismatch corrupts 0.1–3 Hz vital extraction.
#define CSI_FS              100               // Hz
#define CSI_SEND_PERIOD_MS  (1000 / CSI_FS)  // 10 ms

// ─── CSI sub-carriers ───────────────────────────────────────────────────
// ASSUMPTION: HT20 LLTF on ESP32-S3 fills buf with 64 int8 I/Q pairs.
// Actual valid-carrier layout must be confirmed via info->len on real HW.
#define CSI_N_CH            64
#define CSI_BUF_INT8_LEN    (CSI_N_CH * 2)   // 128 bytes minimum

// ─── Butterworth IIR ────────────────────────────────────────────────────
// Coefficients live in biquad_coeffs.h (run tools/gen_sos.py to fill).
// A 4th-order bandpass prototype → 4 SOS sections.
#define BIQUAD_N_SECTIONS   4
#define BIQUAD_N_BANDS      2                 // resp + heart

// ─── Normalization ──────────────────────────────────────────────────────
#define NORM_EPS            1e-6f             // peak-norm zero guard

// ─── Diagnostics ────────────────────────────────────────────────────────
#define LOG_INTERVAL_MS     10000
#define SNTP_SERVER         "pool.ntp.org"

// Uncomment to enable per-frame first-channel sample dump (checklist #5/6)
// #define CONFIG_APP_DEBUG_CSI
