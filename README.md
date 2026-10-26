# SG Haze — Pebble watchapp

Shows Singapore's current haze status from NEA on your Pebble:

- **24-hr PSI** and **1-hr PM2.5** for **your region**, side by side in weight:
  each gets half the screen, a large number, and its own NEA band colour
  - The phone's location picks the nearest of NEA's five regions (North, South,
    East, West, Central); the caption names it, e.g. "East 24-hr PSI"
  - With location off, slow, or outside Singapore, it shows the **National**
    reading instead, which is the worst of the five regions
  - PSI: Good / Moderate / Unhealthy / Very Unhealthy / Hazardous
  - PM2.5: Normal / Elevated / High / Very High
- **Down** opens a table of PSI and PM2.5 for North, South, East, West, Central
- Vibrates when either reading moves into a worse band
- Last reading is cached on the watch, so it shows instantly on launch
- The bottom line always says how old the reading is: `Updated - 14:00` while fresh,
  `NEA data - 5h old` / `No phone - 2d old` once it is more than 2 hours old

## Data source

NEA's official open data on data.gov.sg (no API key needed for light use):

- `https://api-open.data.gov.sg/v2/real-time/api/psi`
- `https://api-open.data.gov.sg/v2/real-time/api/pm25`

The legacy `api.data.gov.sg/v1/environment/*` endpoints are tried as a fallback.
The app uses the API instead of scraping haze.gov.sg, because that page's HTML
can change at any time.

## Using it

- Allow the Pebble phone app to use location, or you'll get the National reading.

- Opens and fetches automatically.
- **Select** refreshes on demand.
- **Down** shows the regional table; **Back** returns.
- Refreshes itself at :05 and :35 past each hour while the app is open (NEA publishes hourly).

## Install from your phone (no SDK needed)

Every push to `main` is built by GitHub Actions (`.github/workflows/build.yml`).
The newest build is attached to the `latest` release:

1. On your phone, open the repo's **Releases** page and tap **SG Haze (latest build)**.
2. Download `sg-haze.pbw`.
3. Open the file and choose the **Pebble** app. It installs onto the paired watch.
4. On the watch, open **SG Haze** from the app menu.

## Build

Requires the Pebble SDK (Rebble's `pebble-tool`, or the CloudPebble / Rebble dev environment).

```sh
pebble build
pebble install --phone <phone-ip>      # or: pebble install --emulator basalt
```

Targets: aplite, basalt, diorite, emery (Pebble Time 2), flint (Pebble 2 Duo).
Round watches (chalk, gabbro) are left out until their layout has been checked.

## Tests

The phone-side parsing (`src/pkjs/haze.js`) is plain JS:

```sh
node test/haze.test.js
```

## Data and attribution

Unofficial app, not affiliated with NEA. Contains information from the PSI and
PM2.5 datasets accessed from data.gov.sg, made available under the terms of the
[Singapore Open Data Licence version 1.0](https://data.gov.sg/open-data-licence).

## Licence

The app's code is MIT licensed (see `LICENSE`). The NEA data it displays is
covered separately by the Singapore Open Data Licence, as noted above.
