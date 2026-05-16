#pragma once

// Halberd C5 external-sensor framework (stage 9).
//
// Manifest-driven driver runtime for sensors attached to J_EXP / J_QWIIC.
// At boot the framework fetches /sensors.json from the S3's SD card via
// the SD-proxy link RPCs, parses it with cJSON, claims a slot per entry
// by calling the matching driver's probe() against the configured I²C
// address(es), then runs a single poller task that walks active slots at
// the manifest-supplied cadence and emits SENSOR_EVENT frames to the S3.
//
// **SD-gate semantics:** the framework is gated on `/sensors.json` being
// reachable. No SD card, no manifest, or a parse error → the framework
// enters SLEEPING and re-probes every 30 seconds, so hot-inserting a
// card after boot transitions to ACTIVE without a reboot. Halberd itself
// continues to function without SD; this gate scopes only the sensor
// task. See feedback in project_sensor_driver_framework memory.
//
// Drivers register themselves through the static driver registry table
// in sensors.c (build-time). Adding a new driver family means:
//   1. New `sensors_drv_<name>.c` exporting a `const struct sensor_driver
//      sensor_drv_<name>;`.
//   2. Append the symbol to `s_drivers[]` in sensors.c.
//   3. Add a corresponding `LINK_SENSOR_DRV_<NAME>` enum entry in
//      Halberd/shared/link_protocol.h.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "link_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SENSORS_MAX_SLOTS  16
#define SENSORS_SLOT_NAME_MAX  32

// Forward decls — drivers receive a slot pointer but only the framework
// allocates/frees them. cJSON pointer kept opaque to drivers; helpers in
// sensors.c read fields from `slot->manifest` on the driver's behalf.
struct sensor_slot;
struct cJSON;

// Driver registration record. Drivers export one `const struct
// sensor_driver` and append it to s_drivers[] in sensors.c. Function
// pointers may be NULL where the driver doesn't need the hook.
struct sensor_driver {
    const char *name;          // matches manifest "driver" string
    uint8_t     driver_id;     // wire id for SENSOR_EVENT.driver_id

    // Called once per matching manifest entry at framework startup, with
    // slot already populated from the manifest (slot->name, slot->addr,
    // slot->tag, slot->poll_ms, etc.). Driver may allocate per-instance
    // state and stash a pointer in slot->user. Returns true to keep the
    // slot active, false to skip (e.g. probe register didn't match).
    bool (*probe)(struct sensor_slot *slot);

    // Called from the poller task at slot->poll_ms cadence. Driver fills
    // ev->tag, ev->tag_len, ev->kv, ev->kv_len from current readings.
    // Returns true if a SENSOR_EVENT should be emitted; false to skip
    // this tick (e.g. no fresh data or below threshold).
    bool (*sample)(struct sensor_slot *slot, struct link_sensor_event *ev);

    // Optional teardown when the framework transitions out of ACTIVE
    // (SD removed, manifest reloaded, etc.). Driver should free anything
    // stashed in slot->user.
    void (*teardown)(struct sensor_slot *slot);
};

// Per-slot state. Framework owns the lifetime; drivers may read all
// fields and write to `user`. `manifest` points into the parsed cJSON
// tree that sensors.c keeps alive for the duration of ACTIVE.
struct sensor_slot {
    char     name[SENSORS_SLOT_NAME_MAX];
    const struct sensor_driver *drv;
    uint8_t  driver_id;                       // mirror of drv->driver_id
    uint8_t  slot_index;                      // 0..SENSORS_MAX_SLOTS-1
    uint8_t  addr;                            // 7-bit I²C addr; 0 = non-I²C
    uint32_t poll_ms;
    uint32_t last_poll_ms;
    uint32_t cooldown_ms;
    uint32_t last_emit_ms;
    char     tag[LINK_SENSOR_TAG_MAX];
    uint8_t  tag_len;
    void    *user;                            // driver-allocated state
    struct cJSON *manifest;                   // borrowed; owned by sensors.c
};

// Spawn the framework. Idempotent. Call once from app_main() after
// link_init() and exp_init() so the SD client and the I²C bus are both
// available.
void sensors_init(void);

#ifdef __cplusplus
}
#endif
