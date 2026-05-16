#include "ble.h"

#include <inttypes.h>
#include <limits.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "link.h"
#include "link_protocol.h"
#include "wifi.h"

static const char *TAG = "ble";

// IDF v6.0.1 NimBLE quirk: ble_hs_deinit() (called from inside
// nimble_port_deinit during our per-scan tear-down) has an unconditional
// reference to ble_sm_deinit, but the entire body of ble_sm.c is gated
// by NIMBLE_BLE_CONNECT = (BLE_ROLE_CENTRAL || BLE_ROLE_PERIPHERAL).
// We're observer-only (neither central nor peripheral), so ble_sm.c
// compiles to an effectively-empty object and the linker can't find
// ble_sm_deinit. SM has nothing to free at runtime in observer-only
// mode, so a weak no-op stub is the surgical fix — it satisfies the
// linker and lets a future IDF with the SM module compiled in win.
__attribute__((weak)) void ble_sm_deinit(void) {}

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

// Semaphores for per-session NimBLE bring-up / tear-down.
// s_sync_sem: signalled from on_host_sync() once controller + host are
//             synchronised; scan task waits on it before issuing
//             ble_gap_disc.
// s_task_done_sem: signalled from the tail of nimble_host_task() once
//             nimble_port_run() returns; lets ble_radio_down() wait for
//             a clean stack-task exit before calling nimble_port_deinit.
static SemaphoreHandle_t s_sync_sem;
static SemaphoreHandle_t s_task_done_sem;

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

// NimBLE host-task entry — pumps the host stack's event queue while the
// stack is alive. Required boilerplate. Returns when nimble_port_stop()
// is called from ble_radio_down().
static void nimble_host_task(void *arg) {
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
    if (s_task_done_sem) xSemaphoreGive(s_task_done_sem);
    vTaskDelete(NULL);
}

static void on_host_sync(void) {
    // Called once the controller + host are synchronised. Safe to start
    // scanning from here onward. Signal the scan task that NimBLE is ready.
    if (s_sync_sem) xSemaphoreGive(s_sync_sem);
}

static void on_host_reset(int reason) {
    ESP_LOGW(TAG, "NimBLE host reset reason=%d", reason);
}

// Bring NimBLE up for a single scan job. Caller must hold wifi_radio_mutex.
// Returns ESP_OK only after the host has synchronised with the controller
// and is ready to accept ble_gap_disc.
static esp_err_t ble_radio_up(void) {
    // Drain any stale signal from a previous session so we don't unblock
    // on stale state.
    if (s_sync_sem) xSemaphoreTake(s_sync_sem, 0);
    if (s_task_done_sem) xSemaphoreTake(s_task_done_sem, 0);

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init: %s", esp_err_to_name(err));
        return err;
    }

    ble_hs_cfg.sync_cb  = on_host_sync;
    ble_hs_cfg.reset_cb = on_host_reset;
    // Observer-only — we never advertise, so ble_svc_gap_init is skipped.

    nimble_port_freertos_init(nimble_host_task);

    // Wait for the host sync callback (controller + host handshake).
    // 5 s is generous; in practice this fires in well under a second.
    if (xSemaphoreTake(s_sync_sem, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "NimBLE host sync timeout");
        nimble_port_stop();
        xSemaphoreTake(s_task_done_sem, pdMS_TO_TICKS(2000));
        nimble_port_deinit();
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

// Tear NimBLE all the way down. Caller still holds wifi_radio_mutex.
static void ble_radio_down(void) {
    nimble_port_stop();
    // Wait for nimble_host_task to fully exit before deinit'ing the
    // host port; double-tearing without waiting can crash or leak.
    xSemaphoreTake(s_task_done_sem, pdMS_TO_TICKS(2000));
    nimble_port_deinit();
}

static void ble_scan_task(void *arg) {
    (void)arg;
    struct link_ble_scan_req req;

    while (1) {
        if (xQueueReceive(s_req_queue, &req, portMAX_DELAY) != pdTRUE) continue;

        // Serialise with wifi scan + wifi sniff + ieee 802.15.4 scan via
        // the shared radio mutex. Wait up to 5 s for an in-flight scan
        // to release before giving up.
        if (xSemaphoreTake(wifi_radio_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
            struct link_ble_scan_done done = {0};
            done.scan_id = req.scan_id;
            done.status  = LINK_BLE_STATUS_BUSY;
            link_send_ble_scan_done(&done);
            ESP_LOGW(TAG, "scan id=%" PRIu32 " rejected: radio busy",
                     req.scan_id);
            continue;
        }

        // Bring NimBLE up just for this scan.
        esp_err_t up_err = ble_radio_up();
        if (up_err != ESP_OK) {
            xSemaphoreGive(wifi_radio_mutex);
            struct link_ble_scan_done done = {0};
            done.scan_id = req.scan_id;
            done.status  = LINK_BLE_STATUS_ERROR;
            link_send_ble_scan_done(&done);
            continue;
        }

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

        // Tear NimBLE down so the next radio (wifi / 802.15.4) takes its
        // turn against a clean slate.
        ble_radio_down();
        xSemaphoreGive(wifi_radio_mutex);

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
    // Queue-overflow guard only. The radio-level busy check happens inside
    // the scan task via the shared mutex (5 s wait → BUSY), so we don't
    // also reject here just because a scan is in flight — a second request
    // can queue up and run after the first.
    if (xQueueSend(s_req_queue, req, 0) != pdTRUE) {
        struct link_ble_scan_done done = {0};
        done.scan_id = req->scan_id;
        done.status  = LINK_BLE_STATUS_ERROR;
        link_send_ble_scan_done(&done);
        ESP_LOGE(TAG, "BLE_SCAN_REQ id=%" PRIu32 " queue full", req->scan_id);
    }
}

void ble_init(void) {
    // No NimBLE init here — the stack is loaded only per scan job inside
    // ble_radio_up() and torn down in ble_radio_down(). Mirrors the S3
    // firmware's BLEDevice::init / deinit(true) pattern around scan
    // modes. Between scans the C5 has zero BLE controller memory in RAM.

    s_req_queue = xQueueCreate(BLE_SCAN_QUEUE_LEN, sizeof(struct link_ble_scan_req));
    configASSERT(s_req_queue != NULL);

    s_sync_sem      = xSemaphoreCreateBinary();
    s_task_done_sem = xSemaphoreCreateBinary();
    configASSERT(s_sync_sem && s_task_done_sem);

    link_register_ble_scan_req(on_scan_req);

    xTaskCreate(ble_scan_task, "ble_scan", BLE_SCAN_TASK_STACK,
                NULL, BLE_SCAN_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "init queues + task, NimBLE on-demand");
}
