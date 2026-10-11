# Ski Watch

A colorful Pebble watch face for skiers. A skier in a red jacket and pom-pom
hat carves down a snowy run under snow-capped peaks. The sky behind them
changes with the current weather, and the face shows how much snow is
forecast at Park City for the week ahead.

## Features

- **Time**: large, centered digits. Follows the watch's 12h/24h setting and
  drops the leading zero in 12-hour mode.
- **Temperature + date** (bottom): the current temperature (°F) and a short
  date, e.g. `34°  Tue Oct 6`, centered together on the snow.
- **Battery** (top left): a battery icon and the charge percentage. The fill
  is green, orange at 30% or less, red at 10% or less, and yellow while
  charging.
- **Weather sky**: instead of a text description, the background shows the
  current conditions at your location:

  | Conditions              | Sky                                  |
  |-------------------------|--------------------------------------|
  | Clear / mostly clear    | Blue sky with the sun                |
  | Partly cloudy           | Blue sky, sun and a cloud            |
  | Overcast                | Light gray sky with white clouds     |
  | Fog                     | Light gray sky, haze over the peaks  |
  | Drizzle / rain / showers| Dark gray sky, clouds and raindrops  |
  | Snow                    | Light gray sky with falling snow     |
  | Thunderstorm            | Dark navy sky, clouds and lightning  |

  Under a pale sky the mountains are drawn darker so they stay visible.
- **Night sky**: between local sunset and sunrise the sky switches to a
  night version of the current weather:

  | Conditions              | Night sky                                   |
  |-------------------------|---------------------------------------------|
  | Clear / mostly clear    | Dark navy sky, crescent moon and stars      |
  | Partly cloudy           | Navy sky, moon, stars and a gray cloud      |
  | Overcast / fog / snow   | Dark gray sky, black mountains, dim clouds  |
  | Rain / thunderstorm     | Black sky with dim clouds                   |

  Sunrise and sunset come from the phone for your location, and the watch
  checks them every minute, so the sky changes at dusk and dawn without
  waiting for a weather refresh. The times are saved on the watch; until the
  first ones arrive, night is assumed to be 7pm–7am.
- **7-day snowfall forecast** (top right): a snowflake icon above the total
  snow expected over the next seven days at **Park City Mountain,
  mid-mountain** (~2500 m), in inches (e.g. `4.2"`). This is fixed to Park
  City regardless of where you are.
- **Status messages**: until the first weather report arrives, the bottom row
  shows what the watch is waiting on, e.g. `Phone not connected`,
  `No network`, `Timeout` or `Fetching...`.
- **Resilient weather**: failed requests (e.g. a temporary HTTP 503) are
  retried automatically, and the last good weather is cached on the phone and
  kept on screen, so a brief outage doesn't replace the forecast with an error.

## How it works

The face has two halves:

- **Watch (C)** – `src/c/ski-watch-face.c` draws the whole scene by hand on a
  single canvas layer: the weather sky, mountains, snow caps, slope, the
  skier, and the text overlays (drawn with a drop shadow so they read against the sky). It
  updates every minute, asks the phone for weather every 30 minutes, and
  retries every 30 seconds until the first weather report arrives.
- **Phone (PebbleKit JS)** – `src/pkjs/index.js` fetches data from the free
  [Open-Meteo](https://open-meteo.com/) API (no API key needed) in two
  requests:
  - **Current weather** for the phone's location: temperature and the WMO
    `weather_code`, which the watch maps to a sky, plus today's sunrise and
    sunset, which decide when the night sky is shown.
  - **7-day snowfall** for Park City Mountain (`SNOW_LAT` / `SNOW_LON`), with
    `SNOW_ELEVATION` = 2500 m so the forecast reflects mid-mountain rather
    than the valley floor. The daily `snowfall_sum` values are added up.

### Error handling & caching

- A failed weather request is retried up to 3 times, after 5 s, 20 s and 60 s.
- Each successful result is saved to the phone's `localStorage`. On the next
  refresh, cached weather (if less than 6 hours old) is sent to the watch
  immediately while fresh data loads.
- If every retry fails, the phone re-sends the cached weather; an error
  message is shown only when there is no recent cache.
- Only one refresh runs at a time, so overlapping watch requests don't stack
  up duplicate fetches.
- On the watch, once real weather has been displayed, later status/error text
  from the phone is ignored and the last weather stays on screen.

If the phone can't provide a location, the current weather falls back to a
default location (Denver, CO); change `DEFAULT_LAT` / `DEFAULT_LON` at the top
of `src/pkjs/index.js` to pick another. To track a different resort's
snowfall, change `SNOW_LAT`, `SNOW_LON` and `SNOW_ELEVATION` (meters).

### Message keys

| Key           | Direction      | Value                                   |
|---------------|----------------|-----------------------------------------|
| `TEMPERATURE` | phone → watch  | Current temperature, °F (integer)       |
| `CONDITIONS`  | phone → watch  | WMO weather code (as text), or status/error text when sent without a temperature |
| `SNOWFALL`    | phone → watch  | Park City 7-day snowfall, tenths of an inch |
| `SUNRISE`     | phone → watch  | Today's sunrise, minutes after local midnight |
| `SUNSET`      | phone → watch  | Today's sunset, minutes after local midnight |

The watch also sends `TEMPERATURE` to the phone to request a refresh.

## Building & installing

Requires the [Pebble SDK](https://developer.repebble.com).

```sh
pebble build                          # build the .pbw (run `pebble clean` first after changing message keys)
pebble install --emulator basalt      # run in the basalt emulator
pebble install --phone <ip>           # install to a paired phone
```

## Platform

Targets **basalt** (Pebble Time / Pebble Time Steel, 144×168 color). The
drawing uses fixed coordinates for that screen size.

## Project layout

```
src/c/ski-watch-face.c   Watch-side drawing (day/night weather sky, skier), time, battery, AppMessage handling
src/pkjs/index.js        Phone-side location, Open-Meteo fetches (local weather + Park City snow), retries + cache
package.json             App metadata (UUID, platforms, message keys)
wscript                  Pebble build rules
```

## Credits

Weather data by [Open-Meteo.com](https://open-meteo.com/) (CC BY 4.0).
