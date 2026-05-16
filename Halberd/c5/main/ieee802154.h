#pragma once

// C5-side IEEE 802.15.4 frame sniffer. The ESP32-C5 has a hardware
// 802.15.4 PHY at 2.4 GHz; this module drives it in promiscuous RX mode,
// hopping channels 11-26 on demand. Each captured MAC frame is parsed,
// classified by protocol family (Zigbee / Thread / Matter / Unknown),
// and emitted to the S3 as a LINK_MSG_IEEE_DETECTION frame.
//
// Unlike Wi-Fi and BLE, the S3 has no native 802.15.4 radio so this is
// a C5-only feature, not a "mirror".

#ifdef __cplusplus
extern "C" {
#endif

void ieee802154_init(void);

#ifdef __cplusplus
}
#endif
