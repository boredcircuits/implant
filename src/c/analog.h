#pragma once

#include <pebble.h>

typedef struct {
  int32_t weather;
  int8_t rain_percent;
  int32_t tomorrow_weather;
  int8_t tomorrow_percent;
  int32_t current_temp;
  int32_t today_high;
  int32_t today_low;
  int32_t tomorrow_high;
  int32_t tomorrow_low;
  int32_t record_high;
  int32_t record_low;
  
  time_t last_forecast;
  time_t forecast_expires;
} AnalogWeather;

extern AnalogWeather g_analog_weather;

// Utility to notify of a change in the analog weather
void analog_update_weather(void);

// Initializes the component
void analog_init(Layer* parent);

// Tears down the component
void analog_deinit(void);