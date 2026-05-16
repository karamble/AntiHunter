// Halberd C5 firmware — bring-up entry point.
//
// Stage 1: bootstrap only. Prints a boot banner over USB Serial/JTAG and
// heartbeats once per second so we can confirm the build/flash/monitor chain
// end-to-end before any protocol code goes in.

#include <inttypes.h>
#include <stdio.h>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ble.h"
#include "exp.h"
#include "gps.h"
#include "hardware.h"
#include "ieee802154.h"
#include "link.h"
#include "wifi.h"
#include "wifi_sniff.h"

static const char *TAG = "halberd-c5";

void app_main(void) {
    ESP_LOGI(TAG, "================================================");
    ESP_LOGI(TAG, " Halberd C5 firmware — stage 8 (5 GHz probe sniffer)");
    ESP_LOGI(TAG, " feat/c5-firmware, ESP-IDF " IDF_VER);
    ESP_LOGI(TAG, "================================================");

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    ESP_LOGI(TAG, "chip=%s cores=%d rev=%d.%d features=0x%" PRIx32,
             CONFIG_IDF_TARGET, chip.cores,
             chip.revision / 100, chip.revision % 100,
             (uint32_t)chip.features);

    uint32_t flash_size = 0;
    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        ESP_LOGI(TAG, "flash=%" PRIu32 "MB", flash_size / (1024 * 1024));
    }

    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        ESP_LOGI(TAG, "wifi-sta mac=%02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    ESP_LOGI(TAG, "pinmap: link=UART%d(tx=%d,rx=%d,baud=%d) "
                  "gps=UART%d(tx=%d,rx=%d,baud=%d) "
                  "i2c(sda=%d,scl=%d)",
             HALBERD_C5_LINK_UART_NUM,
             HALBERD_C5_LINK_TX_GPIO, HALBERD_C5_LINK_RX_GPIO,
             HALBERD_C5_LINK_BAUD,
             HALBERD_C5_GPS_UART_NUM,
             HALBERD_C5_GPS_TX_GPIO, HALBERD_C5_GPS_RX_GPIO,
             HALBERD_C5_GPS_BAUD,
             HALBERD_C5_EXP_SDA_GPIO, HALBERD_C5_EXP_SCL_GPIO);

    link_init();
    gps_init();
    wifi_init();
    wifi_sniff_init();
    ble_init();
    ieee802154_init();
    exp_init();

    // Periodic housekeeping: status beacon + decoder stats every 30 s. The
    // link task itself fires a PING every 5 s (see link.c); the GPS task
    // pushes a GPS_FIX frame every 1 s (see gps.c); the Wi-Fi, BLE,
    // 802.15.4, and expansion-bus handlers run on demand off the link
    // dispatcher.
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));
        link_log_stats();
        link_send_status();
    }
}
