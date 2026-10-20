# Plane Spotter — CYD App Platform port

This app is based on the original [CYD28PlaneSpotter](https://github.com/argonfatl/CYD28PlaneSpotter) source.

Platform-specific changes:
- Adds the platform Arduino SDK include and a 2-second BOOT-button hold to return to the launcher.
- Disables browser firmware update, GitHub firmware update, rollback endpoint, ArduinoOTA startup/handling, and crash-loop rollback when `CYD_PLATFORM_APP` is defined.
- Prevents LittleFS from formatting flash on mount failure.

Build target is the platform's 4 MB partition layout. This first integration is not yet hardware-validated; check build size, LittleFS availability, touch calibration, and all updater UI paths before installation.
