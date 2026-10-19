# Clock ELF app

The Clock example is the second runtime test for CYD App Platform. It is built as a loadable ELF module and embedded into the launcher firmware.

The launcher initializes the CYD's ILI9341 display and draws a 320x240 landscape clock UI while the ELF module runs. The screen shows elapsed time since startup, module status, and a note that wall-clock time is not synchronized yet.

The ELF module runs a 30-second counter and then returns control to the launcher. The display remains active and changes status to `APP FINISHED`.

Hardware pin mapping in this prototype targets the common 2.8-inch ESP32 CYD (ILI9341): SCLK GPIO14, MOSI GPIO13, MISO GPIO12, LCD CS GPIO15, DC GPIO2, backlight GPIO21. Other CYD variants may use different pin assignments.

This is an elapsed-time demo, not a real clock: Wi-Fi/NTP synchronization and app-owned display APIs have not been added yet.
