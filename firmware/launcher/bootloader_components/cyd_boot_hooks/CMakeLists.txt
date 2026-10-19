idf_component_register(SRCS "cyd_boot_hooks.c"
                       INCLUDE_DIRS "../../../../components/cyd_platform/include"
                       PRIV_REQUIRES bootloader_support spi_flash)
