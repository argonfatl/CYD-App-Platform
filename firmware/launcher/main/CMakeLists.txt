idf_component_register(SRCS "main.c" "app_store.c" "http_util.c" "launcher_wifi.c" "self_update.c" "web_ui.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES cyd_board cyd_platform app_update bootloader_support esp_app_format
                                     esp_event esp_http_client esp_http_server esp_netif esp_partition
                                     esp_timer esp_wifi fatfs json mbedtls nvs_flash spi_flash)
