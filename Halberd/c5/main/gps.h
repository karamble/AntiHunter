#pragma once

// C5-side GPS module. Owns UART1 (GPIO8 RX / GPIO9 TX @ 9600 baud) and
// parses NMEA sentences from the ATGM336H breakout. Pushes a structured
// link_gps_fix frame to the S3 once per second over the C5↔S3 link.

#ifdef __cplusplus
extern "C" {
#endif

void gps_init(void);

#ifdef __cplusplus
}
#endif
