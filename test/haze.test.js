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

// v1 uses the explicit national value from the newest item.
var psi1 = haze.parsePsi(v1Psi);
assert.strictEqual(psi1.psi, 61);
assert.strictEqual(psi1.updated, '15:00');

assert.strictEqual(haze.parsePm25(v2Pm25).pm25, 80);

var msg = haze.toMessage(psi, null);
assert.strictEqual(msg.PM25, haze.MISSING);
assert.strictEqual(msg.PSI_WEST, 62);
assert.strictEqual(msg.UPDATED, '14:00');

assert.throws(function () { haze.parsePsi({ code: 4, errorMsg: 'Rate limited' }); }, /Rate limited/);
assert.throws(function () { haze.parsePsi({ data: { items: [] } }); }, /No readings/);
assert.throws(function () { haze.parsePsi({ items: [{ readings: {} }] }); }, /Metric missing/);
assert.throws(function () {
  haze.parsePsi({ items: [{ readings: { psi_twenty_four_hourly: { north: null } } }] });
}, /No values/);

console.log('All haze tests passed');
