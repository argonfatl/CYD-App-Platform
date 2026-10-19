idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_lcd_ili9341 esp_lcd driver esp_timer
                       EMBED_FILES "clock.app.elf")
