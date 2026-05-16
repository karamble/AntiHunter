// Generic field-schema I²C driver. Reads one or more registers (or a
// raw byte stream), applies optional CRC validation, scale + offset to
// produce engineering units, and formats them as `key=value` tokens for
// the SENSOR_EVENT payload. Designed to cover the bulk of the simple
// I²C sensor catalog without per-chip code:
//
//   - DS3231, TMP117, LM75, AHT20, BH1750 (plain register reads)
//   - SHT3x, SCD30, SCD40/41 (CRC-validated reads, Sensirion polynomial)
//   - Generic 12-bit ADC modules (ADS1015 single-ended, etc.)
//
// **Manifest contract** (see eager-churning-donut.md for the full
// schema). Per-field block:
//   {
//     "key": "tempC",
//     "reg": "0xFA",                 // optional; omit for raw-read sensors
//     "len": 3,                      // 1..4
//     "endian": "big" | "little",    // default "big"
//     "scale": 0.01, "offset": -40,  // optional, float
//     "crc": {                       // optional; one CRC byte per field
//       "poly": "0x31", "init": "0xFF",
//       "position": "after_field"    // only mode supported in v1
//     }
//   }
//
// Probe (optional):
//   {
//     "reg": "0xD0", "expect": "0x60"   // read 1 byte, compare
//   }
//   or
//   {
//     "cmd": "0x36F6"                   // write 2 bytes, ignore reply
//   }
//   or absent — just verify the address ACKed during boot I²C scan.

#include "sensors.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"

#include "exp.h"
#include "link_protocol.h"

static const char *TAG = "drv_regread";

#define MAX_FIELDS_PER_SLOT  6
#define MAX_FIELD_BYTES      4
#define MAX_KEY_LEN          15      // 14 chars + NUL

struct field_spec {
    char    key[MAX_KEY_LEN + 1];
    uint8_t reg;                     // valid iff has_reg
    bool    has_reg;
    uint8_t len;                     // 1..MAX_FIELD_BYTES
    bool    big_endian;
    bool    has_crc;
    uint8_t crc_poly;
    uint8_t crc_init;
    float   scale;
    float   offset;
    bool    have_scale;
    bool    have_offset;
};

struct driver_state {
    uint8_t           field_count;
    struct field_spec fields[MAX_FIELDS_PER_SLOT];
};

// ── Manifest parsing helpers ─────────────────────────────────────────────

// Pull a hex / decimal integer out of a cJSON value. Returns def on
// missing / unparseable. Accepts numbers or strings like "0xFF" / "42".
static long get_int_with_default(const cJSON *node, long def) {
    if (!node) return def;
    if (cJSON_IsNumber(node)) return (long)node->valueint;
    if (cJSON_IsString(node) && node->valuestring) {
        char *end = NULL;
        long v = strtol(node->valuestring, &end, 0);
        if (end != node->valuestring) return v;
    }
    return def;
}

static float get_float_with_default(const cJSON *node, float def, bool *was_present) {
    if (was_present) *was_present = false;
    if (!node) return def;
    if (cJSON_IsNumber(node)) {
        if (was_present) *was_present = true;
        return (float)node->valuedouble;
    }
    return def;
}

static bool parse_field(const cJSON *node, struct field_spec *out) {
    if (!cJSON_IsObject(node)) return false;
    memset(out, 0, sizeof(*out));

    const cJSON *key = cJSON_GetObjectItemCaseSensitive(node, "key");
    if (!cJSON_IsString(key) || !key->valuestring || !key->valuestring[0]) {
        return false;
    }
    strncpy(out->key, key->valuestring, MAX_KEY_LEN);

    const cJSON *reg = cJSON_GetObjectItemCaseSensitive(node, "reg");
    if (reg && !cJSON_IsNull(reg)) {
        long v = get_int_with_default(reg, -1);
        if (v >= 0 && v <= 0xFF) {
            out->reg = (uint8_t)v;
            out->has_reg = true;
        }
    }

    long len = get_int_with_default(cJSON_GetObjectItemCaseSensitive(node, "len"), 2);
    if (len < 1 || len > MAX_FIELD_BYTES) return false;
    out->len = (uint8_t)len;

    const cJSON *endian = cJSON_GetObjectItemCaseSensitive(node, "endian");
    out->big_endian = !(cJSON_IsString(endian) && endian->valuestring &&
                        strcmp(endian->valuestring, "little") == 0);

    out->scale  = get_float_with_default(cJSON_GetObjectItemCaseSensitive(node, "scale"),  1.0f, &out->have_scale);
    out->offset = get_float_with_default(cJSON_GetObjectItemCaseSensitive(node, "offset"), 0.0f, &out->have_offset);

    const cJSON *crc = cJSON_GetObjectItemCaseSensitive(node, "crc");
    if (cJSON_IsObject(crc)) {
        out->has_crc  = true;
        out->crc_poly = (uint8_t)get_int_with_default(cJSON_GetObjectItemCaseSensitive(crc, "poly"), 0x31);
        out->crc_init = (uint8_t)get_int_with_default(cJSON_GetObjectItemCaseSensitive(crc, "init"), 0xFF);
    }
    return true;
}

// ── CRC-8 helper (Sensirion default poly 0x31 / init 0xFF) ────────────────

static uint8_t crc8(const uint8_t *data, size_t len, uint8_t poly, uint8_t init) {
    uint8_t crc = init;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ poly) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

// ── Driver ops ───────────────────────────────────────────────────────────

static bool probe(struct sensor_slot *slot) {
    if (slot->addr == 0) return false;
    // No exp_i2c_addr_present() short-circuit on purpose: the boot scan
    // can miss late-booting smart peripherals. The probe op below has
    // a timeout that surfaces real absences with no false negatives
    // from a stale boot-scan bitmap.

    // Optional probe block: chip-ID register read or init command.
    const cJSON *probe_block = cJSON_GetObjectItemCaseSensitive(slot->manifest, "probe");
    if (cJSON_IsObject(probe_block)) {
        const cJSON *cmd = cJSON_GetObjectItemCaseSensitive(probe_block, "cmd");
        if (cmd) {
            // Write a 1-2 byte command, no expected reply.
            long v = get_int_with_default(cmd, -1);
            if (v < 0) return false;
            uint8_t bytes[2];
            uint8_t blen = (v > 0xFF) ? 2 : 1;
            if (blen == 2) { bytes[0] = (uint8_t)(v >> 8); bytes[1] = (uint8_t)(v & 0xFF); }
            else           { bytes[0] = (uint8_t)v; }
            esp_err_t err = exp_i2c_xfer(slot->addr, bytes, blen, NULL, 0);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "%s: probe cmd 0x%lX failed: %s",
                         slot->name, v, esp_err_to_name(err));
                return false;
            }
        } else {
            long reg = get_int_with_default(cJSON_GetObjectItemCaseSensitive(probe_block, "reg"), -1);
            long expect = get_int_with_default(cJSON_GetObjectItemCaseSensitive(probe_block, "expect"), -1);
            if (reg < 0 || expect < 0) return false;
            uint8_t reg_byte = (uint8_t)reg;
            uint8_t got = 0;
            esp_err_t err = exp_i2c_xfer(slot->addr, &reg_byte, 1, &got, 1);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "%s: probe read reg 0x%02X failed: %s",
                         slot->name, reg_byte, esp_err_to_name(err));
                return false;
            }
            if (got != (uint8_t)expect) {
                ESP_LOGI(TAG, "%s: probe mismatch reg 0x%02X got 0x%02X want 0x%02X",
                         slot->name, reg_byte, got, (uint8_t)expect);
                return false;
            }
        }
    }

    // Parse + cache field schema.
    const cJSON *fields = cJSON_GetObjectItemCaseSensitive(slot->manifest, "fields");
    if (!cJSON_IsArray(fields) || cJSON_GetArraySize(fields) == 0) {
        ESP_LOGW(TAG, "%s: no \"fields\" array", slot->name);
        return false;
    }
    struct driver_state *ds = calloc(1, sizeof(*ds));
    if (!ds) return false;

    cJSON *f;
    cJSON_ArrayForEach(f, fields) {
        if (ds->field_count >= MAX_FIELDS_PER_SLOT) break;
        if (parse_field(f, &ds->fields[ds->field_count])) {
            ds->field_count++;
        }
    }
    if (ds->field_count == 0) {
        free(ds);
        ESP_LOGW(TAG, "%s: no parseable fields", slot->name);
        return false;
    }
    slot->user = ds;
    return true;
}

// Read one field's raw bytes (+ optional CRC byte) into out_value. The
// CRC byte is consumed but the function returns false if validation
// fails; on success out_value carries the unsigned integer value in
// host byte order. The caller applies scale/offset.
static bool read_field_value(const struct sensor_slot *slot,
                             const struct field_spec *f,
                             int32_t *out_value) {
    const size_t want = f->len + (f->has_crc ? 1 : 0);
    if (want > MAX_FIELD_BYTES + 1) return false;
    uint8_t buf[MAX_FIELD_BYTES + 1];

    esp_err_t err;
    if (f->has_reg) {
        err = exp_i2c_xfer(slot->addr, &f->reg, 1, buf, want);
    } else {
        err = exp_i2c_xfer(slot->addr, NULL, 0, buf, want);
    }
    if (err != ESP_OK) return false;

    if (f->has_crc) {
        uint8_t expect = crc8(buf, f->len, f->crc_poly, f->crc_init);
        if (expect != buf[f->len]) return false;
    }

    int32_t v = 0;
    if (f->big_endian) {
        for (uint8_t i = 0; i < f->len; i++) v = (v << 8) | buf[i];
    } else {
        for (int i = f->len - 1; i >= 0; i--) v = (v << 8) | buf[i];
    }
    *out_value = v;
    return true;
}

static bool sample(struct sensor_slot *slot, struct link_sensor_event *ev) {
    struct driver_state *ds = (struct driver_state *)slot->user;
    if (!ds) return false;

    memcpy(ev->tag, slot->tag, slot->tag_len);
    ev->tag_len = slot->tag_len;

    size_t kv_off = 0;
    bool   any_field = false;
    for (uint8_t i = 0; i < ds->field_count; i++) {
        const struct field_spec *f = &ds->fields[i];
        int32_t raw = 0;
        if (!read_field_value(slot, f, &raw)) {
            ESP_LOGW(TAG, "%s: field %s read failed", slot->name, f->key);
            continue;
        }
        float scaled = (float)raw;
        if (f->have_scale)  scaled *= f->scale;
        if (f->have_offset) scaled += f->offset;

        // Choose format: integers stay integer, scaled gets 2 decimals.
        // Threshold compares fractional component to a small epsilon so
        // 23.0 prints as "23" but 23.45 prints as "23.45".
        char tmp[40];
        int n;
        if (!f->have_scale && !f->have_offset) {
            n = snprintf(tmp, sizeof(tmp), "%s=%ld", f->key, (long)raw);
        } else {
            float intpart;
            if (fabsf(modff(scaled, &intpart)) < 0.005f) {
                n = snprintf(tmp, sizeof(tmp), "%s=%ld", f->key, (long)scaled);
            } else {
                n = snprintf(tmp, sizeof(tmp), "%s=%.2f", f->key, scaled);
            }
        }
        if (n <= 0) continue;

        // Need 1 byte space separator between fields.
        size_t need = (size_t)n + (any_field ? 1 : 0);
        if (kv_off + need >= LINK_SENSOR_KV_MAX) break;
        if (any_field) ev->kv[kv_off++] = ' ';
        memcpy(ev->kv + kv_off, tmp, n);
        kv_off += n;
        any_field = true;
    }

    ev->kv_len = (uint16_t)kv_off;
    return any_field;
}

static void teardown(struct sensor_slot *slot) {
    if (slot->user) {
        free(slot->user);
        slot->user = NULL;
    }
}

const struct sensor_driver sensor_drv_register_read = {
    .name      = "register_read",
    .driver_id = LINK_SENSOR_DRV_REGISTER_READ,
    .probe     = probe,
    .sample    = sample,
    .teardown  = teardown,
};
