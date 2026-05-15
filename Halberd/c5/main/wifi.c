#include "wifi.h"

#include <inttypes.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "link.h"
#include "link_protocol.h"

static const char *TAG = "wifi";

SemaphoreHandle_t wifi_radio_mutex;

// Default country code. "01" enables passive scan on all globally-permitted
// channels including DFS bands (52-64, 100-144); omits UNII-3 (149-165)
// which is US/CA/AU-only. Override with a build-time #define if a regional
// build is needed.
#ifndef HALBERD_C5_WIFI_COUNTRY
#define HALBERD_C5_WIFI_COUNTRY "01"
#endif

// Per-channel dwell time when the request doesn't specify or specifies a
// total smaller than channel_count * minimum dwell.
#define WIFI_MIN_DWELL_MS_PER_CH   80u
#define WIFI_MAX_DWELL_MS_PER_CH   500u

#define WIFI_SCAN_QUEUE_LEN         4
#define WIFI_SCAN_TASK_STACK     6144
#define WIFI_SCAN_TASK_PRIORITY     5
#define WIFI_AP_BATCH_CAP          32  // wifi_ap_record_t batch per channel

static QueueHandle_t s_req_queue;

static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// Map ESP-IDF auth_mode to the protocol value. They're the same enum today,
// but pinning the mapping here means a future IDF rename can't silently
// shift the wire format.
static uint8_t map_auth_mode(wifi_auth_mode_t m) {
    return (uint8_t)m;
}

static uint8_t map_phy_modes(const wifi_ap_record_t *rec) {
    uint8_t flags = 0;
    if (rec->phy_11b) flags |= (1u << 0);
    if (rec->phy_11g) flags |= (1u << 1);
    if (rec->phy_11n) flags |= (1u << 2);
    if (rec->phy_11ac) flags |= (1u << 3);
    if (rec->phy_11ax) flags |= (1u << 4);
    if (rec->phy_lr)  flags |= (1u << 5);
    if (rec->wps)     flags |= (1u << 6);
    return flags;
}

static void emit_ap(const wifi_ap_record_t *rec, uint32_t scan_id) {
    struct link_wifi_ap_result r = {0};
    r.scan_id   = scan_id;
    r.rssi      = rec->rssi;
    r.channel   = rec->primary;
    memcpy(r.bssid, rec->bssid, LINK_WIFI_BSSID_LEN);
    r.auth_mode = map_auth_mode(rec->authmode);
    r.phy_modes = map_phy_modes(rec);

    // SSID is up to 32 bytes, may or may not be NUL-terminated.
    size_t ssid_len = strnlen((const char *)rec->ssid, LINK_WIFI_SSID_MAX);
    r.ssid_len = (uint8_t)ssid_len;
    if (ssid_len > 0) {
        memcpy(r.ssid, rec->ssid, ssid_len);
    }

    link_send_wifi_ap_result(&r);
}

static void scan_one_channel(uint8_t channel, bool passive, uint32_t dwell_ms,
                             uint32_t scan_id, uint16_t *out_count) {
    wifi_scan_config_t cfg = {0};
    cfg.ssid        = NULL;
    cfg.bssid       = NULL;
    cfg.channel     = channel;
    cfg.show_hidden = true;
    cfg.scan_type   = passive ? WIFI_SCAN_TYPE_PASSIVE : WIFI_SCAN_TYPE_ACTIVE;
    if (passive) {
        cfg.scan_time.passive = dwell_ms;
    } else {
        cfg.scan_time.active.min = (uint32_t)(dwell_ms / 2);
        cfg.scan_time.active.max = dwell_ms;
    }

    esp_err_t err = esp_wifi_scan_start(&cfg, true);   // blocking
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan_start ch=%u failed: %s", channel, esp_err_to_name(err));
        return;
    }

    uint16_t num = 0;
    if (esp_wifi_scan_get_ap_num(&num) != ESP_OK || num == 0) {
        return;
    }
    if (num > WIFI_AP_BATCH_CAP) {
        num = WIFI_AP_BATCH_CAP;
    }
    wifi_ap_record_t recs[WIFI_AP_BATCH_CAP];
    if (esp_wifi_scan_get_ap_records(&num, recs) != ESP_OK) {
        return;
    }
    for (uint16_t i = 0; i < num; i++) {
        emit_ap(&recs[i], scan_id);
    }
    *out_count += num;
}

static void wifi_scan_task(void *arg) {
    (void)arg;
    struct link_wifi_scan_req req;

    while (1) {
        if (xQueueReceive(s_req_queue, &req, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (req.channel_count == 0 || req.channel_count > LINK_WIFI_MAX_CHANNELS) {
            struct link_wifi_scan_done done = {0};
            done.scan_id = req.scan_id;
            done.status  = LINK_WIFI_STATUS_ERROR;
            link_send_wifi_scan_done(&done);
            continue;
        }

        // Serialise with wifi_sniff.c. If the sniff task is using the radio
        // we surrender this job rather than block — the S3 will re-arm.
        if (xSemaphoreTake(wifi_radio_mutex, 0) != pdTRUE) {
            struct link_wifi_scan_done done = {0};
            done.scan_id = req.scan_id;
            done.status  = LINK_WIFI_STATUS_BUSY;
            link_send_wifi_scan_done(&done);
            ESP_LOGW(TAG, "scan id=%" PRIu32 " skipped: radio busy (sniff?)",
                     req.scan_id);
            continue;
        }

        uint32_t dwell = req.duration_ms / req.channel_count;
        if (dwell < WIFI_MIN_DWELL_MS_PER_CH) dwell = WIFI_MIN_DWELL_MS_PER_CH;
        if (dwell > WIFI_MAX_DWELL_MS_PER_CH) dwell = WIFI_MAX_DWELL_MS_PER_CH;

        uint32_t start = now_ms();
        uint16_t ap_count = 0;

        ESP_LOGI(TAG, "scan id=%" PRIu32 " ch_count=%u dwell=%" PRIu32 "ms passive=%u",
                 req.scan_id, req.channel_count, dwell, req.passive);

        for (uint8_t i = 0; i < req.channel_count; i++) {
            uint8_t ch = req.channels[i];
            if (ch == 0) continue;
            scan_one_channel(ch, req.passive != 0, dwell, req.scan_id, &ap_count);
        }

        struct link_wifi_scan_done done = {0};
        done.scan_id     = req.scan_id;
        done.ap_count    = ap_count;
        done.duration_ms = (uint16_t)(now_ms() - start);
        done.status      = LINK_WIFI_STATUS_OK;
        link_send_wifi_scan_done(&done);

        ESP_LOGI(TAG, "scan id=%" PRIu32 " done aps=%u elapsed=%ums",
                 req.scan_id, ap_count, done.duration_ms);

        xSemaphoreGive(wifi_radio_mutex);
    }
}

static void on_scan_req(const struct link_wifi_scan_req *req) {
    // Copy by value into the task queue; the caller's payload lives on the
    // link task's stack and we can't hold onto it. Radio-busy check happens
    // inside the scan task itself so we don't race against the sniff task.
    if (xQueueSend(s_req_queue, req, 0) != pdTRUE) {
        struct link_wifi_scan_done done = {0};
        done.scan_id = req->scan_id;
        done.status  = LINK_WIFI_STATUS_ERROR;
        link_send_wifi_scan_done(&done);
        ESP_LOGE(TAG, "WIFI_SCAN_REQ id=%" PRIu32 " queue full", req->scan_id);
    }
}

void wifi_init(void) {
    // NVS is required by esp_wifi for storing calibration data; the rest of
    // the firmware doesn't use NVS, so a one-time init here is fine.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.sta_disconnected_pm = false;
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    // ieee80211d enabled so the radio honours the country code on DFS bands.
    ESP_ERROR_CHECK(esp_wifi_set_country_code(HALBERD_C5_WIFI_COUNTRY, true));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_req_queue = xQueueCreate(WIFI_SCAN_QUEUE_LEN, sizeof(struct link_wifi_scan_req));
    configASSERT(s_req_queue != NULL);

    // Shared lock for scan + sniff tasks. Must be created before either
    // task is spawned; wifi_sniff_init() is called by main after we return.
    wifi_radio_mutex = xSemaphoreCreateMutex();
    configASSERT(wifi_radio_mutex != NULL);

    link_register_wifi_scan_req(on_scan_req);

    xTaskCreate(wifi_scan_task, "wifi_scan", WIFI_SCAN_TASK_STACK,
                NULL, WIFI_SCAN_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "init country=%s mode=STA scan-only", HALBERD_C5_WIFI_COUNTRY);
}
