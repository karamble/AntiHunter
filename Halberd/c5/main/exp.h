#pragma once

// C5-side expansion bus controller. Owns the I²C master on GPIO7
// (SDA) / GPIO23 (SCL) and the five EXP_GPIO pins exposed on J_EXP.
// Plugged-in Qwiic or J_EXP add-on hardware is reached from the S3 by
// sending request frames over the C5↔S3 link; this module executes
// the operations and emits the matching response frames.
//
// The native helpers below let in-process callers on the C5 (notably
// the stage-9 sensor framework's driver runtime) hit the I²C bus
// directly without the UART round-trip through the link bridge.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

void exp_init(void);

// Boot-time I²C scan exposed for in-process callers. exp_init() probes
// 0x08..0x77 once at startup and records every ACKed address in a
// 128-bit bitmap; exp_i2c_addr_present() reads that bitmap. Returns
// false until exp_init() has run (or before the scan completes).
//
// This is the seed data the sensor framework's manifest loader uses to
// short-circuit driver probe() at addresses that nothing responded to.
// The bitmap is not refreshed automatically — exp_i2c_rescan() forces
// a fresh sweep.
bool exp_i2c_addr_present(uint8_t addr);
void exp_i2c_rescan(void);

// Native I²C transfer. Returns ESP_OK on success.
//   - write_len > 0, read_len == 0: pure write (register-set, command)
//   - write_len == 0, read_len > 0: pure read
//   - both > 0: write-then-read with repeated start (typical
//     register-read pattern)
// Buffers may be NULL when the corresponding length is 0.
esp_err_t exp_i2c_xfer(uint8_t addr,
                       const uint8_t *write_buf, size_t write_len,
                       uint8_t *read_buf, size_t read_len);

#ifdef __cplusplus
}
#endif
