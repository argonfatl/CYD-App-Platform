idf_component_register(SRCS "main.cpp" "sat_math.cpp" "net.c" "web_setup.cpp" "sgp4/SGP4.cpp"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES cyd_board cyd_platform esp_event esp_http_client esp_http_server esp_netif esp_timer
                                     esp_wifi fatfs json mbedtls nvs_flash)
# Vallado's reference code has unused variables; keep its warnings out of the build log.
set_source_files_properties("sgp4/SGP4.cpp" PROPERTIES COMPILE_OPTIONS "-w")
