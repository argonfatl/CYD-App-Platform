idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES cyd_board cyd_platform app_update esp_app_format esp_partition
                                     esp_timer json mbedtls nvs_flash)
