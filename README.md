# SG Haze — Pebble watchapp

Shows Singapore's current haze status from NEA on your Pebble:

- **24-hr PSI** (national), big, with the NEA descriptor
  (Good / Moderate / Unhealthy / Very Unhealthy / Hazardous)
- **1-hr PM2.5** (worst region) with its band (Normal / Elevated / High / Very High)
- Regional 24-hr PSI for North, South, Central, East, West
- Background colour follows the PSI band on colour watches; vibrates when the band gets worse
- Last reading is cached on the watch, so it shows instantly on launch

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
