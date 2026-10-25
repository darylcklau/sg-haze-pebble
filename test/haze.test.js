// Run with: node test/haze.test.js
var assert = require('assert');
var haze = require('../src/pkjs/haze');

var regions = { west: 62, east: 58, central: 57, south: 60, north: 55 };

var v2Psi = {
  code: 0,
  errorMsg: '',
  data: {
    regionMetadata: [],
    items: [{
      date: '2026-10-24',
      timestamp: '2026-10-24T14:00:00+08:00',
      readings: { psi_twenty_four_hourly: regions, pm25_one_hourly: regions }
    }]
  }
};

var v1Psi = {
  region_metadata: [],
  items: [
    { timestamp: '2026-10-24T13:00:00+08:00',
      readings: { psi_twenty_four_hourly: Object.assign({ national: 999 }, regions) } },
    { timestamp: '2026-10-24T15:00:00+08:00',
      readings: { psi_twenty_four_hourly: Object.assign({ national: 61 }, regions) } }
  ],
  api_info: { status: 'healthy' }
};

var v2Pm25 = {
  code: 0,
  data: { items: [{ timestamp: '2026-10-24T14:00:00+08:00',
    readings: { pm25_one_hourly: { west: 12, east: 80, central: 20, south: 9, north: 11 } } }] }
};

// v2 without a national value falls back to the worst region.
var psi = haze.parsePsi(v2Psi);
assert.strictEqual(psi.psi, 62);
assert.deepStrictEqual(psi.regions, [55, 60, 58, 62, 57]);
assert.strictEqual(psi.updated, '14:00');
// 14:00 SGT (+08:00) is 06:00 UTC.
assert.strictEqual(psi.epoch, Date.UTC(2026, 9, 24, 6, 0, 0) / 1000);
assert.strictEqual(haze.epoch('garbage'), haze.MISSING);
assert.strictEqual(haze.epoch(undefined), haze.MISSING);

// v1 uses the explicit national value from the newest item.
var psi1 = haze.parsePsi(v1Psi);
assert.strictEqual(psi1.psi, 61);
assert.strictEqual(psi1.updated, '15:00');

var pm = haze.parsePm25(v2Pm25);
assert.strictEqual(pm.pm25, 80);
assert.deepStrictEqual(pm.regions, [11, 9, 80, 12, 20]);
var full = haze.toMessage(psi, pm);
assert.strictEqual(full.PM25_EAST, 80);
assert.strictEqual(full.PM25_CENTRAL, 20);

var msg = haze.toMessage(psi, null);
assert.strictEqual(msg.PM25, haze.MISSING);
assert.strictEqual(msg.PM25_NORTH, haze.MISSING);
assert.strictEqual(msg.PSI_WEST, 62);
assert.strictEqual(msg.UPDATED, '14:00');
assert.strictEqual(msg.UPDATED_EPOCH, psi.epoch);

assert.throws(function () { haze.parsePsi({ code: 4, errorMsg: 'Rate limited' }); }, /Rate limited/);
assert.throws(function () { haze.parsePsi({ data: { items: [] } }); }, /No readings/);
assert.throws(function () { haze.parsePsi({ items: [{ readings: {} }] }); }, /Metric missing/);
assert.throws(function () {
  haze.parsePsi({ items: [{ readings: { psi_twenty_four_hourly: { north: null } } }] });
}, /No values/);

// Nearest region by phone position (REGIONS order: north, south, east, west, central).
assert.strictEqual(haze.nearestRegion(1.3496, 103.9568), 2);   // Tampines -> east
assert.strictEqual(haze.nearestRegion(1.3329, 103.7436), 3);   // Jurong East -> west
assert.strictEqual(haze.nearestRegion(1.4382, 103.7890), 0);   // Woodlands -> north
assert.strictEqual(haze.nearestRegion(1.2494, 103.8303), 1);   // Sentosa -> south
assert.strictEqual(haze.nearestRegion(1.3508, 103.8484), 4);   // Bishan -> central
assert.strictEqual(haze.nearestRegion(51.5072, -0.1276), haze.MISSING);  // London
assert.strictEqual(haze.nearestRegion(1.4927, 103.7414), haze.MISSING);  // Johor Bahru

// Region centres come from the response when present (v2 and v1 shapes).
var v2Meta = { data: { regionMetadata: [
  { name: 'east', labelLocation: { latitude: 1.4, longitude: 104.0 } },
  { name: 'national', labelLocation: { latitude: 0, longitude: 0 } }
] } };
assert.deepStrictEqual(haze.regionCentres(v2Meta).east, { lat: 1.4, lon: 104.0 });
assert.strictEqual(haze.regionCentres(v2Meta).west.lon, 103.7);
var v1Meta = { region_metadata: [{ name: 'west', label_location: { latitude: 1.3, longitude: 103.6 } }] };
assert.deepStrictEqual(haze.regionCentres(v1Meta).west, { lat: 1.3, lon: 103.6 });
assert.ok(psi.centres.north);

assert.strictEqual(haze.toMessage(psi, pm, 2).REGION, 2);
assert.strictEqual(haze.toMessage(psi, pm).REGION, haze.MISSING);

console.log('All haze tests passed');
