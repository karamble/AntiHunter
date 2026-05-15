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
// wifi_radio_mutex is the shared serialisation lock across all four C5
// radios — wifi scan, wifi probe sniff, BLE scan, and IEEE 802.15.4
// scan. The C5 has a single RF chain; whichever task holds the mutex
// owns the radio for the duration of its job. Whoever doesn't hold it
// either surrenders (BUSY) or waits briefly. Created by wifi_init()
// before any other *_init runs, so all four can reach in via the
// extern below.
//
// wifi_radio_up/down bring the esp_wifi stack up and tear it down for
// the lifetime of a single scan or sniff job. Mirror of the S3
// firmware's radioStartSTA / radioStopSTA. Caller must hold
// wifi_radio_mutex around the up/down pair.

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

extern SemaphoreHandle_t wifi_radio_mutex;

void wifi_init(void);

esp_err_t wifi_radio_up(void);
void      wifi_radio_down(void);

#ifdef __cplusplus
}
#endif
