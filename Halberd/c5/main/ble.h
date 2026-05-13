#pragma once

// C5-side BLE scanner. Brings up the ESP-IDF NimBLE host in observer
// (scan-only) role at boot, then listens for LINK_MSG_BLE_SCAN_REQ frames
// over the link. While a scan is running, each advertising event becomes
// a LINK_MSG_BLE_ADV frame to the S3; the scan ends with LINK_MSG_BLE_SCAN_DONE
// at the requested duration.
//
// On the C5 we lean on BLE 5 features the S3's NimBLE-Arduino doesn't
// reliably expose: LE Coded PHY (long range, 1/8 or 1/2 rate) and
// extended advertising. Legacy 1M-PHY adverts get mirrored too; the S3's
// bleDeviceCache MAC dedup drops anything it already saw, so the
// genuinely useful records from this side are typically the Coded/EXT ones.

#ifdef __cplusplus
extern "C" {
#endif

void ble_init(void);

#ifdef __cplusplus
}
#endif
