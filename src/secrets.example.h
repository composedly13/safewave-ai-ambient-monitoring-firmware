#pragma once
// ─────────────────────────────────────────────────────────────────────────
// Template. Copy this file to `secrets.h` (same folder) and fill in the real
// WiFi credentials. `secrets.h` is gitignored so the password is never
// committed to this public repo.
//
//   cp src/secrets.example.h src/secrets.h   # then edit secrets.h
//
// config.h auto-includes secrets.h when present; otherwise it falls back to
// the placeholders in config.h (so CI / a fresh clone still builds).
// ─────────────────────────────────────────────────────────────────────────

#define WIFI_SSID       "your_2g_ssid"     // ipTIME 2.4GHz SSID
#define WIFI_PASSWORD   "your_password"
