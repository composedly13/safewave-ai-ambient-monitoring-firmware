#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"

#include "config.h"
#include "packet.h"
#include "csi_capture.h"
#include "biquad.h"
#include "net.h"

static const char *TAG = "net";

static int                s_sock      = -1;   // UDP data socket → RPi
static int                s_ping_sock = -1;   // raw ICMP socket → gateway (CSI trigger)
static struct sockaddr_in s_target;            // RPi sensing service
static struct sockaddr_in s_ping_dst;          // gateway (ICMP echo target)
static uint16_t           s_ping_seq  = 0;

// ─── Helpers ────────────────────────────────────────────────────────────────

static uint32_t get_ts_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t ms = (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
    return (uint32_t)(ms & 0xFFFFFFFFULL);
}

// Per-frame peak normalisation: amp / (max|amp| + eps) → [0, 1]
static void peak_normalize(const float *in, float *out)
{
    float peak = 0.0f;
    for (int i = 0; i < CSI_N_CH; i++) {
        if (in[i] > peak) peak = in[i];
    }
    float scale = 1.0f / (peak + NORM_EPS);
    for (int i = 0; i < CSI_N_CH; i++) {
        out[i] = in[i] * scale;
    }
}

// One ICMP echo request to the gateway per tick. The echo *reply* is an
// AP→STA unicast frame, so every reply drives exactly one fresh CSI callback —
// a deterministic ~100 Hz CSI cadence.
//
// Why not the old closed-UDP-port "dummy" trick: a UDP datagram to a dead port
// elicits only an ICMP port-unreachable, which routers rate-limit to ~1/s.
// That starved CSI to <1% fresh frames (drops ≈ 100%). Echo *replies* are not
// rate-limited, so ping restores the full rate.
static void net_ping_send(void)
{
    uint8_t pkt[sizeof(struct icmp_echo_hdr) + 8];
    struct icmp_echo_hdr *icmp = (struct icmp_echo_hdr *)pkt;

    ICMPH_TYPE_SET(icmp, ICMP_ECHO);
    ICMPH_CODE_SET(icmp, 0);
    icmp->chksum = 0;
    icmp->id     = htons(0xC51A);                  // arbitrary session id
    icmp->seqno  = htons(s_ping_seq++);
    memset(pkt + sizeof(struct icmp_echo_hdr), 0xA5, 8);
    icmp->chksum = inet_chksum(pkt, sizeof(pkt));  // S3 has no ICMP HW checksum

    sendto(s_ping_sock, pkt, sizeof(pkt), 0,
           (struct sockaddr *)&s_ping_dst, sizeof(s_ping_dst));

    // Drain replies so the raw-PCB mailbox can't back up. CSI is generated at
    // PHY RX (before lwIP), so the echo-reply payload itself is not needed.
    uint8_t scratch[64];
    while (recv(s_ping_sock, scratch, sizeof(scratch), 0) > 0) { }
}

// ─── Init ───────────────────────────────────────────────────────────────────

esp_err_t net_init(void)
{
    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock < 0) {
        ESP_LOGE(TAG, "socket() failed: errno=%d", errno);
        return ESP_FAIL;
    }

    // Non-blocking: 100 Hz task must never stall on a slow send
    int fl = fcntl(s_sock, F_GETFL, 0);
    fcntl(s_sock, F_SETFL, fl | O_NONBLOCK);

    // RPi target
    memset(&s_target, 0, sizeof(s_target));
    s_target.sin_family      = AF_INET;
    s_target.sin_port        = htons(TARGET_PORT);
    s_target.sin_addr.s_addr = inet_addr(TARGET_IP);

    // Raw ICMP socket → gateway, used as the per-tick CSI trigger (echo reply
    // = one AP→STA frame = one CSI callback).
    s_ping_sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (s_ping_sock < 0) {
        ESP_LOGE(TAG, "raw ICMP socket() failed: errno=%d", errno);
        return ESP_FAIL;
    }
    fl = fcntl(s_ping_sock, F_GETFL, 0);
    fcntl(s_ping_sock, F_SETFL, fl | O_NONBLOCK);

    // Gateway address — derive from DHCP-assigned IP info (no hardcode needed)
    esp_netif_t *ni = esp_netif_get_default_netif();
    esp_netif_ip_info_t ip;
    ESP_ERROR_CHECK(esp_netif_get_ip_info(ni, &ip));

    memset(&s_ping_dst, 0, sizeof(s_ping_dst));
    s_ping_dst.sin_family      = AF_INET;
    s_ping_dst.sin_addr.s_addr = ip.gw.addr;   // already network byte order
                                               // (port unused for ICMP)

    ESP_LOGI(TAG, "UDP ready → RPi=" TARGET_IP ":%d  CSI trigger=ICMP→" IPSTR,
             TARGET_PORT, IP2STR(&ip.gw));
    return ESP_OK;
}

void net_udp_send(const void *data, size_t len)
{
    int r = sendto(s_sock, data, len, 0,
                   (struct sockaddr *)&s_target, sizeof(s_target));
    if (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        ESP_LOGW(TAG, "sendto errno=%d", errno);
    }
}

// ─── 100 Hz send task ───────────────────────────────────────────────────────

#define SEND_STACK_BYTES  8192
#define SEND_TASK_PRIO    5        // above idle, below WiFi driver

static void send_task(void *arg)
{
    TickType_t    last_wake  = xTaskGetTickCount();
    const TickType_t period  = pdMS_TO_TICKS(CSI_SEND_PERIOD_MS);  // 10 ms
    uint32_t      seq        = 0;
    uint32_t      frames     = 0;
    uint32_t      drops      = 0;
    TickType_t    last_log   = xTaskGetTickCount();

    float raw      [CSI_N_CH];
    float block_raw  [CSI_N_CH];
    float block_resp [CSI_N_CH];
    float block_heart[CSI_N_CH];
    int16_t rssi = -70;

    memset(raw, 0, sizeof(raw));   // safe seed before first CSI callback arrives

    for (;;) {
        xTaskDelayUntil(&last_wake, period);

        // 1. Pull latest CSI frame; on miss reuse previous raw[]
        if (!csi_capture_get_frame(raw, &rssi)) {
            drops++;
            if (drops % 100 == 1) {
                ESP_LOGW(TAG, "no fresh CSI frame — reusing last (%lu total drops)",
                         (unsigned long)drops);
            }
        }

        // 2. Peak-normalise → block_raw  (M1 broadband input, filter-free)
        peak_normalize(raw, block_raw);

        // 3. Per-channel IIR: state persists across frames (temporal filter)
        biquad_process_resp (block_raw, block_resp);
        biquad_process_heart(block_raw, block_heart);

        // 4. Pack 788 B + transmit to RPi
        BinaryPacket pkt;
        packet_fill(&pkt, NODE_ID, seq++, get_ts_ms(), rssi,
                    block_raw, block_resp, block_heart);
        net_udp_send(&pkt, sizeof(pkt));

        // 5. ICMP echo → gateway: the reply is an AP→STA frame that fires the
        //    next CSI callback, sustaining the ~100 Hz fresh-frame cadence.
        net_ping_send();

        // 6. Periodic diagnostics (checklist #3, #7)
        frames++;
        TickType_t now = xTaskGetTickCount();
        uint32_t elapsed_ms = (uint32_t)((now - last_log) * portTICK_PERIOD_MS);
        if (elapsed_ms >= LOG_INTERVAL_MS) {
            float hz       = (float)frames * 1000.0f / (float)elapsed_ms;
            float loss_pct = frames ? 100.0f * (float)drops / (float)frames : 0.0f;
            ESP_LOGI(TAG, "rate=%.1fHz  seq=%lu  drops=%lu(%.1f%%)  rssi=%ddBm",
                     hz, (unsigned long)seq,
                     (unsigned long)drops, loss_pct, (int)rssi);

#ifdef CONFIG_APP_DEBUG_CSI
            // First-channel sample dump — verify band separation (checklist #5/6)
            ESP_LOGD(TAG, "  raw[0]=%.4f  resp[0]=%.6f  heart[0]=%.6f",
                     block_raw[0], block_resp[0], block_heart[0]);
#endif
            frames   = 0;
            drops    = 0;
            last_log = now;
        }
    }
}

void net_send_task_start(void)
{
    xTaskCreate(send_task, "csi_send", SEND_STACK_BYTES, NULL, SEND_TASK_PRIO, NULL);
    ESP_LOGI(TAG, "100 Hz send task started");
}
