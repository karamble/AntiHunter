idf_component_register(
    SRCS "main.c"
         "link.c"
         "gps.c"
         "wifi.c"
         "ble.c"
         "ieee802154.c"
         "exp.c"
         "../../shared/link_codec.c"
    INCLUDE_DIRS "."
                 "../../shared"
    # No REQUIRES / PRIV_REQUIRES on purpose: the `main` component gets
    # implicit access to every other IDF component by default. Adding
    # either narrows that and breaks our access to driver/, esp_flash.h,
    # etc. Revisit if main grows into something more library-like.
)
