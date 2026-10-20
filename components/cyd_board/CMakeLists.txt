idf_component_register(SRCS "cyd_battery.c" "cyd_display.c" "cyd_font.c" "cyd_touch.c" "cyd_sd.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_lcd
                       PRIV_REQUIRES cyd_platform esp_adc esp_driver_gpio esp_driver_spi esp_driver_sdspi fatfs sdmmc
                                     esp_lcd_ili9341 esp_timer)
