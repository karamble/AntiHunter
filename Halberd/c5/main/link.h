#pragma once

// C5-side link layer over UART0 (GPIO11 TX / GPIO12 RX @ 921600 baud).
//
// link_init() owns the UART driver setup and spawns a FreeRTOS task that
// reads bytes, feeds the COBS+CRC decoder, and dispatches received frames.
// It also fires a PING every 5 s so the link is visibly heartbeating on
// the wire even when nothing else is happening.

#ifdef __cplusplus
extern "C" {
#endif

void link_init(void);

// Manual triggers, mostly for diagnostics or coupling to higher-level code
// in later stages. The periodic ping inside link_init already covers the
// keepalive case.
void link_send_ping(void);
void link_send_status(void);
void link_log_stats(void);

// Push a GPS fix frame to the S3. Called by the GPS task once per second
// (see gps.c). Caller owns the struct; this function copies the bytes.
struct link_gps_fix;  // forward decl, full type in link_protocol.h
void link_send_gps_fix(const struct link_gps_fix *fix);

#ifdef __cplusplus
}
#endif
