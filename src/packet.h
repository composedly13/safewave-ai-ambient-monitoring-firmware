#pragma once
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include "config.h"

// ═══════════════════════════════════════════════════════════════════════
// 788-byte UDP packet — wire contract (little-endian, fixed size).
// Python counterpart: struct.Struct("<4sBBHIIhH192f")
//
// Offset  Field          Type       Bytes  Description
//    0    magic[4]       char[4]       4   "CSI!" — discard if mismatch
//    4    node_id        uint8         1   1..5
//    5    reserved       uint8         1   = 0
//    6    n_samples      uint16        2   = CSI_N_CH (64)
//    8    seq_num        uint32        4   monotonic counter
//   12    ts_ms          uint32        4   Unix ms lower 32 bits
//   16    rssi           int16         2   dBm
//   18    reserved2      uint16        2   = 0
//   20    block_raw[64]  float32[64] 256   M1: peak-norm amplitude (broadband)
//  276    block_resp[64] float32[64] 256   M2: 0.1–0.6 Hz filtered
//  532    block_heart[64]float32[64] 256   M2: 0.8–3.0 Hz filtered
//  ─────────────────────────────────────────────────────────────────────
//  Total: 20 + 192×4 = 788 bytes  (fits in one UDP/Ethernet frame)
// ═══════════════════════════════════════════════════════════════════════

typedef struct __attribute__((packed)) {
    char     magic[4];
    uint8_t  node_id;
    uint8_t  reserved;
    uint16_t n_samples;
    uint32_t seq_num;
    uint32_t ts_ms;
    int16_t  rssi;
    uint16_t reserved2;
    float    block_raw  [CSI_N_CH];
    float    block_resp [CSI_N_CH];
    float    block_heart[CSI_N_CH];
} BinaryPacket;

static_assert(sizeof(BinaryPacket) == 788, "BinaryPacket must be 788 bytes");

static inline void packet_fill(
        BinaryPacket *p,
        uint8_t       node_id,
        uint32_t      seq_num,
        uint32_t      ts_ms,
        int16_t       rssi,
        const float  *raw,
        const float  *resp,
        const float  *heart)
{
    p->magic[0] = 'C'; p->magic[1] = 'S'; p->magic[2] = 'I'; p->magic[3] = '!';
    p->node_id   = node_id;
    p->reserved  = 0;
    p->n_samples = CSI_N_CH;
    p->seq_num   = seq_num;
    p->ts_ms     = ts_ms;
    p->rssi      = rssi;
    p->reserved2 = 0;
    memcpy(p->block_raw,   raw,   CSI_N_CH * sizeof(float));
    memcpy(p->block_resp,  resp,  CSI_N_CH * sizeof(float));
    memcpy(p->block_heart, heart, CSI_N_CH * sizeof(float));
}
