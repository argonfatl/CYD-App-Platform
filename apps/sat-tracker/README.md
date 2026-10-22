# Satellites

Shows which satellites are above your location right now and when the next passes will be.

- **SKY** (title *SKY RADAR*) — a polar map of the sky: the edge is the horizon, the center is straight up, north at the top. Dots are satellites above the horizon (green above 45°, cyan above 15°, grey lower); the large white dot is the ISS. Tap a dot to select it; the panel on the right shows its elevation, direction, range, and altitude, or its next pass when it is below the horizon. If a position cannot be computed it shows **NO POSITION** with the SGP4 error code and the age of the data (see [Troubleshooting](#troubleshooting)).
- **OVERHEAD** (LIST) — satellites above the horizon, highest first. Tap one to show it on the sky map.
- **PASSES** (title *UPCOMING PASSES*) — passes in the next 12 hours that climb at least 10° (configurable), with local start time, peak elevation, duration, and direction. Passes in progress are green and marked NOW. They are computed in the background after start-up, which takes a while on the ESP32.

Everything works by touch: the buttons at the bottom switch screens, ▲/▼ on the right page through long lists, and **EXIT** (two taps) returns to the launcher. Holding the screen for 2 seconds also returns to the launcher, on every screen including start-up and errors. BOOT is an optional shortcut: short press switches screens, long press exits.

The header shows the local time and the place name. For 15 seconds after start-up a short notice can replace the place name, such as `USING SAVED ORBITS` (the data came from the SD card) or `CRASH: 60 SATELLITES` (see below).

## How it works

1. Connects to the Wi-Fi network saved by the launcher (`wifi.json`), so there is nothing to set up in the app.
2. Uses the clock the launcher set. If it is not set (for example right after power-on), tries NTP (`pool.ntp.org`, `time.google.com`, `time.cloudflare.com`) and then the `Date` header of a web server, for networks that block NTP.
3. Gets the location, in this order: `config.json` on the SD card (below); the location saved in the launcher (open **GET APPS**, then the web page shown on the screen, *Location*); an approximate location from your public IP address (ipwho.is, ipapi.co, or ip-api.com; the device itself has a private address, but these services see your router's public one); the location from the last run.
4. Downloads orbital elements (TLE) for the CelesTrak `visual` group, about 150 of the brightest satellites, and keeps a copy on the SD card (`/apps/sat-tracker/<group>.tle`). The copy is reused for 12 hours. The time of every download attempt is saved before the attempt, so when a copy exists the app tries CelesTrak at most every 2 hours, even if it restarts or crashes in between. Downloading too often can get the address blocked by CelesTrak.
5. Turns Wi-Fi off to free memory and loads the satellites. Each one takes about 1 KB; loading stops while 64 KB of memory is still free, so usually 110–120 satellites of the `visual` group fit. If the previous run crashed, only 60 are loaded.
6. Computes positions every second with SGP4.

Positions agree with [Skyfield](https://rhodesmill.org/skyfield/) to about 0.001° and pass times to about a second (checked on a computer with the same code). Whether a pass is visible to the eye (sunlit satellite, dark sky) is not computed yet.

## Troubleshooting

| On screen | Meaning and what to do |
|-----------|------------------------|
| `NO WI-FI` | No Wi-Fi saved in the launcher. Put `wifi.json` on the SD card and start the launcher once. |
| `WI-FI FAILED` | The saved network did not connect within 20 seconds. |
| `NO TIME` | Neither NTP nor a web server gave the time. |
| `NO LOCATION` | No location from any source. Set it in the launcher: **GET APPS**, then the web page shown on the screen. |
| `NO ORBIT DATA` | The download failed and there is no copy on the SD card. CelesTrak may be blocking repeated downloads; wait a few hours. |
| `NO SATELLITES` | The orbit data could not be read. Delete the `.tle` file on the SD card to download it again. |
| `NO POSITION`, `SGP4 ERROR n` | SGP4 could not compute this satellite. The app re-reads its data from the SD card once, which fixes a damaged copy in memory. If it stays, `DATA n DAYS OLD` shows how old the elements are; very old data or a re-entered satellite gives error 1 (eccentricity), 2 (mean motion), or 6 (decayed). |
| `CRASH: 60 SATELLITES` | The previous run crashed, so fewer satellites were loaded to be safe. |

On the error screens (all but the last two rows), a tap returns to the launcher. The serial log (115200 baud) has the details, for example `Free heap before loading`, `N satellites loaded`, `SGP4 error`, and `N passes in the next 12 h (took N s)`.

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
