var haze = require('./haze');

// Official NEA data via data.gov.sg; v1 is kept as a fallback while it still serves.
var ENDPOINTS = {
  psi: [
    'https://api-open.data.gov.sg/v2/real-time/api/psi',
    'https://api.data.gov.sg/v1/environment/psi'
  ],
  pm25: [
    'https://api-open.data.gov.sg/v2/real-time/api/pm25',
    'https://api.data.gov.sg/v1/environment/pm25'
  ]
};
var TIMEOUT_MS = 15000;
var LOCATE_TIMEOUT_MS = 10000;

function getJson(url, cb) {
  var xhr = new XMLHttpRequest();
  var done = false;
  function finish(err, data) {
    if (done) return;
    done = true;
    cb(err, data);
  }
  xhr.open('GET', url, true);
  xhr.timeout = TIMEOUT_MS;
  xhr.onload = function () {
    if (xhr.status < 200 || xhr.status >= 300) return finish(new Error('HTTP ' + xhr.status));
    try {
      finish(null, JSON.parse(xhr.responseText));
    } catch (e) {
      finish(e);
    }
  };
  xhr.onerror = function () { finish(new Error('Network error')); };
  xhr.ontimeout = function () { finish(new Error('Timeout')); };
  xhr.send();
}

// Tries each URL in turn until one returns data the parser accepts.
function fetchParsed(urls, parse, cb, i) {
  i = i || 0;
  if (i >= urls.length) return cb(new Error('All sources failed'));
  getJson(urls[i], function (err, json) {
    var result;
    if (!err) {
      try {
        result = parse(json);
      } catch (e) {
        err = e;
      }
    }
    if (err) {
      console.log('SG Haze: ' + urls[i] + ' failed: ' + err.message);
      return fetchParsed(urls, parse, cb, i + 1);
    }
    cb(null, result);
  });
}

function send(msg) {
  Pebble.sendAppMessage(msg, null, function (e) {
    console.log('SG Haze: send failed ' + JSON.stringify(e));
  });
}

// Calls back with the phone's position, or null if location is off or slow.
function locate(cb) {
  var done = false;
  function finish(coords) {
    if (done) return;
    done = true;
    cb(coords);
  }
  if (!navigator.geolocation) return finish(null);
  // Some phones never call back when permission is denied, so don't wait forever.
  setTimeout(function () { finish(null); }, LOCATE_TIMEOUT_MS + 2000);
  navigator.geolocation.getCurrentPosition(
    function (pos) { finish(pos.coords); },
    function (e) {
      console.log('SG Haze: no location (' + e.message + '), showing national reading');
      finish(null);
    },
    { enableHighAccuracy: false, timeout: LOCATE_TIMEOUT_MS, maximumAge: 15 * 60 * 1000 }
  );
}

function fetchReadings(cb) {
  fetchParsed(ENDPOINTS.psi, haze.parsePsi, function (err, psi) {
    if (err) return cb(err);
    // PM2.5 is a nice-to-have; still show PSI if it fails.
    fetchParsed(ENDPOINTS.pm25, haze.parsePm25, function (err2, pm25) {
      cb(null, psi, err2 ? null : pm25);
    });
  });
}

// Location and NEA data are fetched in parallel; the reply waits for both.
function refresh() {
  var pending = 2;
  var coords, result;
  function joined() {
    if (--pending > 0) return;
    if (result.err) return send({ ERROR: 'NEA down' });
    var region = coords
      ? haze.nearestRegion(coords.latitude, coords.longitude, result.psi.centres)
      : haze.MISSING;
    send(haze.toMessage(result.psi, result.pm25, region));
  }
  locate(function (c) { coords = c; joined(); });
  fetchReadings(function (err, psi, pm25) {
    result = { err: err, psi: psi, pm25: pm25 };
    joined();
  });
}

Pebble.addEventListener('ready', refresh);

// The watch only ever sends REQUEST, so any message means "refresh now".
Pebble.addEventListener('appmessage', refresh);
