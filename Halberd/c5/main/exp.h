#pragma once

// C5-side expansion bus controller. Owns the I²C master on GPIO7
// (SDA) / GPIO23 (SCL) and the five EXP_GPIO pins exposed on J_EXP.
// Plugged-in Qwiic or J_EXP add-on hardware is reached from the S3 by
// sending request frames over the C5↔S3 link; this module executes
// the operations and emits the matching response frames.

#ifdef __cplusplus
extern "C" {
#endif

void exp_init(void);

#ifdef __cplusplus
}
#endif
