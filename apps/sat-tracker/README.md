# Satellites

Shows which satellites are above your location right now and when the next passes will be.

- **SKY** — a polar map of the sky: the edge is the horizon, the center is straight up, north at the top. Dots are satellites above the horizon (green above 45°, cyan above 15°, grey lower); the large white dot is the ISS. Tap a dot to select it; the panel on the right shows its elevation, direction, range, and altitude, or its next pass when it is below the horizon.
- **OVERHEAD** (LIST) — satellites above the horizon, highest first. Tap one to show it on the sky map.
- **PASSES** — passes in the next 12 hours that climb at least 10° (configurable), with local start time, peak elevation, duration, and direction. Passes in progress are green and marked NOW. They are computed in the background after start-up, which takes a while on the ESP32.

Everything works by touch: the buttons at the bottom switch screens, ▲/▼ on the right page through long lists, and **EXIT** (two taps) returns to the launcher. Tap the header to cycle the **ALL**, **ISS**, and **FAV** filters. On SKY, tap **+FAV** next to the selected satellite to save or remove it from favorites; the filter and favorites persist across restarts. Passes starting within 10 minutes are highlighted as **SOON**. Holding the screen for 2 seconds also returns to the launcher, on every screen including start-up and errors. BOOT is an optional shortcut: short press switches screens, long press exits.

## How it works

1. Connects to the Wi-Fi network saved by the launcher (`wifi.json`), so there is nothing to set up in the app.
2. Uses the clock the launcher set. If it is not set (for example right after power-on), tries NTP (`pool.ntp.org`, `time.google.com`, `time.cloudflare.com`) and then the `Date` header of a web server, for networks that block NTP.
3. Gets the location, in this order: `config.json` on the SD card (below); the location saved in the launcher (open **GET APPS**, then the web page shown on the screen, *Location*); an approximate location from your public IP address (ipwho.is, ipapi.co, or ip-api.com; the device itself has a private address, but these services see your router's public one); the location from the last run.
4. Downloads orbital elements (TLE) for the CelesTrak `visual` group, about 150 of the brightest satellites, and keeps a copy on the SD card. The copy is reused for 12 hours and whenever the download fails.
5. Turns Wi-Fi off to free memory, then computes positions every second with SGP4.

Positions agree with [Skyfield](https://rhodesmill.org/skyfield/) to about 0.001° and pass times to about a second (checked on a computer with the same code). Whether a pass is visible to the eye (sunlit satellite, dark sky) is not computed yet.

## Settings: `/apps/sat-tracker/config.json` on the SD card (optional)

```json
{
  "lat": 41.88,
  "lon": -88.01,
  "alt_m": 220,
  "name": "LOMBARD",
  "utc_offset_min": -300,
  "min_elevation": 10,
  "group": "visual"
}
```

All fields are optional. Without `lat`/`lon` the launcher's location or the IP location is used; without `utc_offset_min` the offset reported for your IP is used (it follows daylight saving time when the app starts). `group` is any [CelesTrak GP group](https://celestrak.org/NORAD/elements/) name, such as `stations`, `amateur`, or `weather`; at most 220 satellites are loaded.

## Credits

SGP4 is David Vallado's reference implementation as distributed in [python-sgp4](https://github.com/brandon-rhodes/python-sgp4) (MIT); see `main/sgp4/README.md`. Orbital data from [CelesTrak](https://celestrak.org/).
