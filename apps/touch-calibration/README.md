# Touch Setup (touch calibration)

A system app that calibrates the resistive touch screen for the launcher and every app that uses `cyd_board`.

1. Open **Touch Setup** in the launcher. If touch is badly off, the BOOT button can also open it (short press moves, long press opens).
2. Press the center of each of the four corner targets, then the center target. A stylus or fingernail gives the best result.
3. Draw on the test screen to check that the dots follow, then tap **SAVE**, **REDO** to start over, or **CANCEL**.

To leave without saving while the targets are shown, hold the screen anywhere for 3 seconds (or hold BOOT). After 90 seconds without a touch the app returns to the launcher by itself.

The app fits an affine map from raw XPT2046 readings to screen pixels, so it handles scaling, offsets, swapped axes, and mirroring. The center target is used only as a check: if it is more than 15 pixels off, the app asks you to repeat the calibration.

The result is stored in the `cydcfg` partition (see `cyd_platform_layout.h`) and read by `cyd_touch_init()`. Arduino apps can read it with `cydGetTouchCalibration()` from `sdk/arduino/CYDPlatform.h`.
