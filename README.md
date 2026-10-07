# Ski Watch

A colorful Pebble watch face for skiers. A skier in a red jacket carves down a
snowy run under a blue sky and snow-capped peaks. The face also shows the
current weather and how much snow is forecast for the week ahead.

## Features

- **Time**: large, centered digits. Follows the watch's 12h/24h setting and
  drops the leading zero in 12-hour mode.
- **Date**: shown at the bottom in a short format, e.g. `Tue Oct 6`.
- **Battery**: a battery icon with a fill level and the charge percentage,
  shown next to the date.
- **Current weather** (top left): temperature in °F plus a short description
  of conditions (Clear, Overcast, Snow, Hvy snow, Storm, …).
- **7-day snowfall forecast** (top right): a snowflake icon above the total
  snow expected over the next seven days, in inches (e.g. `4.2"`).
- **Status messages**: if weather can't be loaded, the weather area shows why,
  e.g. `Phone not connected`, `No network`, `Timeout` or `Fetching...`.

## How it works

The face has two halves:

- **Watch (C)** – `src/c/ski-watch-face.c` draws the whole scene by hand on a
  single canvas layer: sky, mountains, snow caps, slope, the skier, and the
  text overlays (drawn with a drop shadow so they read against the sky). It
  updates every minute, asks the phone for weather every 30 minutes, and
  retries every 30 seconds until the first weather report arrives.
- **Phone (PebbleKit JS)** – `src/pkjs/index.js` gets the phone's location and
  fetches data from the free [Open-Meteo](https://open-meteo.com/) API (no API
  key needed). It sends the watch the current temperature, a condition label,
  and the sum of the 7-day `snowfall_sum` forecast.

If the phone can't provide a location, the face falls back to a default
location (Denver, CO). To use your home mountain instead, change
`DEFAULT_LAT` / `DEFAULT_LON` at the top of `src/pkjs/index.js`.

### Message keys

| Key           | Direction      | Value                                   |
|---------------|----------------|-----------------------------------------|
| `TEMPERATURE` | phone → watch  | Current temperature, °F (integer)       |
| `CONDITIONS`  | phone → watch  | Condition label or status/error text    |
| `SNOWFALL`    | phone → watch  | 7-day snowfall total, tenths of an inch |

The watch also sends `TEMPERATURE` to the phone to request a refresh.

## Building & installing

Requires the [Pebble SDK](https://developer.repebble.com).

```sh
pebble build                          # build the .pbw
pebble install --emulator basalt      # run in the basalt emulator
pebble install --phone <ip>           # install to a paired phone
```

## Platform

Targets **basalt** (Pebble Time / Pebble Time Steel, 144×168 color). The
drawing uses fixed coordinates for that screen size.

## Project layout

```
src/c/ski-watch-face.c   Watch-side drawing, time, battery, AppMessage handling
src/pkjs/index.js        Phone-side location + Open-Meteo weather fetch
package.json             App metadata (UUID, platforms, message keys)
wscript                  Pebble build rules
```

## Credits

Weather data by [Open-Meteo.com](https://open-meteo.com/) (CC BY 4.0).
