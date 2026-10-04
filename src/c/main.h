#pragma once

#include <pebble.h>

typedef enum {
  Keys_Settings,
  Keys_Forecast,
} Keys;

typedef enum {
  Theme_Green,
  Theme_Blue,
  Theme_Red,
  Theme_Count,
} Theme;

typedef struct {
  bool persistent_data;
  bool temp_celcius;
  uint32_t step_goal;
  Theme theme;
} Settings;

extern Settings g_settings;