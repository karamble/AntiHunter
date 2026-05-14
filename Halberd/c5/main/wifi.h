#pragma once

// C5-side Wi-Fi scanner. Brings up the ESP-IDF Wi-Fi stack in STA scan-only
// mode at boot, then listens for LINK_MSG_WIFI_SCAN_REQ frames over the
// link and serves them by iterating the requested channel list.
//
// The C5 owns the 5 GHz band that the S3 cannot reach; in practice the S3
// fires a scan request with the 5 GHz channel list while it runs its own
// 2.4 GHz scan via WiFi.scanNetworks() on the Arduino side. Results stream
// back as LINK_MSG_WIFI_AP_RESULT, capped by a LINK_MSG_WIFI_SCAN_DONE.
//
// wifi_radio_mutex (stage 8) serialises radio access between this module
// and wifi_sniff.c. Either the scan task or the sniff task may hold it at
// any time; whichever doesn't gets WIFI_STATUS_BUSY. The mutex is created
// by wifi_init() before either user starts so wifi_sniff_init() is free
// to call into it.

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

extern SemaphoreHandle_t wifi_radio_mutex;

void wifi_init(void);

#ifdef __cplusplus
}
#endif
