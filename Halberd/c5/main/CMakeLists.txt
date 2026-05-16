idf_component_register(
    SRCS "main.c"
         "link.c"
         "gps.c"
         "wifi.c"
         "wifi_sniff.c"
         "ble.c"
         "ieee802154.c"
         "exp.c"
         "sensors.c"
         "sensors_drv_register_read.c"
         "sensors_drv_sscma.c"
         "../../shared/link_codec.c"
    INCLUDE_DIRS "."
                 "../../shared"
    # cJSON moved out of IDF base in 6.x; the component manager pulls
    # it in via main/idf_component.yml as espressif/cjson. Use
    # PRIV_REQUIRES (not REQUIRES) so the main component's implicit
    # access to driver/, spi_flash, esp_wifi, bt, etc. is preserved —
    # REQUIRES on `main` switches it into "explicit-only" mode and
    # breaks every other include that wasn't listed.
    PRIV_REQUIRES espressif__cjson
)
