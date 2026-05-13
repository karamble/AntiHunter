#include "ble.h"

#include <inttypes.h>
#include <limits.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "link.h"
#include "link_protocol.h"

static const char *TAG = "ble";

#define BLE_SCAN_QUEUE_LEN          4
#define BLE_SCAN_TASK_STACK         4096
#define BLE_SCAN_TASK_PRIORITY      6

#define BLE_DEFAULT_INTERVAL_MS    100   // scan interval
#define BLE_DEFAULT_WINDOW_MS       80   // scan window (must be ≤ interval)

static QueueHandle_t s_req_queue;
static volatile bool s_scan_busy;

// Active-scan bookkeeping: incremented per advertising event so the
// SCAN_DONE marker can report a count. Reset on each new scan job.
static volatile uint32_t s_active_scan_id;
static volatile uint16_t s_adv_count;
static volatile uint32_t s_scan_start_ms;

static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// Map NimBLE addr type → wire byte. The values happen to line up but pin
// the mapping so a future NimBLE rename can't silently drift.
static uint8_t map_addr_type(uint8_t nimble_type) {
    switch (nimble_type) {
    case BLE_OWN_ADDR_PUBLIC:        return 0;
    case BLE_OWN_ADDR_RANDOM:        return 1;
    case BLE_OWN_ADDR_RPA_PUBLIC_DEFAULT: return 2;
    case BLE_OWN_ADDR_RPA_RANDOM_DEFAULT: return 3;
    default:                         return nimble_type;
    }
}

// GAP discovery callback. Fires on each advertising packet seen.
static int gap_event_cb(struct ble_gap_event *event, void *arg) {
    (void)arg;
    if (!s_scan_busy) {
        // Stragglers from a cancelled scan — let them go.
        return 0;
    }
    if (event->type != BLE_GAP_EVENT_DISC && event->type != BLE_GAP_EVENT_EXT_DISC) {
        return 0;
    }

    struct link_ble_adv adv = {0};
    adv.scan_id = s_active_scan_id;
    adv.tx_power = INT8_MIN;  // unknown unless set below

    if (event->type == BLE_GAP_EVENT_DISC) {
        const struct ble_gap_disc_desc *d = &event->disc;
        memcpy(adv.addr, d->addr.val, LINK_BLE_ADDR_LEN);
        adv.addr_type     = d->addr.type;   // public/random/rpa
        adv.rssi          = d->rssi;
        adv.primary_phy   = 1;              // legacy adv → 1M PHY
        adv.secondary_phy = 0;
        adv.adv_type      = d->event_type;  // BLE_HCI_ADV_RPT_EVTYPE_*
        uint8_t copy = d->length_data > LINK_BLE_ADV_DATA_MAX
                       ? LINK_BLE_ADV_DATA_MAX
                       : (uint8_t)d->length_data;
        adv.adv_data_len = copy;
        if (copy > 0) memcpy(adv.adv_data, d->data, copy);
    } else {
        // BLE_GAP_EVENT_EXT_DISC (only fires when CONFIG_BT_NIMBLE_EXT_ADV is on).
#if MYNEWT_VAL(BLE_EXT_ADV)
        const struct ble_gap_ext_disc_desc *d = &event->ext_disc;
        memcpy(adv.addr, d->addr.val, LINK_BLE_ADDR_LEN);
        adv.addr_type     = d->addr.type;
        adv.rssi          = d->rssi;
        adv.primary_phy   = d->prim_phy;
        adv.secondary_phy = d->sec_phy;
        adv.tx_power      = d->tx_power;
        adv.adv_type      = d->props;
        uint8_t copy = d->length_data > LINK_BLE_ADV_DATA_MAX
                       ? LINK_BLE_ADV_DATA_MAX
                       : (uint8_t)d->length_data;
        adv.adv_data_len = copy;
        if (copy > 0) memcpy(adv.adv_data, d->data, copy);
#endif
    }

    adv.addr_type = map_addr_type(adv.addr_type);
    link_send_ble_adv(&adv);
    s_adv_count++;
    return 0;
}

static void start_disc(const struct link_ble_scan_req *req) {
    s_scan_busy      = true;
    s_active_scan_id = req->scan_id;
    s_adv_count      = 0;
    s_scan_start_ms  = now_ms();

    struct ble_gap_disc_params params = {0};
    params.itvl              = (req->interval_ms != 0 ? req->interval_ms : BLE_DEFAULT_INTERVAL_MS)
                               * 1000 / 625;  // NimBLE units of 0.625 ms
    params.window            = (req->window_ms != 0 ? req->window_ms : BLE_DEFAULT_WINDOW_MS)
                               * 1000 / 625;
    if (params.window > params.itvl) params.window = params.itvl;
    params.filter_duplicates = 0;             // we want every advertising event
    params.passive           = req->active ? 0 : 1;
    params.limited           = 0;
    params.filter_policy     = BLE_HCI_SCAN_FILT_NO_WL;

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC,
                          req->duration_ms == 0 ? BLE_HS_FOREVER : req->duration_ms,
                          &params, gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc rc=%d", rc);
        struct link_ble_scan_done done = {0};
        done.scan_id = req->scan_id;
        done.status  = LINK_BLE_STATUS_ERROR;
        link_send_ble_scan_done(&done);
        s_scan_busy = false;
    }
}

static void ble_scan_task(void *arg) {
    (void)arg;
    struct link_ble_scan_req req;

    while (1) {
        if (xQueueReceive(s_req_queue, &req, portMAX_DELAY) != pdTRUE) continue;

        ESP_LOGI(TAG, "scan id=%" PRIu32 " dur=%ums phy=0x%02x active=%u",
                 req.scan_id, req.duration_ms, req.phy_mask, req.active);

        start_disc(&req);

        // Block until the scan finishes (or duration_ms elapses on its own).
        // ble_gap_disc with a non-zero duration auto-stops; for FOREVER we'd
        // need to call ble_gap_disc_cancel() ourselves.
        if (req.duration_ms != 0) {
            vTaskDelay(pdMS_TO_TICKS(req.duration_ms + 50));
        }
        if (s_scan_busy) {
            // Defensive: cancel if NimBLE didn't auto-end (unlikely w/
            // non-zero duration; covers cancellation / FOREVER cases).
            int rc = ble_gap_disc_cancel();
            if (rc != 0 && rc != BLE_HS_EALREADY) {
                ESP_LOGW(TAG, "ble_gap_disc_cancel rc=%d", rc);
            }
            s_scan_busy = false;
        }

        struct link_ble_scan_done done = {0};
        done.scan_id     = s_active_scan_id;
        done.adv_count   = s_adv_count;
        done.duration_ms = (uint16_t)(now_ms() - s_scan_start_ms);
        done.status      = LINK_BLE_STATUS_OK;
        link_send_ble_scan_done(&done);

        ESP_LOGI(TAG, "scan id=%" PRIu32 " done advs=%u elapsed=%ums",
                 done.scan_id, done.adv_count, done.duration_ms);
    }
}

static void on_scan_req(const struct link_ble_scan_req *req) {
    if (s_scan_busy) {
        struct link_ble_scan_done done = {0};
        done.scan_id = req->scan_id;
        done.status  = LINK_BLE_STATUS_BUSY;
        link_send_ble_scan_done(&done);
        ESP_LOGW(TAG, "BLE_SCAN_REQ id=%" PRIu32 " rejected: BUSY", req->scan_id);
        return;
    }
    if (xQueueSend(s_req_queue, req, 0) != pdTRUE) {
        struct link_ble_scan_done done = {0};
        done.scan_id = req->scan_id;
        done.status  = LINK_BLE_STATUS_ERROR;
        link_send_ble_scan_done(&done);
        ESP_LOGE(TAG, "BLE_SCAN_REQ id=%" PRIu32 " queue full", req->scan_id);
    }
}

// NimBLE host-task entry — pumps the host stack's event queue. Required
// boilerplate.
static void nimble_host_task(void *arg) {
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void on_host_sync(void) {
    // Called once the controller + host are synchronised. Safe to start
    // scanning from here onward; the scan task waits on its own queue.
    ESP_LOGI(TAG, "NimBLE host synced");
}

static void on_host_reset(int reason) {
    ESP_LOGW(TAG, "NimBLE host reset reason=%d", reason);
}

void ble_init(void) {
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init: %s", esp_err_to_name(err));
        return;
    }

    ble_hs_cfg.sync_cb  = on_host_sync;
    ble_hs_cfg.reset_cb = on_host_reset;
    // Observer-only — we never advertise, so the GAP service (which exposes
    // our device name to connecting peers) is skipped. Keeps flash + RAM
    // down and avoids the ble_svc_gap_init link dependency.

    s_req_queue = xQueueCreate(BLE_SCAN_QUEUE_LEN, sizeof(struct link_ble_scan_req));
    configASSERT(s_req_queue != NULL);

    link_register_ble_scan_req(on_scan_req);

    xTaskCreate(ble_scan_task, "ble_scan", BLE_SCAN_TASK_STACK,
                NULL, BLE_SCAN_TASK_PRIORITY, NULL);
    nimble_port_freertos_init(nimble_host_task);

    ESP_LOGI(TAG, "init NimBLE observer ready");
}
