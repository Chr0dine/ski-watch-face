// Phone-side code: fetches weather + 7-day snowfall from Open-Meteo (no API key needed).
// If something fails, a short error shows where the weather normally appears on the watch.

// Used only if the phone can't give a location. Change to your home mountain / city.
var DEFAULT_LAT = 39.7392;   // Denver
var DEFAULT_LON = -104.9903;

var CONDITIONS = {   // kept short so they fit left of the battery icon
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

function sendStatus(msg) {
  console.log('Status: ' + msg);
  Pebble.sendAppMessage({ 'CONDITIONS': msg });
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

function fetchWeather(lat, lon) {
  var url = 'https://api.open-meteo.com/v1/forecast' +
    '?latitude=' + lat + '&longitude=' + lon +
    '&current=temperature_2m,weather_code' +
    '&daily=snowfall_sum' +
    '&temperature_unit=fahrenheit&precipitation_unit=inch' +
    '&forecast_days=7&timezone=auto';

  xhrGet(url, function (json) {
    if (!json.current || !json.daily) { sendStatus('Bad data'); return; }

    var temp = Math.round(json.current.temperature_2m);
    var cond = CONDITIONS[json.current.weather_code] || 'Unknown';

    var total = 0;
    var days = json.daily.snowfall_sum || [];
    for (var i = 0; i < days.length; i++) { total += days[i] || 0; }

    Pebble.sendAppMessage({
      'TEMPERATURE': temp,
      'CONDITIONS': cond,
      'SNOWFALL': Math.round(total * 10) // tenths of an inch
    }, function () {
      console.log('Sent weather to watch');
    }, function () {
      console.log('Send to watch failed');
    });
  }, sendStatus);
}

function getWeather() {
  sendStatus('Fetching...');
  if (!navigator.geolocation) {
    fetchWeather(DEFAULT_LAT, DEFAULT_LON);
    return;
  }
  navigator.geolocation.getCurrentPosition(
    function (pos) { fetchWeather(pos.coords.latitude, pos.coords.longitude); },
    function (err) {
      console.log('Location error: ' + err.message + ', using default location');
      fetchWeather(DEFAULT_LAT, DEFAULT_LON);
    },
    { timeout: 15000, maximumAge: 600000 }
  );
}

Pebble.addEventListener('ready', function () { getWeather(); });
Pebble.addEventListener('appmessage', function () { getWeather(); });
