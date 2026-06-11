#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "config.h"
#include "csi_capture.h"
#include "biquad.h"
#include "net.h"

static const char *TAG = "main";

static EventGroupHandle_t s_wifi_eg;
#define WIFI_CONNECTED_BIT  BIT0

// ─── WiFi ────────────────────────────────────────────────────────────────────

static void wifi_event_cb(void *arg, esp_event_base_t base,
                           int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            ESP_LOGW(TAG, "WiFi disconnected — retrying");
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&e->ip_info.ip));
        xEventGroupSetBits(s_wifi_eg, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init(void)
{
    s_wifi_eg = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               wifi_event_cb, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               wifi_event_cb, NULL));

    wifi_config_t wc = {
        .sta = {
            .ssid      = WIFI_SSID,
            .password  = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    // CSI cadence: disable modem-sleep power save. Default WIFI_PS_MIN_MODEM
    // sleeps between beacons → irregular/throttled CSI callbacks, starving the
    // 100 Hz loop. WIFI_PS_NONE keeps the radio awake for steady CSI delivery.
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    ESP_LOGI(TAG, "Connecting to \"%s\" ...", WIFI_SSID);
    xEventGroupWaitBits(s_wifi_eg, WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);
}

// ─── SNTP ────────────────────────────────────────────────────────────────────

static void sntp_sync(void)
{
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, SNTP_SERVER);
    sntp_init();

    // Wait up to 30 s; proceed regardless (ts_ms will correct automatically once synced)
    int retry = 0;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && retry++ < 15) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        time_t now; time(&now);
        ESP_LOGI(TAG, "SNTP synced: epoch=%ld", (long)now);
    } else {
        // ts_ms will be relative until background resync completes
        ESP_LOGW(TAG, "SNTP timed out — ts_ms is relative until resync");
    }
}

// ─── Entry point ─────────────────────────────────────────────────────────────

void app_main(void)
{
    // NVS is required by the WiFi driver
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init();     // blocks until IP assigned
    sntp_sync();     // best-effort, 30 s timeout

    ESP_ERROR_CHECK(csi_capture_init());   // register CSI callback
    ESP_ERROR_CHECK(biquad_init());        // allocate PSRAM IIR state
    ESP_ERROR_CHECK(net_init());           // create UDP socket + resolve gateway

    net_send_task_start();                 // spawn 100 Hz send loop

    ESP_LOGI(TAG, "SafeWave-AI node %d running at %d Hz", NODE_ID, CSI_FS);

    // app_main can return; FreeRTOS scheduler keeps running other tasks.
}
