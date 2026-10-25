// Parsing for NEA air-quality data published on data.gov.sg.
// Handles both the v2 (api-open.data.gov.sg) and legacy v1 (api.data.gov.sg) shapes.

var REGIONS = ['north', 'south', 'east', 'west', 'central'];
var MISSING = -1;

function latestItem(json) {
  if (!json) throw new Error('Empty response');
  if (typeof json.code === 'number' && json.code !== 0) {
    throw new Error(json.errorMsg || 'API error ' + json.code);
  }
  var root = json.data || json;
  var items = root.items;
  if (!items || !items.length) throw new Error('No readings');
  var latest = items[0];
  for (var i = 1; i < items.length; i++) {
    if ((items[i].timestamp || '') > (latest.timestamp || '')) latest = items[i];
  }
  if (!latest.readings) throw new Error('No readings');
  return latest;
}

function toInt(v) {
  // Number(null) is 0, which would read as "Good" air; treat blanks as missing.
  if (v === null || v === undefined || v === '') return MISSING;
  var n = Number(v);
  return isFinite(n) ? Math.round(n) : MISSING;
}

// Returns [north, south, east, west, central, national]. National falls back to
// the worst regional value when the API omits it.
function regional(metric) {
  if (!metric) throw new Error('Metric missing');
  var values = REGIONS.map(function (r) { return toInt(metric[r]); });
  var national = toInt(metric.national);
  if (national === MISSING) national = Math.max.apply(null, values);
  if (national === MISSING) throw new Error('No values');
  return values.concat([national]);
}

// "2026-10-24T14:00:00+08:00" -> "14:00". The API reports Singapore time, so we
// take it from the string rather than converting to the phone's time zone.
function hhmm(timestamp) {
  var m = /T(\d{2}:\d{2})/.exec(timestamp || '');
  return m ? m[1] : '--:--';
}

// Seconds since the Unix epoch (UTC), so the watch can tell how old a reading is.
function epoch(timestamp) {
  var ms = Date.parse(timestamp || '');
  return isFinite(ms) ? Math.floor(ms / 1000) : MISSING;
}

function parsePsi(json) {
  var item = latestItem(json);
  var v = regional(item.readings.psi_twenty_four_hourly);
  return {
    regions: v.slice(0, 5),
    psi: v[5],
    updated: hhmm(item.timestamp),
    epoch: epoch(item.timestamp)
  };
}

function parsePm25(json) {
  var item = latestItem(json);
  var v = regional(item.readings.pm25_one_hourly);
  return { regions: v.slice(0, 5), pm25: v[5] };
}

function toMessage(psi, pm25) {
  var pmRegions = pm25 ? pm25.regions : [MISSING, MISSING, MISSING, MISSING, MISSING];
  return {
    PSI: psi.psi,
    PM25: pm25 ? pm25.pm25 : MISSING,
    PM25_NORTH: pmRegions[0],
    PM25_SOUTH: pmRegions[1],
    PM25_EAST: pmRegions[2],
    PM25_WEST: pmRegions[3],
    PM25_CENTRAL: pmRegions[4],
    PSI_NORTH: psi.regions[0],
    PSI_SOUTH: psi.regions[1],
    PSI_EAST: psi.regions[2],
    PSI_WEST: psi.regions[3],
    PSI_CENTRAL: psi.regions[4],
    UPDATED: psi.updated,
    UPDATED_EPOCH: psi.epoch
  };
}

module.exports = {
  MISSING: MISSING,
  parsePsi: parsePsi,
  parsePm25: parsePm25,
  toMessage: toMessage,
  hhmm: hhmm,
  epoch: epoch
};
