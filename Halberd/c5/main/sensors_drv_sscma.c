// SSCMA (Seeed SenseCraft Model Assistant) I²C driver — for the
// Grove Vision AI V2 (WE-2) and future SSCMA-protocol cameras.
//
// **Wire protocol** (FEATURE_TRANSPORT framing, mirrors Seeed's own
// sscma-example-esp32/components/sscma-client/src/sscma_client_io_i2c.c
// reference driver).
//
//   6-byte header on every I²C op:
//     [0]   0x10  FEATURE_TRANSPORT magic
//     [1]   0x01  READ / 0x02 WRITE / 0x03 AVAILABLE / 0x06 RESET
//     [2-3] payload-length (big-endian)
//     [4-5] 0xFF 0xFF  trailer (CRC placeholder per Seeed TODO)
//
//   WRITE: I²C write of header+payload+trailer.
//   READ:  I²C write of header alone, then I²C read of N+6 bytes
//          (camera echoes a 6-byte trailer; first N are payload).
//   AVAIL: I²C write of header alone, then I²C read of 2 bytes
//          (big-endian count of bytes the camera has queued).
//
// **Application layer** — AT commands terminated by \r\n. Setup
// sequence on probe:
//     AT+ID?              → confirms an SSCMA responder
//     AT+MODEL=<n>        → select compiled-in model slot (manifest)
//     AT+TSCORE=<s>       → confidence threshold (manifest, 0-100)
//     AT+INVOKE=-1,0,1    → run forever, filter=0 (all classes), save events
//
// **Event format** (returned by camera between samples):
//     {"type":1,"name":"INVOKE","code":0,
//      "data":{"boxes":[[x,y,w,h,score,class], ...]}, ...}
//
// **JPEG capture path is intentionally NOT in v1.** The plan defers
// JPEG over SD_WRITE; this driver only parses detection events and
// emits a kv line per detection (`class score x y w h`).
//
// Manifest contract:
//   {
//     "driver":   "sscma",
//     "bus":      "qwiic",
//     "addr":     ["0x62"],
//     "model_id": 1,             // optional, default 1
//     "tscore":   60,            // optional, default 60
//     "emit":     { "tag": "vision", "cooldown_ms": 0 }
//   }

#include "sensors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "exp.h"
#include "link_protocol.h"

static const char *TAG = "drv_sscma";

#define FEATURE_TRANSPORT             0x10
#define FT_CMD_READ                   0x01
#define FT_CMD_WRITE                  0x02
#define FT_CMD_AVAILABLE              0x03
#define FT_CMD_RESET                  0x06

#define SSCMA_MAX_PAYLOAD             200      // per chunk; well under LINK_I2C_DATA_MAX
#define SSCMA_RX_RING_BYTES           1024     // ring buffer for stream parsing
#define SSCMA_PROBE_REPLY_WAIT_MS     150      // delay after sending AT+ID? before reading
#define SSCMA_SETUP_INTER_CMD_MS      50

struct sscma_state {
    uint8_t  model_id;
    uint16_t tscore;
    bool     setup_done;
    bool     setup_failed;             // sample() abandons once true; backoff via cooldown
    uint16_t rx_len;
    uint8_t  rx[SSCMA_RX_RING_BYTES];
};

// ── Low-level FEATURE_TRANSPORT helpers ──────────────────────────────────

// Write `len` bytes of `payload` as one FEATURE_TRANSPORT WRITE frame
// to the camera. Splits into chunks of SSCMA_MAX_PAYLOAD when needed.
static esp_err_t sscma_write(uint8_t addr, const uint8_t *payload, size_t len) {
    uint8_t buf[6 + SSCMA_MAX_PAYLOAD];
    size_t off = 0;
    while (off < len) {
        size_t chunk = (len - off > SSCMA_MAX_PAYLOAD) ? SSCMA_MAX_PAYLOAD : (len - off);
        buf[0] = FEATURE_TRANSPORT;
        buf[1] = FT_CMD_WRITE;
        buf[2] = (uint8_t)(chunk >> 8);
        buf[3] = (uint8_t)(chunk & 0xFF);
        memcpy(buf + 4, payload + off, chunk);
        buf[4 + chunk]     = 0xFF;
        buf[4 + chunk + 1] = 0xFF;
        esp_err_t err = exp_i2c_xfer(addr, buf, 6 + chunk, NULL, 0);
        if (err != ESP_OK) return err;
        off += chunk;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return ESP_OK;
}

// Send a NUL-terminated AT command (caller responsible for "\r\n" at end).
static esp_err_t sscma_send_at(uint8_t addr, const char *cmd) {
    return sscma_write(addr, (const uint8_t *)cmd, strlen(cmd));
}

// Query how many bytes the camera has queued. Returns ESP_OK + *out_len.
static esp_err_t sscma_available(uint8_t addr, uint16_t *out_len) {
    static const uint8_t hdr[6] = { FEATURE_TRANSPORT, FT_CMD_AVAILABLE, 0, 0, 0xFF, 0xFF };
    uint8_t reply[2] = {0};
    esp_err_t err = exp_i2c_xfer(addr, hdr, sizeof(hdr), reply, sizeof(reply));
    if (err != ESP_OK) return err;
    *out_len = ((uint16_t)reply[0] << 8) | reply[1];
    return ESP_OK;
}

// Read up to `want` bytes from the camera's output queue. Reference
// driver reads (want + 6) and discards the trailing 6 — appears to be
// the camera echoing the header on the read; we do the same. Buffer
// `out` must hold at least `want` bytes. Returns actual bytes copied
// into `out`.
static esp_err_t sscma_read(uint8_t addr, uint8_t *out, uint16_t want, uint16_t *out_len) {
    if (want > SSCMA_MAX_PAYLOAD) want = SSCMA_MAX_PAYLOAD;
    uint8_t hdr[6] = { FEATURE_TRANSPORT, FT_CMD_READ,
                       (uint8_t)(want >> 8), (uint8_t)(want & 0xFF),
                       0xFF, 0xFF };
    esp_err_t err = exp_i2c_xfer(addr, hdr, sizeof(hdr), NULL, 0);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(5));

    uint8_t buf[SSCMA_MAX_PAYLOAD + 6];
    err = exp_i2c_xfer(addr, NULL, 0, buf, want + 6);
    if (err != ESP_OK) return err;
    memcpy(out, buf, want);
    *out_len = want;
    return ESP_OK;
}

static esp_err_t sscma_reset(uint8_t addr) {
    static const uint8_t hdr[6] = { FEATURE_TRANSPORT, FT_CMD_RESET, 0, 0, 0xFF, 0xFF };
    return exp_i2c_xfer(addr, hdr, sizeof(hdr), NULL, 0);
}

// ── Stream-parser helpers ────────────────────────────────────────────────

// Find the first balanced top-level JSON object in `buf`. On success
// returns the byte offset just past the closing `}` (length of the
// object); start is set to the offset of the opening `{`. Returns 0
// when no complete object fits.
//
// Single-pass, brace-only counting — sufficient for SSCMA event JSON
// which doesn't contain literal `{`/`}` inside strings except as
// part of nested objects. If that ever bites us we add a proper
// string-aware scanner; not needed for the v1 box-extraction path.
static size_t find_complete_json(const uint8_t *buf, size_t len, size_t *start) {
    size_t i = 0;
    while (i < len && buf[i] != '{') i++;
    if (i >= len) return 0;
    *start = i;
    int depth = 0;
    bool in_string = false;
    bool escape = false;
    for (size_t j = i; j < len; j++) {
        char c = (char)buf[j];
        if (escape) { escape = false; continue; }
        if (c == '\\') { escape = true; continue; }
        if (c == '"') { in_string = !in_string; continue; }
        if (in_string) continue;
        if (c == '{') depth++;
        else if (c == '}') {
            depth--;
            if (depth == 0) return (j + 1) - i;
        }
    }
    return 0;
}

// Build the kv blob for one detection box: "class=N score=N x=N y=N w=N h=N".
// Returns bytes written into `out`, or 0 on failure.
static size_t format_box_kv(const cJSON *box, char *out, size_t cap) {
    if (!cJSON_IsArray(box) || cJSON_GetArraySize(box) < 6) return 0;
    int x     = cJSON_GetArrayItem(box, 0)->valueint;
    int y     = cJSON_GetArrayItem(box, 1)->valueint;
    int w     = cJSON_GetArrayItem(box, 2)->valueint;
    int h     = cJSON_GetArrayItem(box, 3)->valueint;
    int score = cJSON_GetArrayItem(box, 4)->valueint;
    int cls   = cJSON_GetArrayItem(box, 5)->valueint;
    int n = snprintf(out, cap,
                     "class=%d score=%d x=%d y=%d w=%d h=%d",
                     cls, score, x, y, w, h);
    return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
}

// Walk the rx ring, parse the latest complete event, fill ev->kv with
// the first detection's coordinates. Compacts the ring by dropping
// everything up to the end of the parsed object so partial bytes
// after it carry over to the next sample. Returns true on emit.
static bool extract_one_event(struct sscma_state *st, struct link_sensor_event *ev) {
    size_t start = 0;
    size_t obj_len = find_complete_json(st->rx, st->rx_len, &start);
    if (obj_len == 0) {
        // No complete event yet — if the ring is full, drop the oldest
        // half so we don't get permanently stuck on a malformed prefix.
        if (st->rx_len >= SSCMA_RX_RING_BYTES - 64) {
            memmove(st->rx, st->rx + (st->rx_len / 2), st->rx_len - (st->rx_len / 2));
            st->rx_len = st->rx_len - (st->rx_len / 2);
            ESP_LOGW(TAG, "rx ring trimmed to recover from malformed stream");
        }
        return false;
    }

    // Temporarily NUL-terminate at end-of-object for cJSON_Parse.
    uint8_t saved = st->rx[start + obj_len];
    st->rx[start + obj_len] = 0;
    cJSON *root = cJSON_Parse((const char *)(st->rx + start));
    st->rx[start + obj_len] = saved;

    // Always advance past this object regardless of parse result.
    size_t consumed = start + obj_len;
    memmove(st->rx, st->rx + consumed, st->rx_len - consumed);
    st->rx_len -= consumed;

    if (!root) return false;

    // Looking for data.boxes[0] = [x, y, w, h, score, class].
    cJSON *data  = cJSON_GetObjectItemCaseSensitive(root, "data");
    cJSON *boxes = data ? cJSON_GetObjectItemCaseSensitive(data, "boxes") : NULL;
    bool emitted = false;
    if (cJSON_IsArray(boxes) && cJSON_GetArraySize(boxes) > 0) {
        cJSON *first = cJSON_GetArrayItem(boxes, 0);
        size_t n = format_box_kv(first, ev->kv, LINK_SENSOR_KV_MAX);
        if (n > 0) {
            ev->kv_len = (uint16_t)n;
            emitted = true;
        }
    }
    cJSON_Delete(root);
    return emitted;
}

// ── Driver ops ───────────────────────────────────────────────────────────

static bool probe(struct sensor_slot *slot) {
    if (slot->addr == 0) return false;
    // No exp_i2c_addr_present() short-circuit on purpose: the boot scan
    // can miss late-booting smart peripherals (the WE-2 clock-stretches
    // for several seconds during its own boot), and the real reset
    // write below has a fast timeout that'll surface a genuine absence.

    // Identify: write AT+ID?, wait, see if anything comes back.
    if (sscma_reset(slot->addr) != ESP_OK) {
        ESP_LOGW(TAG, "%s: reset write failed at 0x%02X", slot->name, slot->addr);
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    if (sscma_send_at(slot->addr, "AT+ID?\r\n") != ESP_OK) {
        ESP_LOGW(TAG, "%s: AT+ID? send failed", slot->name);
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(SSCMA_PROBE_REPLY_WAIT_MS));

    uint16_t avail = 0;
    if (sscma_available(slot->addr, &avail) != ESP_OK) {
        ESP_LOGW(TAG, "%s: AVAILABLE after probe failed", slot->name);
        return false;
    }
    if (avail == 0) {
        ESP_LOGI(TAG, "%s: no AT reply at 0x%02X — not an SSCMA device", slot->name, slot->addr);
        return false;
    }
    // Drain whatever response came back; we don't parse the AT+ID? body
    // (it varies by firmware), just that something replied is enough.
    uint8_t junk[SSCMA_MAX_PAYLOAD];
    uint16_t got = 0;
    sscma_read(slot->addr, junk, avail > SSCMA_MAX_PAYLOAD ? SSCMA_MAX_PAYLOAD : avail, &got);
    ESP_LOGI(TAG, "%s: SSCMA responder claimed at 0x%02X (probe reply %u bytes)",
             slot->name, slot->addr, got);

    struct sscma_state *st = calloc(1, sizeof(*st));
    if (!st) return false;

    const cJSON *m = cJSON_GetObjectItemCaseSensitive(slot->manifest, "model_id");
    st->model_id = (cJSON_IsNumber(m) && m->valueint > 0 && m->valueint < 255)
                       ? (uint8_t)m->valueint : 1;
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(slot->manifest, "tscore");
    st->tscore = (cJSON_IsNumber(t) && t->valueint > 0 && t->valueint <= 100)
                     ? (uint16_t)t->valueint : 60;

    slot->user = st;
    return true;
}

// Lazy one-shot setup, run from first sample() call. Failures are
// sticky — flips st->setup_failed so we stop hammering the camera.
static void ensure_setup(struct sensor_slot *slot, struct sscma_state *st) {
    if (st->setup_done || st->setup_failed) return;
    char buf[32];

    snprintf(buf, sizeof(buf), "AT+MODEL=%u\r\n", st->model_id);
    if (sscma_send_at(slot->addr, buf) != ESP_OK) { st->setup_failed = true; return; }
    vTaskDelay(pdMS_TO_TICKS(SSCMA_SETUP_INTER_CMD_MS));

    snprintf(buf, sizeof(buf), "AT+TSCORE=%u\r\n", st->tscore);
    if (sscma_send_at(slot->addr, buf) != ESP_OK) { st->setup_failed = true; return; }
    vTaskDelay(pdMS_TO_TICKS(SSCMA_SETUP_INTER_CMD_MS));

    if (sscma_send_at(slot->addr, "AT+INVOKE=-1,0,1\r\n") != ESP_OK) {
        st->setup_failed = true;
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(SSCMA_SETUP_INTER_CMD_MS));

    st->setup_done = true;
    ESP_LOGI(TAG, "%s: setup complete (model=%u tscore=%u, INVOKE started)",
             slot->name, st->model_id, st->tscore);
}

static bool sample(struct sensor_slot *slot, struct link_sensor_event *ev) {
    struct sscma_state *st = (struct sscma_state *)slot->user;
    if (!st || st->setup_failed) return false;

    ensure_setup(slot, st);
    if (!st->setup_done) return false;

    // Pull whatever's queued. Camera streams continuously after INVOKE.
    uint16_t avail = 0;
    if (sscma_available(slot->addr, &avail) != ESP_OK) return false;
    while (avail > 0 && st->rx_len < SSCMA_RX_RING_BYTES - SSCMA_MAX_PAYLOAD) {
        uint16_t want = avail > SSCMA_MAX_PAYLOAD ? SSCMA_MAX_PAYLOAD : avail;
        if (st->rx_len + want > SSCMA_RX_RING_BYTES) {
            want = (uint16_t)(SSCMA_RX_RING_BYTES - st->rx_len);
        }
        uint16_t got = 0;
        if (sscma_read(slot->addr, st->rx + st->rx_len, want, &got) != ESP_OK) break;
        if (got == 0) break;
        st->rx_len += got;
        avail = (avail > got) ? (uint16_t)(avail - got) : 0;
    }

    memcpy(ev->tag, slot->tag, slot->tag_len);
    ev->tag_len = slot->tag_len;
    return extract_one_event(st, ev);
}

static void teardown(struct sensor_slot *slot) {
    if (slot->user) {
        struct sscma_state *st = (struct sscma_state *)slot->user;
        if (st->setup_done) {
            sscma_send_at(slot->addr, "AT+BREAK\r\n");
        }
        free(st);
        slot->user = NULL;
    }
}

const struct sensor_driver sensor_drv_sscma = {
    .name      = "sscma",
    .driver_id = LINK_SENSOR_DRV_SSCMA,
    .probe     = probe,
    .sample    = sample,
    .teardown  = teardown,
};
