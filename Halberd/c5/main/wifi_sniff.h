#pragma once

// C5-side 5 GHz Wi-Fi probe sniffer (stage 8). Brings up promiscuous-mode
// RX on top of the WIFI_MODE_STA stack already initialised by wifi_init()
// and serialises radio access against wifi.c's scan task via the shared
// wifi_radio_mutex declared in wifi.h.
//
// On boot, registers a LINK_MSG_WIFI_PROBE_REQ callback. While a sniff
// job is running, every captured 802.11 management frame (FCF type=0,
// stype=4 for probe-req; stype=5 for probe-resp when capture_responses
// is set in the request) gets truncated to LINK_WIFI_PROBE_PAYLOAD_MAX,
// packed into a link_wifi_probe_event, and emitted to the S3. The task
// channel-hops the supplied channel list, dwelling
// duration_ms / channel_count on each (clamped), then closes with
// LINK_MSG_WIFI_PROBE_DONE.

#ifdef __cplusplus
extern "C" {
#endif

void wifi_sniff_init(void);

#ifdef __cplusplus
}
#endif
