# SG Haze — Pebble watchapp

Shows Singapore's current haze status from NEA on your Pebble:

- **24-hr PSI** (national), big, with the NEA descriptor
  (Good / Moderate / Unhealthy / Very Unhealthy / Hazardous)
- **1-hr PM2.5** (worst region) with its band (Normal / Elevated / High / Very High)
- Regional 24-hr PSI for North, South, Central, East, West
- Background colour follows the PSI band on colour watches; vibrates when the band gets worse
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

- Opens and fetches automatically.
- **Select** refreshes on demand.
- Refreshes itself at :05 and :35 past each hour while the app is open (NEA publishes hourly).

## Install from your phone (no SDK needed)

Every push that touches `pebble-haze/` is built by GitHub Actions
(`.github/workflows/pebble-haze.yml`). The newest build is attached to the
`pebble-haze-latest` release:

1. On your phone, open the repo's **Releases** page and tap **SG Haze (latest build)**.
2. Download `sg-haze.pbw`.
3. Open the file and choose the **Pebble** app. It installs onto the paired watch.
4. On the watch, open **SG Haze** from the app menu.

## Build

Requires the Pebble SDK (Rebble's `pebble-tool`, or the CloudPebble / Rebble dev environment).

```sh
cd pebble-haze
pebble build
pebble install --phone <phone-ip>      # or: pebble install --emulator basalt
```

Targets: aplite, basalt, chalk, diorite, emery.

## Tests

The phone-side parsing (`src/pkjs/haze.js`) is plain JS:

```sh
node test/haze.test.js
```
