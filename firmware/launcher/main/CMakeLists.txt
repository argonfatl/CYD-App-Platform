idf_component_register(SRCS "main.c" "launcher_wifi.c" "self_update.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES cyd_board cyd_platform app_update bootloader_support esp_app_format
                                     esp_event esp_http_client esp_netif esp_partition esp_timer
                                     esp_wifi json mbedtls nvs_flash spi_flash)
