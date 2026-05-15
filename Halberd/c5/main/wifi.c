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

// Default country code. "US" gives the C5 full UNII-1/2A/2C/3 access for
// 5 GHz scanning. GhostESP-Revival uses "US" + ieee80211d=true and that's
// what reliably surfaces 5 GHz APs on the C5 in IDF v6.0.1. Override at
// build time via -DHALBERD_C5_WIFI_COUNTRY for other regions; if you do,
// keep the ieee80211d argument below in sync with the regulatory choice.
#ifndef HALBERD_C5_WIFI_COUNTRY
#define HALBERD_C5_WIFI_COUNTRY "US"
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

// Bring the Wi-Fi stack up. Caller must hold wifi_radio_mutex. Mirrors the
// S3 firmware's radioStartSTA pattern: the stack lives in RAM only while an
// active scan or sniff is in flight; idle time has no Wi-Fi loaded at all.
esp_err_t wifi_radio_up(void) {
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.sta_disconnected_pm = false;
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) goto fail;
    // ieee80211d=true with country="US": matches GhostESP-Revival's working
    // pattern on the C5. Country US has explicit UNII-1/2A/2C/3 5 GHz
    // allowances, and enabling 802.11d lets the radio honour beacon IEs
    // if any heard (but doesn't gate scanning when the country mask is
    // already permissive). The on-demand radio still tears down between
    // scans; what matters for 5 GHz coverage is using the full-band
    // scan path below.
    err = esp_wifi_set_country_code(HALBERD_C5_WIFI_COUNTRY, true);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_start();
    if (err != ESP_OK) goto fail;
    // Short post-start settle. Mirror the S3 radioStartSTA(200ms) pattern.
    vTaskDelay(pdMS_TO_TICKS(200));
    return ESP_OK;
fail:
    esp_wifi_deinit();
    return err;
}

void wifi_radio_down(void) {
    esp_wifi_stop();
    esp_wifi_deinit();
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

        // Serialise with wifi_sniff + ble_scan + ieee802154_scan tasks via
        // the shared radio mutex. Wait up to 5 s so a short concurrent IEEE
        // sweep doesn't cause us to surrender; truly stuck radio → BUSY.
        if (xSemaphoreTake(wifi_radio_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
            struct link_wifi_scan_done done = {0};
            done.scan_id = req.scan_id;
            done.status  = LINK_WIFI_STATUS_BUSY;
            link_send_wifi_scan_done(&done);
            ESP_LOGW(TAG, "scan id=%" PRIu32 " skipped: radio busy",
                     req.scan_id);
            continue;
        }

        // Bring the Wi-Fi stack up for this job only.
        esp_err_t up_err = wifi_radio_up();
        if (up_err != ESP_OK) {
            ESP_LOGE(TAG, "wifi_radio_up: %s", esp_err_to_name(up_err));
            xSemaphoreGive(wifi_radio_mutex);
            struct link_wifi_scan_done done = {0};
            done.scan_id = req.scan_id;
            done.status  = LINK_WIFI_STATUS_ERROR;
            link_send_wifi_scan_done(&done);
            continue;
        }

        uint32_t start = now_ms();
        uint16_t ap_count = 0;

        // Full-band single scan (channel=0). The S3-supplied channel list
        // is ignored — IDF orchestrates the band sweep internally using
        // the country mask, which is the path that actually surfaces 5
        // GHz BSSIDs on the C5 (per-channel set_channel + scan_start in
        // a loop reliably returned aps=0 in IDF v6.0.1). Matches
        // GhostESP-Revival's working pattern: active scan with 250-300 ms
        // per-channel dwell, passive fallback for DFS channels.
        wifi_scan_config_t cfg = {0};
        cfg.ssid        = NULL;
        cfg.bssid       = NULL;
        cfg.channel     = 0;
        cfg.show_hidden = true;
        cfg.scan_type   = WIFI_SCAN_TYPE_ACTIVE;
        cfg.scan_time.active.min = 250;
        cfg.scan_time.active.max = 300;
        cfg.scan_time.passive    = 300;

        ESP_LOGI(TAG, "scan id=%" PRIu32 " full-band active(250-300)/passive(300)",
                 req.scan_id);

        esp_err_t scan_err = esp_wifi_scan_start(&cfg, true);
        if (scan_err != ESP_OK) {
            ESP_LOGW(TAG, "scan_start failed: %s", esp_err_to_name(scan_err));
        } else {
            uint16_t num = 0;
            if (esp_wifi_scan_get_ap_num(&num) == ESP_OK && num > 0) {
                if (num > WIFI_AP_BATCH_CAP) num = WIFI_AP_BATCH_CAP;
                wifi_ap_record_t recs[WIFI_AP_BATCH_CAP];
                if (esp_wifi_scan_get_ap_records(&num, recs) == ESP_OK) {
                    for (uint16_t i = 0; i < num; i++) {
                        emit_ap(&recs[i], req.scan_id);
                    }
                    ap_count = num;
                }
            }
        }

        // Tear the Wi-Fi stack down before releasing the mutex so another
        // radio (BLE / 802.15.4) can take its turn against a clean slate.
        wifi_radio_down();

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

    // netif + event-loop boilerplate stays at boot — these are lightweight
    // and don't claim the RF chain. The Wi-Fi stack itself (esp_wifi_init
    // and friends) only comes up inside wifi_radio_up(), called per scan
    // job from wifi_scan_task and wifi_sniff_task. Mirrors the S3's
    // radioStartSTA / radioStopSTA pattern.
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    s_req_queue = xQueueCreate(WIFI_SCAN_QUEUE_LEN, sizeof(struct link_wifi_scan_req));
    configASSERT(s_req_queue != NULL);

    // Shared lock across all C5 radios (wifi scan/sniff, ble scan, ieee
    // 802.15.4 scan). Despite the name it's the C5 radio mutex.
    // Created here because wifi_init is called first by main(); the other
    // *_init helpers reach in via the extern in wifi.h.
    wifi_radio_mutex = xSemaphoreCreateMutex();
    configASSERT(wifi_radio_mutex != NULL);

    link_register_wifi_scan_req(on_scan_req);

    xTaskCreate(wifi_scan_task, "wifi_scan", WIFI_SCAN_TASK_STACK,
                NULL, WIFI_SCAN_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "init country=%s queues + task, radio on-demand",
             HALBERD_C5_WIFI_COUNTRY);
}
