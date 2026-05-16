#include "sensors.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "exp.h"
#include "link.h"
#include "link_protocol.h"

static const char *TAG = "sensors";

#define SENSORS_MANIFEST_PATH       "/sensors.json"
#define SENSORS_MANIFEST_MAX_BYTES  4096
#define SENSORS_TASK_STACK          6144
#define SENSORS_TASK_PRIORITY       4
#define SENSORS_TICK_MS             100      // poller granularity
#define SENSORS_SLEEP_RETRY_MS      30000    // SD-gate re-probe cadence

// Minimal skeleton dropped on the SD when /sensors.json is missing but
// the card is mounted. Operator edits this in place to declare sensors.
// Must fit in a single LINK_SD_DATA_MAX (128 B) chunk; the canonical
// annotated example lives in docs/examples/sensors.example.json.
static const char *const SENSORS_SKELETON =
    "{\"version\":1,"
    "\"_doc\":\"see docs/examples/sensors.example.json\","
    "\"sensors\":[]}\n";

// ── Driver registry ────────────────────────────────────────────────────────
//
// Each driver-family file exports a `const struct sensor_driver
// sensor_drv_<name>` symbol; the registry below is the build-time list
// of what's available. Adding a driver = add the extern + append the
// entry. Order in the table is irrelevant; manifest "driver" string is
// the matcher.

extern const struct sensor_driver sensor_drv_register_read;
extern const struct sensor_driver sensor_drv_sscma;

static const struct sensor_driver *const s_drivers[] = {
    &sensor_drv_register_read,
    &sensor_drv_sscma,
    NULL,
};

static const struct sensor_driver *find_driver(const char *name) {
    if (!name) return NULL;
    for (size_t i = 0; s_drivers[i] != NULL; i++) {
        if (strcmp(s_drivers[i]->name, name) == 0) {
            return s_drivers[i];
        }
    }
    return NULL;
}

// ── State machine ─────────────────────────────────────────────────────────

enum sensor_state {
    SENSOR_STATE_BOOT     = 0,   // initial; not yet attempted manifest
    SENSOR_STATE_SLEEPING = 1,   // SD gate closed; re-probe every 30 s
    SENSOR_STATE_ACTIVE   = 2,   // manifest loaded, drivers polling
    SENSOR_STATE_ERROR    = 3,   // manifest parse failed; needs operator
};

static enum sensor_state  s_state = SENSOR_STATE_BOOT;
static struct sensor_slot s_slots[SENSORS_MAX_SLOTS];
static uint8_t            s_slot_count;
static cJSON             *s_manifest_root;     // owned; freed on shutdown_active
static uint32_t           s_last_retry_ms;

static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// Free all per-slot resources; close out the parsed manifest.
static void shutdown_active(void) {
    for (uint8_t i = 0; i < s_slot_count; i++) {
        struct sensor_slot *slot = &s_slots[i];
        if (slot->drv && slot->drv->teardown) {
            slot->drv->teardown(slot);
        }
        slot->user = NULL;
        slot->drv  = NULL;
    }
    s_slot_count = 0;
    if (s_manifest_root) {
        cJSON_Delete(s_manifest_root);
        s_manifest_root = NULL;
    }
}

// ── Manifest fetch + parse ────────────────────────────────────────────────

// Wraps the multi-chunk SD_READ loop. Returns a heap-allocated, NUL-
// terminated string on success or NULL on failure; sets *out_status to
// the wire status so the caller can distinguish NO_SD / NOT_FOUND /
// IO_ERROR. Caller frees the returned buffer.
static char *fetch_manifest(uint8_t *out_status) {
    uint32_t size = 0;
    uint8_t  status = 0;
    int rc = link_sd_stat(SENSORS_MANIFEST_PATH, &size, NULL, NULL, &status);
    if (rc != 0) {
        *out_status = LINK_SD_STATUS_BUSY;
        return NULL;
    }
    *out_status = status;
    if (status != LINK_SD_STATUS_OK) {
        return NULL;
    }
    if (size == 0 || size > SENSORS_MANIFEST_MAX_BYTES) {
        ESP_LOGW(TAG, "manifest size %" PRIu32 " out of bounds (0..%d)",
                 size, SENSORS_MANIFEST_MAX_BYTES);
        *out_status = LINK_SD_STATUS_BAD_PARAM;
        return NULL;
    }
    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        ESP_LOGE(TAG, "manifest malloc failed for %" PRIu32 " bytes", size);
        *out_status = LINK_SD_STATUS_IO_ERROR;
        return NULL;
    }
    uint32_t off = 0;
    while (off < size) {
        uint16_t want = (size - off) > LINK_SD_DATA_MAX
                            ? LINK_SD_DATA_MAX : (uint16_t)(size - off);
        uint16_t got = 0;
        uint8_t  eof = 0;
        uint8_t  chunk_status = 0;
        rc = link_sd_read(SENSORS_MANIFEST_PATH, off,
                          (uint8_t *)buf + off, want,
                          &got, &eof, &chunk_status);
        if (rc != 0 || chunk_status != LINK_SD_STATUS_OK || got == 0) {
            ESP_LOGW(TAG, "manifest read failed off=%" PRIu32 " rc=%d status=%u got=%u",
                     off, rc, chunk_status, got);
            free(buf);
            *out_status = (chunk_status != 0) ? chunk_status : LINK_SD_STATUS_IO_ERROR;
            return NULL;
        }
        off += got;
        if (eof) break;
    }
    buf[off] = '\0';
    ESP_LOGI(TAG, "manifest loaded (%" PRIu32 " bytes)", off);
    return buf;
}

// Pull a 7-bit I²C address out of a cJSON string like "0x76" or a raw
// numeric. Returns 0 on parse failure.
static uint8_t parse_addr(const cJSON *node) {
    if (!node) return 0;
    if (cJSON_IsNumber(node)) {
        int v = node->valueint;
        return (v > 0 && v <= 0x7F) ? (uint8_t)v : 0;
    }
    if (!cJSON_IsString(node) || !node->valuestring) return 0;
    char *end = NULL;
    long v = strtol(node->valuestring, &end, 0);
    if (end == node->valuestring) return 0;
    return (v > 0 && v <= 0x7F) ? (uint8_t)v : 0;
}

// Populate slot fields shared by every driver from a single manifest
// entry. Driver-specific fields stay in slot->manifest for the driver
// to read in its probe().
static bool populate_slot(struct sensor_slot *slot, cJSON *entry, uint8_t idx) {
    memset(slot, 0, sizeof(*slot));
    slot->slot_index = idx;

    const cJSON *name = cJSON_GetObjectItemCaseSensitive(entry, "name");
    if (cJSON_IsString(name) && name->valuestring) {
        strncpy(slot->name, name->valuestring, SENSORS_SLOT_NAME_MAX - 1);
    } else {
        snprintf(slot->name, sizeof(slot->name), "slot%u", idx);
    }

    const cJSON *poll = cJSON_GetObjectItemCaseSensitive(entry, "poll_ms");
    slot->poll_ms = cJSON_IsNumber(poll) && poll->valueint > 0 ? (uint32_t)poll->valueint : 5000;

    const cJSON *emit = cJSON_GetObjectItemCaseSensitive(entry, "emit");
    if (cJSON_IsObject(emit)) {
        const cJSON *tag = cJSON_GetObjectItemCaseSensitive(emit, "tag");
        if (cJSON_IsString(tag) && tag->valuestring) {
            size_t n = strlen(tag->valuestring);
            if (n > LINK_SENSOR_TAG_MAX) n = LINK_SENSOR_TAG_MAX;
            memcpy(slot->tag, tag->valuestring, n);
            slot->tag_len = (uint8_t)n;
        }
        const cJSON *cd = cJSON_GetObjectItemCaseSensitive(emit, "cooldown_ms");
        slot->cooldown_ms = cJSON_IsNumber(cd) && cd->valueint > 0 ? (uint32_t)cd->valueint : 0;
    }

    if (slot->tag_len == 0) {
        ESP_LOGW(TAG, "slot %u (%s) missing emit.tag — dropping", idx, slot->name);
        return false;
    }

    slot->manifest = entry;
    return true;
}

// Try each addr from the manifest's "addr" array against the driver's
// probe(). First true wins; on success slot->addr is set. Returns true
// if any address claimed.
static bool probe_addresses(struct sensor_slot *slot) {
    if (!slot->drv || !slot->drv->probe) return false;
    const cJSON *addr_node = cJSON_GetObjectItemCaseSensitive(slot->manifest, "addr");
    if (!cJSON_IsArray(addr_node) || cJSON_GetArraySize(addr_node) == 0) {
        // No addr block — driver doesn't need I²C (e.g. gpio_digital).
        slot->addr = 0;
        return slot->drv->probe(slot);
    }
    cJSON *a;
    cJSON_ArrayForEach(a, addr_node) {
        uint8_t addr = parse_addr(a);
        if (addr == 0) continue;
        slot->addr = addr;
        if (slot->drv->probe(slot)) {
            ESP_LOGI(TAG, "slot %u (%s) claimed addr 0x%02X via %s",
                     slot->slot_index, slot->name, addr, slot->drv->name);
            return true;
        }
    }
    slot->addr = 0;
    return false;
}

// Refresh the I²C scan bitmap right before probing manifest entries.
// The boot-time scan in exp_init() runs ~T+0.5s after boot; smart I²C
// peripherals like the Grove Vision AI V2 (WE-2) hold the bus while
// they boot, so the early scan often comes back with 0 devices even
// when they're physically present. A rescan here is the cheap fix —
// also handles hot-plug across SLEEPING retries.
static void load_manifest_rescan_i2c(void) {
    exp_i2c_rescan();
}

// Walk the manifest's "sensors" array and fill s_slots. Returns the
// number of slots successfully claimed.
static uint8_t load_manifest(cJSON *root) {
    load_manifest_rescan_i2c();
    cJSON *sensors = cJSON_GetObjectItemCaseSensitive(root, "sensors");
    if (!cJSON_IsArray(sensors)) {
        ESP_LOGE(TAG, "manifest has no \"sensors\" array");
        return 0;
    }
    uint8_t count = 0;
    cJSON *entry;
    cJSON_ArrayForEach(entry, sensors) {
        if (count >= SENSORS_MAX_SLOTS) {
            ESP_LOGW(TAG, "manifest exceeds SENSORS_MAX_SLOTS=%d, ignoring rest",
                     SENSORS_MAX_SLOTS);
            break;
        }
        if (!cJSON_IsObject(entry)) continue;

        struct sensor_slot *slot = &s_slots[count];
        if (!populate_slot(slot, entry, count)) continue;

        const cJSON *driver_name = cJSON_GetObjectItemCaseSensitive(entry, "driver");
        if (!cJSON_IsString(driver_name) || !driver_name->valuestring) {
            ESP_LOGW(TAG, "slot %u (%s) missing \"driver\" — skip", count, slot->name);
            continue;
        }
        slot->drv = find_driver(driver_name->valuestring);
        if (!slot->drv) {
            ESP_LOGW(TAG, "slot %u (%s) unknown driver \"%s\" — skip",
                     count, slot->name, driver_name->valuestring);
            continue;
        }
        slot->driver_id = slot->drv->driver_id;

        if (!probe_addresses(slot)) {
            ESP_LOGI(TAG, "slot %u (%s) %s probe failed at all addrs — skip",
                     count, slot->name, slot->drv->name);
            slot->drv = NULL;
            continue;
        }
        count++;
    }
    return count;
}

// ── State transitions ─────────────────────────────────────────────────────

static const char *state_name(enum sensor_state s) {
    switch (s) {
    case SENSOR_STATE_BOOT:     return "BOOT";
    case SENSOR_STATE_SLEEPING: return "SLEEPING";
    case SENSOR_STATE_ACTIVE:   return "ACTIVE";
    case SENSOR_STATE_ERROR:    return "ERROR";
    default:                    return "?";
    }
}

static void transition(enum sensor_state next) {
    if (s_state == next) return;
    ESP_LOGI(TAG, "state %s → %s", state_name(s_state), state_name(next));
    s_state = next;
    s_last_retry_ms = now_ms();
}

// First-boot ergonomics: if /sensors.json is missing on a mounted SD,
// drop a minimal skeleton so the operator has a file to edit instead
// of needing to know the schema cold. Best-effort — failure just leaves
// us in SLEEPING. Triggered only on the NOT_FOUND path; once the
// skeleton exists, subsequent attempt_load() calls parse it (and find
// 0 slots, transitioning to SLEEPING with a different log line).
static void try_drop_skeleton(void) {
    const size_t len = strlen(SENSORS_SKELETON);
    uint32_t bytes_written = 0;
    uint8_t  status = 0;
    int rc = link_sd_write(SENSORS_MANIFEST_PATH, 0,
                           (const uint8_t *)SENSORS_SKELETON, (uint16_t)len,
                           LINK_SD_WRITE_FLAG_CREATE_TRUNCATE |
                               LINK_SD_WRITE_FLAG_FINAL_CHUNK,
                           &bytes_written, &status);
    if (rc == 0 && status == LINK_SD_STATUS_OK) {
        ESP_LOGI(TAG, "dropped skeleton " SENSORS_MANIFEST_PATH " (%u bytes) — "
                      "edit it to declare sensors", (unsigned)bytes_written);
    } else {
        ESP_LOGW(TAG, "skeleton write failed rc=%d status=%u", rc, status);
    }
}

// Attempt full manifest load + slot population. Returns the resulting
// state to enter. ACTIVE on success (≥1 slot claimed), SLEEPING on
// missing-file / no-SD, ERROR on parse / IO failure with a present file.
static enum sensor_state attempt_load(void) {
    uint8_t status = 0;
    char *buf = fetch_manifest(&status);
    if (!buf) {
        switch (status) {
        case LINK_SD_STATUS_NO_SD:
            ESP_LOGI(TAG, "no SD card → SLEEPING");
            return SENSOR_STATE_SLEEPING;
        case LINK_SD_STATUS_NOT_FOUND:
            ESP_LOGI(TAG, "no " SENSORS_MANIFEST_PATH " → dropping skeleton");
            try_drop_skeleton();
            return SENSOR_STATE_SLEEPING;
        case LINK_SD_STATUS_BAD_PARAM:
            ESP_LOGW(TAG, "manifest oversize → ERROR");
            return SENSOR_STATE_ERROR;
        case LINK_SD_STATUS_DENIED:
            // Indicates a C5/S3 path-whitelist mismatch — code bug, not
            // recoverable at runtime. Park in ERROR so the operator sees it.
            ESP_LOGE(TAG, "manifest path rejected by S3 whitelist → ERROR");
            return SENSOR_STATE_ERROR;
        default:
            ESP_LOGW(TAG, "manifest fetch failed (status=%u) → SLEEPING", status);
            return SENSOR_STATE_SLEEPING;
        }
    }
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        const char *err = cJSON_GetErrorPtr();
        ESP_LOGE(TAG, "manifest parse failed near: %s", err ? err : "(unknown)");
        return SENSOR_STATE_ERROR;
    }
    uint8_t claimed = load_manifest(root);
    if (claimed == 0) {
        ESP_LOGW(TAG, "manifest loaded but 0 slots claimed → SLEEPING");
        cJSON_Delete(root);
        return SENSOR_STATE_SLEEPING;
    }
    s_manifest_root = root;
    s_slot_count    = claimed;
    ESP_LOGI(TAG, "framework ACTIVE with %u sensor slot(s)", claimed);
    return SENSOR_STATE_ACTIVE;
}

// ── Poller task ───────────────────────────────────────────────────────────

// One pass over active slots. For each slot whose poll_ms has elapsed,
// call sample() and emit a SENSOR_EVENT if it returned true and the
// cooldown allows it.
static void poll_active_slots(void) {
    const uint32_t now = now_ms();
    for (uint8_t i = 0; i < s_slot_count; i++) {
        struct sensor_slot *slot = &s_slots[i];
        if (!slot->drv || !slot->drv->sample) continue;
        if (slot->last_poll_ms != 0 && (now - slot->last_poll_ms) < slot->poll_ms) continue;
        slot->last_poll_ms = now;

        struct link_sensor_event ev = {0};
        if (!slot->drv->sample(slot, &ev)) continue;

        if (slot->cooldown_ms > 0 && slot->last_emit_ms != 0 &&
            (now - slot->last_emit_ms) < slot->cooldown_ms) {
            continue;
        }

        ev.uptime_ms = now;
        ev.driver_id = slot->driver_id;
        ev.slot      = slot->slot_index;
        link_send_sensor_event(&ev);
        slot->last_emit_ms = now;
    }
}

static void sensors_task(void *arg) {
    (void)arg;
    // Initial grace so:
    //   1. The link's first PING/PONG round-trip has completed before
    //      we start asking the S3 for files — reduces boot log noise
    //      from spurious SLEEPING transitions if the S3 is slow.
    //   2. Smart I²C peripherals (notably the Grove Vision AI V2 / WE-2)
    //      have finished their own boot and released the bus. The
    //      WE-2's firmware load is substantially longer than typical
    //      Qwiic devices — bench observation shows >10 s before it
    //      releases SCL and starts ACKing. 12 s gives comfortable
    //      headroom without making the boot UX feel laggy; per-attempt
    //      rescan below picks up devices that take even longer.
    vTaskDelay(pdMS_TO_TICKS(12000));

    for (;;) {
        switch (s_state) {
        case SENSOR_STATE_BOOT: {
            transition(attempt_load());
            break;
        }
        case SENSOR_STATE_SLEEPING: {
            if ((now_ms() - s_last_retry_ms) >= SENSORS_SLEEP_RETRY_MS) {
                enum sensor_state next = attempt_load();
                if (next == SENSOR_STATE_ACTIVE) {
                    transition(next);
                } else {
                    s_last_retry_ms = now_ms();
                }
            }
            vTaskDelay(pdMS_TO_TICKS(SENSORS_TICK_MS));
            break;
        }
        case SENSOR_STATE_ACTIVE: {
            poll_active_slots();
            vTaskDelay(pdMS_TO_TICKS(SENSORS_TICK_MS));
            break;
        }
        case SENSOR_STATE_ERROR: {
            // Parked. Re-attempt only on a much longer cadence — operator
            // is expected to fix /sensors.json and reboot. We still poke
            // once a minute in case a fresh card was swapped in.
            if ((now_ms() - s_last_retry_ms) >= 60000) {
                transition(SENSOR_STATE_BOOT);
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            break;
        }
        }

        // SD removal mid-session: any link_sd_* call returning NO_SD in
        // a driver's sample() is a hint to transition. v1 leaves that
        // decision to the driver — most don't touch SD per-sample. The
        // gate runs at boot + sleep-retry; sufficient for the first
        // bench. Add a per-slot "needs_sd" flag if we add a driver that
        // does per-sample writes (the SSCMA JPEG path eventually).
    }
}

void sensors_init(void) {
    static bool started = false;
    if (started) return;
    started = true;
    memset(s_slots, 0, sizeof(s_slots));
    s_slot_count = 0;
    xTaskCreate(sensors_task, "sensors",
                SENSORS_TASK_STACK, NULL, SENSORS_TASK_PRIORITY, NULL);
    ESP_LOGI(TAG, "init (driver registry: %s)",
             s_drivers[0] ? s_drivers[0]->name : "(empty)");
}
