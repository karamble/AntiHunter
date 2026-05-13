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

// Push a Wi-Fi AP record / scan-done frame to the S3 (see wifi.c). These
// fire while a scan job is running on the C5.
struct link_wifi_ap_result;
struct link_wifi_scan_done;
void link_send_wifi_ap_result(const struct link_wifi_ap_result *r);
void link_send_wifi_scan_done(const struct link_wifi_scan_done *d);

// Register a callback that fires when the S3 sends a WIFI_SCAN_REQ. The
// C5 wifi module installs this during wifi_init(); link's dispatcher
// invokes it on the link task. Caller copies the payload before returning.
struct link_wifi_scan_req;
typedef void (*link_wifi_scan_req_cb)(const struct link_wifi_scan_req *req);
void link_register_wifi_scan_req(link_wifi_scan_req_cb cb);

#ifdef __cplusplus
}
#endif
