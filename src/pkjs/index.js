var Clay = require('@rebble/clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig);

// Utility to fetch from a URL
const fetch = (url) => {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open('GET', url, true);
    xhr.timeout = 15000; // 15 second timeout

    xhr.onload = () => {
      const response = {
        ok: xhr.status >= 200 && xhr.status < 300,
        status: xhr.status,
        statusText: xhr.statusText,
        json: () => {
          try {
            return Promise.resolve(JSON.parse(xhr.responseText));
          } catch (e) {
            return Promise.reject(new Error("Failed to parse response JSON"));
          }
        },
        text: () => Promise.resolve(xhr.responseText)
      };
      resolve(response);
    };

    xhr.onerror = () => reject(new Error('Network request failed'));
    xhr.ontimeout = () => reject(new Error('Network request timed out'));

    xhr.send();
  });
};

// Get the weather from Open Meteo
const fetchWeather = (pos) => {
  const now = new Date();
  let max = -Infinity;
  let min = Infinity;
  let sequence = Promise.resolve();
  
  // Fetch the historical min/max for the last 10 years
  for (let i = 1; i <= 10; i++) {
    // Get a week of data for each year
    const startDate = new Date(now.getTime());
    startDate.setFullYear(now.getFullYear() - i);

    const endDate = new Date(startDate.getTime());
    endDate.setDate(startDate.getDate() + 6);

    // Get the data after the previous request was received
    sequence = sequence
      .then(() => fetch(
        'https://archive-api.open-meteo.com/v1/archive?' +
        'latitude=' + pos.coords.latitude +
        '&longitude=' + pos.coords.longitude +
        '&start_date=' + startDate.toLocaleDateString('en-CA') +
        '&end_date=' + endDate.toLocaleDateString('en-CA') +
        '&daily=temperature_2m_max,temperature_2m_min' +
        '&timezone=auto'))
      .then(res => res.ok ? res.json() : null)
      .catch(() => null)
      .then(data => {
        if ( data && data.daily ) {
          // Filter out nulls
          const validMax = (data.daily.temperature_2m_max || []).filter(v => v !== null);
          const validMin = (data.daily.temperature_2m_min || []).filter(v => v !== null);

          // Use the spread operator to find extremes
          max = Math.max(max, ...validMax);
          min = Math.min(min, ...validMin);
        }
      });
  }

  // Fetch the forecast and current temperature
  sequence = sequence
    .then(() => fetch(
      'https://api.open-meteo.com/v1/forecast?' +
      'latitude=' + pos.coords.latitude +
      '&longitude=' + pos.coords.longitude +
      '&current=temperature_2m' +
      '&daily=temperature_2m_max,temperature_2m_min,weather_code,precipitation_probability_max' +
      '&timezone=auto' +
      '&forecast_days=2'))
    .then(res => res.ok ? res.json() : null)
    .then(data => {
      if ( data && data.daily ) {
        const msg = {
          'WEATHER': data.daily.weather_code[0],
          'RAIN_PERCENT': data.daily.precipitation_probability_max[0],
          'TOMORROW_WEATHER': data.daily.weather_code[1],
          'TOMORROW_PERCENT': data.daily.precipitation_probability_max[1],
          'CURRENT_TEMP': Math.round(data.current.temperature_2m * 10),
          'TODAY_HIGH': Math.round(data.daily.temperature_2m_max[0] * 10),
          'TODAY_LOW': Math.round(data.daily.temperature_2m_min[0] * 10),
          'TOMORROW_HIGH': Math.round(data.daily.temperature_2m_max[1] * 10),
          'TOMORROW_LOW': Math.round(data.daily.temperature_2m_min[1] * 10),
        };
        
        if ( max !== -Infinity && min != Infinity ) {
          msg['RECORD_HIGH'] = Math.round(max * 10),
          msg['RECORD_LOW'] = Math.round(min * 10)
        }
        
        Pebble.sendAppMessage(msg);
      }
    })
    .catch(() => console.log("Unexpected failure processing weather:", err));
}

// Get weather data
const getWeather = (event) => {
  navigator.geolocation.getCurrentPosition(
    fetchWeather,
    err => console.error(`Location error: ${err.message}`),
    { timeout: 15000, maximumAge: 60000 }
  );
};

Pebble.addEventListener('ready', getWeather);
Pebble.addEventListener('appmessage', getWeather);