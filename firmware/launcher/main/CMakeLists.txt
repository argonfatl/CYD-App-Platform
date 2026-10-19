idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_lcd_ili9341 driver
                       EMBED_FILES "clock.app.elf")
