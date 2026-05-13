idf_component_register(
    SRCS "main.c"
         "link.c"
         "gps.c"
         "../../shared/link_codec.c"
    INCLUDE_DIRS "."
                 "../../shared"
)
