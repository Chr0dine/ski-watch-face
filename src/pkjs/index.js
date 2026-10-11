// Phone-side code: current weather from your location, 7-day snowfall for Park City,
// both from Open-Meteo (no API key needed).
// The watch turns the weather code into a sky. Failed requests are retried, and the last good weather is kept so a temporary
// outage (like an HTTP 503) doesn't blank the watchface.

// Used only if the phone can't give a location. Change to your home mountain / city.
var DEFAULT_LAT = 39.7392;   // Denver
var DEFAULT_LON = -104.9903;

// The snow forecast always comes from Park City Mountain (not your current location).
// Coordinates are approximate; ELEVATION (meters) makes the forecast match the mountain
// rather than the valley (~2500 m is mid-mountain). Edit these to change the resort.
var SNOW_LAT = 40.6514;
var SNOW_LON = -111.5080;
var SNOW_ELEVATION = 2500;

var RETRY_DELAYS = [5000, 20000, 60000];       // ms between attempts after a failure
var CACHE_MAX_AGE = 6 * 60 * 60 * 1000;        // ignore cached weather older than 6 hours

var inFlight = false;

// ---------- Messaging ----------

function sendStatus(msg) {
  console.log('Status: ' + msg);
  Pebble.sendAppMessage({ 'CONDITIONS': msg });
}

function sendWeather(w) {
  var msg = {
    'TEMPERATURE': w.temp,
    'CONDITIONS': w.cond,
    'SNOWFALL': w.snow            // tenths of an inch
  };
  // Sunrise/sunset as minutes after local midnight (missing in older caches / polar regions)
  if (w.sunrise != null && w.sunset != null) {
    msg.SUNRISE = w.sunrise;
    msg.SUNSET = w.sunset;
  }
  Pebble.sendAppMessage(msg, function () {
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

// "2026-10-10T07:12" -> 432 (minutes after midnight), or null
function minutesOfDay(iso) {
  var m = /T(\d{2}):(\d{2})/.exec(iso || '');
  return m ? parseInt(m[1], 10) * 60 + parseInt(m[2], 10) : null;
}

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
  // Current temperature + sky, and today's sunrise/sunset: from wherever you are
  var currentUrl = 'https://api.open-meteo.com/v1/forecast' +
    '?latitude=' + lat + '&longitude=' + lon +
    '&current=temperature_2m,weather_code' +
    '&daily=sunrise,sunset&forecast_days=1' +
    '&temperature_unit=fahrenheit&timezone=auto';

  // 7-day snowfall: always for Park City, in inches
  var snowUrl = 'https://api.open-meteo.com/v1/forecast' +
    '?latitude=' + SNOW_LAT + '&longitude=' + SNOW_LON +
    '&elevation=' + SNOW_ELEVATION +
    '&daily=snowfall_sum&precipitation_unit=inch' +
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

  xhrGet(currentUrl, function (cur) {
    if (!cur.current) { fail('Bad data'); return; }

    xhrGet(snowUrl, function (snow) {
      if (!snow.daily) { fail('Bad data'); return; }

      var total = 0;
      var days = snow.daily.snowfall_sum || [];
      for (var i = 0; i < days.length; i++) { total += days[i] || 0; }

      var w = {
        temp: Math.round(cur.current.temperature_2m),
        cond: String(cur.current.weather_code),   // WMO code; the watch draws the sky from it
        snow: Math.round(total * 10),             // tenths of an inch
        sunrise: cur.daily && cur.daily.sunrise ? minutesOfDay(cur.daily.sunrise[0]) : null,
        sunset: cur.daily && cur.daily.sunset ? minutesOfDay(cur.daily.sunset[0]) : null
      };
      inFlight = false;
      saveCache(w);
      sendWeather(w);
    }, fail);
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
