#pragma once

#include <pebble.h>

#define NUM_HAND_LINES 3
#define NUM_COMP_LINES 2

extern const struct HandPathInfo {
  GPathInfo hour[NUM_HAND_LINES];
  GPathInfo min[NUM_HAND_LINES];
  GPathInfo comp[NUM_COMP_LINES];
} HAND_PATH_INFO;