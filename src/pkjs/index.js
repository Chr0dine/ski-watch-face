// Phone-side code: fetches weather + 7-day snowfall from Open-Meteo (no API key needed).
// Failed requests are retried, and the last good weather is kept so a temporary
// outage (like an HTTP 503) doesn't blank the watchface.

// Used only if the phone can't give a location. Change to your home mountain / city.
var DEFAULT_LAT = 39.7392;   // Denver
var DEFAULT_LON = -104.9903;

var RETRY_DELAYS = [5000, 20000, 60000];       // ms between attempts after a failure
var CACHE_MAX_AGE = 6 * 60 * 60 * 1000;        // ignore cached weather older than 6 hours

var CONDITIONS = {   // kept short so they fit on the watch
  0: 'Clear', 1: 'Fair', 2: 'P.Cloudy', 3: 'Overcast',
  45: 'Fog', 48: 'Fog',
  51: 'Drizzle', 53: 'Drizzle', 55: 'Drizzle',
  61: 'Rain', 63: 'Rain', 65: 'Hvy rain',
  66: 'Ice rain', 67: 'Ice rain',
  71: 'Snow', 73: 'Snow', 75: 'Hvy snow', 77: 'Snow',
  80: 'Showers', 81: 'Showers', 82: 'Hvy rain',
  85: 'Snow', 86: 'Hvy snow',
  95: 'Storm', 96: 'Storm', 99: 'Storm'
};

var inFlight = false;

// ---------- Messaging ----------

function sendStatus(msg) {
  console.log('Status: ' + msg);
  Pebble.sendAppMessage({ 'CONDITIONS': msg });
}

function sendWeather(w) {
  Pebble.sendAppMessage({
    'TEMPERATURE': w.temp,
    'CONDITIONS': w.cond,
    'SNOWFALL': w.snow            // tenths of an inch
  }, function () {
    console.log('Sent weather to watch');
  }, function () {
    console.log('Send to watch failed');
  });
}

// ---------- Cache of the last good result ----------

function loadCache() {
  try {
    var raw = localStorage.getItem('lastWeather');
    if (!raw) return null;
    var w = JSON.parse(raw);
    if (Date.now() - w.time > CACHE_MAX_AGE) return null;
    return w;
  } catch (e) {
    return null;
  }
}

function saveCache(w) {
  try {
    w.time = Date.now();
    localStorage.setItem('lastWeather', JSON.stringify(w));
  } catch (e) { /* storage unavailable: carry on without a cache */ }
}

// ---------- Fetching ----------

function xhrGet(url, onOk, onFail) {
  var req = new XMLHttpRequest();
  req.open('GET', url);
  req.timeout = 15000;
  req.onload = function () {
    if (this.status !== 200) { onFail('HTTP ' + this.status); return; }
    try {
      onOk(JSON.parse(this.responseText));
    } catch (e) {
      onFail('Bad data');
    }
  };
  req.onerror = function () { onFail('No network'); };
  req.ontimeout = function () { onFail('Timeout'); };
  req.send();
}

function fetchWeather(lat, lon, attempt) {
  var url = 'https://api.open-meteo.com/v1/forecast' +
    '?latitude=' + lat + '&longitude=' + lon +
    '&current=temperature_2m,weather_code' +
    '&daily=snowfall_sum' +
    '&temperature_unit=fahrenheit&precipitation_unit=inch' +
    '&forecast_days=7&timezone=auto';

  function fail(msg) {
    console.log('Attempt ' + (attempt + 1) + ' failed: ' + msg);
    if (attempt < RETRY_DELAYS.length) {
      setTimeout(function () { fetchWeather(lat, lon, attempt + 1); }, RETRY_DELAYS[attempt]);
      return;
    }
    // Out of retries: fall back to the last good weather, or show the error
    inFlight = false;
    var cached = loadCache();
    if (cached) { sendWeather(cached); } else { sendStatus(msg); }
  }

  xhrGet(url, function (json) {
    if (!json.current || !json.daily) { fail('Bad data'); return; }

    var total = 0;
    var days = json.daily.snowfall_sum || [];
    for (var i = 0; i < days.length; i++) { total += days[i] || 0; }

    var w = {
      temp: Math.round(json.current.temperature_2m),
      cond: CONDITIONS[json.current.weather_code] || 'Unknown',
      snow: Math.round(total * 10)
    };
    inFlight = false;
    saveCache(w);
    sendWeather(w);
  }, fail);
}

function getWeather() {
  if (inFlight) return;          // a refresh (or its retries) is already running
  inFlight = true;

  // Show the last good weather right away while refreshing
  var cached = loadCache();
  if (cached) { sendWeather(cached); } else { sendStatus('Fetching...'); }

  if (!navigator.geolocation) {
    fetchWeather(DEFAULT_LAT, DEFAULT_LON, 0);
    return;
  }
  navigator.geolocation.getCurrentPosition(
    function (pos) { fetchWeather(pos.coords.latitude, pos.coords.longitude, 0); },
    function (err) {
      console.log('Location error: ' + err.message + ', using default location');
      fetchWeather(DEFAULT_LAT, DEFAULT_LON, 0);
    },
    { timeout: 15000, maximumAge: 600000 }
  );
}

Pebble.addEventListener('ready', function () { getWeather(); });
Pebble.addEventListener('appmessage', function () { getWeather(); });
