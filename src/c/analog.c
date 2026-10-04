#include "analog.h"
#include "hands.h"
#include "main.h"

#define COMP_SIZE 17
#define ANALOG_SIZE 15
#define DATAPAGE_SIZE 42

// The data layer can have maybe 11ish characters per line, 4 lines.
// First and last are less. 64 should be plenty and then some.
#define DATA_LENGTH 64
#define PERCENT_STUB_LENGTH 9

AnalogWeather g_analog_weather;

static Layer* s_analog_layer;
static Layer* s_data_layer;

static GPath* s_hour_paths[NUM_HAND_LINES];
static GPath* s_min_paths[NUM_HAND_LINES];
static GPath* s_comp_paths[NUM_COMP_LINES];
static GPath* s_north_path;
static GPath* s_umbrella_path;

static AppTimer* timer;

typedef enum {
  Comp_Weather,
  Comp_Step,
  Comp_Batt,
  Comp_Count,
} Complication;

typedef enum {
  DataPage_Date,
  DataPage_Health,
  DataPage_Weather,
  DataPage_Forecast,
  DataPage_Device,
  DataPage_Idle // Last to count number of pages, too
} DataPage;

static struct {
  int hour_angle;
  int min_angle;
  GPoint comp_locations[Comp_Count];
  int32_t comp_angles[Comp_Count];
  BatteryChargeState battery;
  bool bluetooth_connected;
  uint32_t steps;
  CompassHeading heading;
  GPoint starfield[40];
  DataPage data_page;
  char data[DataPage_Idle][DATA_LENGTH];
  char percent_stub[DataPage_Idle][PERCENT_STUB_LENGTH]; // e.g "    100%\0"
} s_state;

static const char* WEATHER_CODES[] = {
  [0] = "Clear",
  [1] = "Mostly Clear",
  [2] = "Partly Cloudy",
  [3] = "Overcast",
  
  [45] = "Fog",
  [48] = "Fog",
  
  [51] = "Drizzle",
  [53] = "Drizzle",
  [55] = "Drizzle",
  [56] = "Frz Dizzle",
  [57] = "Frz Dizzle",
  [61] = "Light Rain",
  [63] = "Rain",
  [65] = "Heavy Rain",
  [66] = "Freezing Rain",
  [67] = "Freezing Rain",
  [80] = "Showers",
  [81] = "Showers",
  [82] = "Showers",
  
  [71] = "Light Snow",
  [73] = "Snow",
  [75] = "Heavy Snow",
  [77] = "Snow",
  [85] = "Snow Showers",
  [86] = "Snow Showers",
  
  [95] = "Thunder",
  [96] = "Hail",
  [99] = "Hail",
};

// North indication
static const GPathInfo NORTH_PATH_INFO = {
  .num_points = 4,
  .points = (GPoint []) {
    {0, 0 - DATAPAGE_SIZE},   // North Tip (Center Top)
    {6, 18 - DATAPAGE_SIZE}, // Bottom-Right wing
    {0, 12 - DATAPAGE_SIZE},  // Inner Bottom Notch
    {-6, 18 - DATAPAGE_SIZE}   // Bottom-Left wing
  }
};

// Umbrella icon
static const GPathInfo UMBRELLA_PATH_INFO = {
  .num_points = 26,
  .points = (GPoint []) {
    // Top center
    {6, 0}, 
    
    // Right curve of the canopy
    {8, 1}, {10, 2}, {11, 4}, {12, 7}, 
    
    // Right scalloped bottom edge (going inwards to the center)
    {10, 6}, {9, 7}, {7, 6}, {6, 7}, 
    
    // --- THE HANDLE ---
    // Straight down the shaft
    {6, 11}, 
    // Curve left for the hook
    {5, 12}, {3, 12}, {2, 11}, 
    // Tip of the hook
    {2, 9}, 
    // TRACE BACK: Tip down to bottom of hook
    {2, 11}, {3, 12}, {5, 12}, 
    // TRACE BACK: Straight up the shaft back to center canopy
    {6, 11}, {6, 7}, 
    
    // Left scalloped bottom edge (going outwards to the left)
    {5, 6}, {3, 7}, {2, 6}, {0, 7}, 
    
    // Left curve of the canopy (GPath auto-connects the last point to the first)
    {1, 4}, {2, 2}, {4, 1}
  }
};

const struct {
  GColor main;
  GColor bright;
  GColor dark;
  GColor c1;
  GColor c2;
  GColor c3;
  GColor c4;
  GColor c5;
  GColor c6;
  GColor c7;
  GColor c8;
  GColor c9;
} COLORS[Theme_Count] = {
  {
    .main = GColorGreen,           // 00_11_00
    .bright = GColorBrightGreen,   // 01_11_00
    .dark = GColorDarkGreen,       // 00_01_00
    .c1 = GColorMediumSpringGreen, // 00_11_10
    .c2 = GColorScreaminGreen,     // 01_11_01
    .c3 = GColorIslamicGreen,      // 00_10_00
    .c4 = GColorKellyGreen,        // 01_10_00
    .c5 = GColorMintGreen,         // 10_11_10
    .c6 = GColorInchworm,          // 10_11_01
    .c7 = GColorSpringBud,         // 10_11_00
    .c8 = GColorJaegerGreen,       // 00_10_01
    .c9 = GColorMayGreen,          // 01_10_01
  },
  {
    .main = GColorPictonBlue,
    .bright = GColorCyan,
    .dark = GColorBlue,
    .c1 = GColorVeryLightBlue,
    .c2 = GColorVividCerulean,
    .c3 = GColorDukeBlue,
    .c4 = GColorCadetBlue,
    .c5 = GColorCyan,
    .c6 = GColorCeleste,
    .c7 = GColorTiffanyBlue,
    .c8 = GColorBlueMoon,
    .c9 = GColorElectricBlue,
  },
  {
    .main = GColorRed,             // 11_00_00 2
    .bright = GColorFolly,         // 11_00_01 3
    .dark = GColorBulgarianRose,   // 01_00_00 1
    .c1 = GColorChromeYellow,      // 11_10_00 3
    .c2 = GColorSunsetOrange,      // 11_01_01 4
    .c3 = GColorDarkCandyAppleRed, // 10_00_00 1
    .c4 = GColorJazzberryJam,      // 10_00_01 2
    .c5 = GColorMelon,             // 11_10_10 4
    .c6 = GColorBrilliantRose,     // 11_01_10 4
    .c7 = GColorFashionMagenta,    // 11_00_10 3
    .c8 = GColorWindsorTan,        // 10_01_00 3
    .c9 = GColorRoseVale,          // 10_01_01 3
  }
};

#ifdef PBL_ROUND

// Get the point along a perimeter of the display
static GPoint gpoint_for_display(GRect rect, int32_t angle) {
  return gpoint_from_polar(rect, GOvalScaleModeFitCircle, angle);
}

// Get a rectangle circled around the perimeter of the display
static GRect grect_for_display(GRect rect, int32_t angle, GSize size) {
  return grect_centered_from_polar(rect, GOvalScaleModeFitCircle, angle, size);
}

#else

// Get the point along a perimeter of the display
static GPoint gpoint_for_display(GRect rect, int32_t angle) {
  int32_t hw = rect.size.w / 2;
  int32_t hh = rect.size.h / 2;
  int32_t s = sin_lookup(angle);
  int32_t c = cos_lookup(angle);
  
  int32_t dx, dy;
  
  // Cross-multiply to determine which wall the ray intersects.
  if (hw * abs(c) < hh * abs(s)) {
    // Intersects left or right vertical wall
    dx = (s > 0) ? hw : -hw;
    dy = (-c * hw) / abs(s); 
  } else {
    // Intersects top or bottom horizontal wall
    dy = (c > 0) ? -hh : hh;
    dx = (s * hh) / abs(c);
  }
  
  GPoint center = grect_center_point(&rect);
  return GPoint(center.x + dx, center.y + dy);
}

// Get a rectangle circled around the perimeter of the display
static GRect grect_for_display(GRect rect, int32_t angle, GSize size) {
  GPoint center = gpoint_for_display(rect, angle);
  return (GRect){.size = size, .origin = {center.x - size.w / 2, center.y - size.h / 2}};
}

#endif

// Draws the step progress
static void draw_step_complication(Layer* layer, GContext* ctx) {
  GRect bounds = layer_get_bounds(layer);
  
  uint32_t progress = s_state.steps % g_settings.step_goal;
  uint32_t repeat = s_state.steps / g_settings.step_goal;
  
  uint32_t angle = TRIG_MAX_ANGLE * progress / g_settings.step_goal + TRIG_MAX_ANGLE / 2;
  
  GColor line_colors[NUM_COMP_LINES] = { COLORS[g_settings.theme].c1, COLORS[g_settings.theme].bright };
  
  for ( int i = 0; i < NUM_COMP_LINES; i++ ) {
    graphics_context_set_stroke_color(ctx, line_colors[i]);
    graphics_context_set_stroke_width(ctx, 2);
    gpath_move_to(s_comp_paths[i], grect_center_point(&bounds));
    gpath_rotate_to(s_comp_paths[i], s_state.comp_angles[Comp_Step]);
    gpath_draw_outline_open(ctx, s_comp_paths[i]);
  }
  
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, s_state.comp_locations[Comp_Step], COMP_SIZE);
  
  GRect box = {
    .origin = {s_state.comp_locations[Comp_Step].x - COMP_SIZE, s_state.comp_locations[Comp_Step].y - COMP_SIZE},
    .size = {COMP_SIZE * 2 + 1, COMP_SIZE * 2 + 1}
  };
  
  if ( repeat == 0 )
    graphics_context_set_fill_color(ctx, GColorBlack);
  else if ( repeat == 1 )
    graphics_context_set_fill_color(ctx, COLORS[g_settings.theme].c3);
  else
    graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_radial(ctx, box, GOvalScaleModeFitCircle, 8, 0, TRIG_MAX_ANGLE);
  
  if ( repeat == 0 )
    graphics_context_set_fill_color(ctx, COLORS[g_settings.theme].c3);
  else if ( repeat == 1 )
    graphics_context_set_fill_color(ctx, GColorIcterine);
  else
    graphics_context_set_fill_color(ctx, GColorIcterine);
  graphics_fill_radial(ctx, box, GOvalScaleModeFitCircle, 8, TRIG_MAX_ANGLE / 2, angle);
  
  graphics_context_set_stroke_color(ctx, COLORS[g_settings.theme].c2);
  graphics_context_set_stroke_width(ctx, 1);
  
  graphics_draw_line(ctx,
    gpoint_from_polar(box, GOvalScaleModeFitCircle, TRIG_MAX_ANGLE / 2),
    gpoint_from_polar(grect_crop(box, 8), GOvalScaleModeFitCircle, TRIG_MAX_ANGLE / 2));
  
  graphics_draw_line(ctx,
    gpoint_from_polar(box, GOvalScaleModeFitCircle, angle),
    gpoint_from_polar(grect_crop(box, 8), GOvalScaleModeFitCircle, angle));
  
  graphics_draw_circle(ctx, s_state.comp_locations[Comp_Step], COMP_SIZE - 8);
  graphics_draw_circle(ctx, s_state.comp_locations[Comp_Step], COMP_SIZE);
  
  graphics_context_set_fill_color(ctx, COLORS[g_settings.theme].main);
  graphics_fill_circle(ctx, s_state.comp_locations[Comp_Step], 1);
}
  
// Draws the battery level indication
static void draw_batt_complication(Layer* layer, GContext* ctx) {
  GRect bounds = layer_get_bounds(layer);
  
  uint32_t angle = TRIG_MAX_ANGLE * s_state.battery.charge_percent / 100 + TRIG_MAX_ANGLE / 2;
  
  GColor line_colors[NUM_COMP_LINES] = { COLORS[g_settings.theme].c1, COLORS[g_settings.theme].bright };
  
  for ( int i = 0; i < NUM_COMP_LINES; i++ ) {
    graphics_context_set_stroke_color(ctx, line_colors[i]);
    graphics_context_set_stroke_width(ctx, 2);
    gpath_move_to(s_comp_paths[i], grect_center_point(&bounds));
    gpath_rotate_to(s_comp_paths[i], s_state.comp_angles[Comp_Batt]);
    gpath_draw_outline_open(ctx, s_comp_paths[i]);
  }
  
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, s_state.comp_locations[Comp_Batt], COMP_SIZE);
  
  GRect box = {
    .origin = {s_state.comp_locations[Comp_Batt].x - COMP_SIZE, s_state.comp_locations[Comp_Batt].y - COMP_SIZE},
    .size = {COMP_SIZE * 2 + 1, COMP_SIZE * 2 + 1}
  };
  
  if ( s_state.battery.is_plugged )
    graphics_context_set_fill_color(ctx, GColorIcterine);
  else
    graphics_context_set_fill_color(ctx, COLORS[g_settings.theme].c3);
  graphics_fill_radial(ctx, box, GOvalScaleModeFitCircle, 8, TRIG_MAX_ANGLE / 2, angle);
  
  graphics_context_set_stroke_color(ctx, COLORS[g_settings.theme].c2);
  graphics_context_set_stroke_width(ctx, 1);
  
  graphics_draw_line(ctx,
    gpoint_from_polar(box, GOvalScaleModeFitCircle, TRIG_MAX_ANGLE / 2),
    gpoint_from_polar(grect_crop(box, 8), GOvalScaleModeFitCircle, TRIG_MAX_ANGLE / 2));
  
  graphics_draw_line(ctx,
    gpoint_from_polar(box, GOvalScaleModeFitCircle, angle),
    gpoint_from_polar(grect_crop(box, 8), GOvalScaleModeFitCircle, angle));
  
  graphics_draw_circle(ctx, s_state.comp_locations[Comp_Batt], COMP_SIZE - 8);
  graphics_draw_circle(ctx, s_state.comp_locations[Comp_Batt], COMP_SIZE);
  
  graphics_context_set_fill_color(ctx, COLORS[g_settings.theme].main);
  graphics_fill_circle(ctx, s_state.comp_locations[Comp_Batt], 1);
}

// Draws the weather complication
static void draw_weather_complication(Layer* layer, GContext* ctx) {
  GRect bounds = layer_get_bounds(layer);
  
  GColor line_colors[NUM_COMP_LINES] = { COLORS[g_settings.theme].c1, COLORS[g_settings.theme].bright };
  
  for ( int i = 0; i < NUM_COMP_LINES; i++ ) {
    graphics_context_set_stroke_color(ctx, line_colors[i]);
    graphics_context_set_stroke_width(ctx, 2);
    gpath_move_to(s_comp_paths[i], grect_center_point(&bounds));
    gpath_rotate_to(s_comp_paths[i], s_state.comp_angles[Comp_Weather]);
    gpath_draw_outline_open(ctx, s_comp_paths[i]);
  }
  
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, s_state.comp_locations[Comp_Weather], COMP_SIZE);
  
  GRect box = {
    .origin = {s_state.comp_locations[Comp_Weather].x - COMP_SIZE, s_state.comp_locations[Comp_Weather].y - COMP_SIZE},
    .size = {COMP_SIZE * 2 + 1, COMP_SIZE * 2 + 1}
  };
  
  graphics_context_set_fill_color(ctx, COLORS[g_settings.theme].c3);
  graphics_context_set_stroke_color(ctx, COLORS[g_settings.theme].c2);
  
  if ( time(NULL) < g_analog_weather.forecast_expires &&
       g_analog_weather.record_high != g_analog_weather.record_low ) {
    // Normalize to the record high/low
    int32_t range = g_analog_weather.record_high - g_analog_weather.record_low;
    
    // Angles for todays's forecast (0 is at bottom)
    uint32_t today_start_angle = g_analog_weather.today_low <= g_analog_weather.record_low ? 0 :
      (g_analog_weather.today_low - g_analog_weather.record_low) * TRIG_MAX_ANGLE / range;
    uint32_t today_end_angle = g_analog_weather.today_high >= g_analog_weather.record_high ? TRIG_MAX_ANGLE :
      (g_analog_weather.today_high - g_analog_weather.record_low) * TRIG_MAX_ANGLE / range;

    today_start_angle += TRIG_MAX_ANGLE / 2;
    today_end_angle += TRIG_MAX_ANGLE / 2;
    
    // Fill forecasts
    graphics_fill_radial(ctx, box, GOvalScaleModeFitCircle, 8, today_start_angle, today_end_angle);
    
    // Draw outline of today's low/high
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_line(ctx,
      gpoint_from_polar(box, GOvalScaleModeFitCircle, today_start_angle),
      gpoint_from_polar(grect_crop(box, 8), GOvalScaleModeFitCircle, today_start_angle));

    graphics_draw_line(ctx,
      gpoint_from_polar(box, GOvalScaleModeFitCircle, today_end_angle),
      gpoint_from_polar(grect_crop(box, 8), GOvalScaleModeFitCircle, today_end_angle));
    
    // Draw current temperature (if not stale by more than 120 minutes)
    if ( time(NULL) < g_analog_weather.last_forecast + 120 * 60) {
      uint32_t angle = (g_analog_weather.current_temp - g_analog_weather.record_low) * TRIG_MAX_ANGLE / range + TRIG_MAX_ANGLE / 2;
      if ( g_analog_weather.current_temp < g_analog_weather.record_low ||
           g_analog_weather.current_temp > g_analog_weather.record_high )
        angle = 0 + TRIG_MAX_ANGLE / 2;

      graphics_context_set_stroke_width(ctx, 3);
      graphics_draw_line(ctx,
        grect_center_point(&box),
        gpoint_from_polar(box, GOvalScaleModeFitCircle, angle));
    }
  }
  
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_circle(ctx, s_state.comp_locations[Comp_Weather], COMP_SIZE - 8);
  graphics_draw_circle(ctx, s_state.comp_locations[Comp_Weather], COMP_SIZE);
  
  // Draw tick at bottom as a reference
  graphics_draw_line(ctx,
    gpoint_from_polar(box, GOvalScaleModeFitCircle, TRIG_MAX_ANGLE / 2),
    gpoint_from_polar(grect_crop(box, 8), GOvalScaleModeFitCircle, TRIG_MAX_ANGLE / 2));
  
  graphics_context_set_fill_color(ctx, COLORS[g_settings.theme].main);
  graphics_fill_circle(ctx, s_state.comp_locations[Comp_Weather], 1);
}

// Draws one hand
static void draw_hand(Layer* layer, GContext* ctx, GRect bounds, uint16_t angle, uint8_t quarter, GPath** paths) {
  GRect box = grect_for_display(bounds, angle, (GSize){15, 15});
  GPoint center = grect_center_point(&box);
  
  GColor line_colors[NUM_HAND_LINES] = { COLORS[g_settings.theme].c1, COLORS[g_settings.theme].bright, COLORS[g_settings.theme].dark };
  
  for ( int i = 0; i < NUM_HAND_LINES; i++ ) {
    graphics_context_set_stroke_color(ctx, line_colors[i]);
    graphics_context_set_stroke_width(ctx, 2);
    gpath_move_to(paths[i], grect_center_point(&bounds));
    gpath_rotate_to(paths[i], angle);
    gpath_draw_outline_open(ctx, paths[i]);
  }
  
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, center, 15);
  
  graphics_context_set_fill_color(ctx, COLORS[g_settings.theme].main);
  graphics_fill_circle(ctx, center, 7);
  
  graphics_context_set_fill_color(ctx, COLORS[g_settings.theme].dark);
  graphics_fill_radial(ctx, box, GOvalScaleModeFitCircle, 8, DEG_TO_TRIGANGLE(90 * quarter), DEG_TO_TRIGANGLE(90 * (quarter + 1)));
  
  graphics_context_set_stroke_color(ctx, COLORS[g_settings.theme].c4);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_circle(ctx, center, 11);
  
  graphics_context_set_stroke_color(ctx, COLORS[g_settings.theme].c5);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_circle(ctx, center, 15);
  
  graphics_context_set_stroke_color(ctx, GColorIcterine);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, (GPoint){center.x - 7, center.y}, (GPoint){center.x + 7, center.y});
  graphics_draw_line(ctx, (GPoint){center.x, center.y - 7}, (GPoint){center.x, center.y + 7});
  
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, center, 3);
  
  graphics_context_set_fill_color(ctx, COLORS[g_settings.theme].c6);
  graphics_fill_circle(ctx, center, 2);
}

// Draws the analog layer
static void analog_layer_update(Layer* layer, GContext* ctx) {
  GRect bounds = layer_get_bounds(layer);
  GPoint center = grect_center_point(&bounds);
  
  graphics_context_set_antialiased(ctx, true);
  
  // Starfield
  graphics_context_set_stroke_color(ctx, GColorWhite);
  for ( unsigned i = 0; i < sizeof(s_state.starfield) / sizeof(GPoint); i++ )
    graphics_draw_pixel(ctx, s_state.starfield[i]);
  
  // Background lines
  graphics_context_set_stroke_color(ctx, s_state.bluetooth_connected ? GColorChromeYellow : GColorDarkCandyAppleRed);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, (GPoint){center.x - 85, bounds.origin.y}, (GPoint){center.x - 85, bounds.origin.y + bounds.size.h});
  graphics_draw_line(ctx, (GPoint){center.x + 70, bounds.origin.y}, (GPoint){center.x + 70, bounds.origin.y + bounds.size.h});
  graphics_draw_line(ctx, (GPoint){center.x + 100, bounds.origin.y}, (GPoint){center.x + 100, bounds.origin.y + bounds.size.h});
  graphics_draw_line(ctx, (GPoint){bounds.origin.x, center.y - 85}, (GPoint){bounds.origin.x + bounds.size.w, center.y - 85});
  graphics_draw_line(ctx, (GPoint){bounds.origin.x, center.y + 70}, (GPoint){bounds.origin.x + bounds.size.w, center.y + 70});
  graphics_draw_line(ctx, (GPoint){bounds.origin.x, center.y + 100}, (GPoint){bounds.origin.x + bounds.size.w, center.y + 100});
  
  // Center circle
  graphics_context_set_fill_color(ctx, COLORS[g_settings.theme].main);
  graphics_fill_circle(ctx, center, 20);
  
  GRect wedge = {.size = {40, 40}};
  grect_align(&wedge, &bounds, GAlignCenter, false);
  graphics_context_set_fill_color(ctx, COLORS[g_settings.theme].dark);
  graphics_fill_radial(ctx, wedge, GOvalScaleModeFitCircle, 20, DEG_TO_TRIGANGLE(180), DEG_TO_TRIGANGLE(270));
  
  graphics_context_set_stroke_color(ctx, COLORS[g_settings.theme].c7);
  graphics_context_set_stroke_width(ctx, 7);
  graphics_draw_circle(ctx, center, 23);
  
  graphics_context_set_stroke_color(ctx, COLORS[g_settings.theme].c8);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_circle(ctx, center, 29);
  
  graphics_context_set_stroke_color(ctx, COLORS[g_settings.theme].c5);
  graphics_context_set_stroke_width(ctx, 5);
  graphics_draw_circle(ctx, center, 36);
  
  graphics_context_set_stroke_color(ctx, COLORS[g_settings.theme].main);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_circle(ctx, center, 42);
  
  // Crosshairs
  graphics_context_set_stroke_color(ctx, GColorIcterine);
  graphics_context_set_stroke_width(ctx, 7);
  graphics_draw_line(ctx, (GPoint){center.x - 50, center.y}, (GPoint){center.x + 50, center.y});
  graphics_draw_line(ctx, (GPoint){center.x, center.y - 50}, (GPoint){center.x, center.y + 50});
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_line(ctx, (GPoint){bounds.origin.x, center.y}, (GPoint){bounds.origin.x + bounds.size.w, center.y});
  graphics_draw_line(ctx, (GPoint){center.x, bounds.origin.y}, (GPoint){center.x, bounds.origin.y + bounds.size.h});
  
  // Very center
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, center, 10);
  
  graphics_context_set_stroke_color(ctx, COLORS[g_settings.theme].c9);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_circle(ctx, center, 7);
  
  graphics_context_set_fill_color(ctx, COLORS[g_settings.theme].c6);
  graphics_fill_circle(ctx, center, 3);
  
  // Hands
  draw_hand(layer, ctx, grect_crop(bounds, 45), s_state.hour_angle, 0, s_hour_paths);
  draw_hand(layer, ctx, grect_crop(bounds, ANALOG_SIZE), s_state.min_angle, 1, s_min_paths);
  
  draw_weather_complication(layer, ctx);
  draw_step_complication(layer, ctx);
  draw_batt_complication(layer, ctx);
  
  // Hour marks
  graphics_context_set_stroke_width(ctx, 3);
  graphics_context_set_stroke_color(ctx, GColorChromeYellow);
  for ( int i = 0; i < 12; i++ )
    if ( i % 3 != 0 )
      graphics_draw_line(ctx,
        gpoint_for_display(bounds, TRIG_MAX_ANGLE * i / 12),
        gpoint_for_display(grect_crop(bounds, 5), TRIG_MAX_ANGLE * i / 12));
}

// Calculates the square of the distance between two points
static int32_t dist_2(GPoint p1, GPoint p2) {
  int32_t dx = p1.x - p2.x;
  int32_t dy = p1.y - p2.y;
  return dx * dx + dy * dy;
}

// Determines where a complication should be on the display, avoiding overlap with the hour hand if needed.
// Note: the complication is placed within the radius of the minute hand to avoid conflict
static void place_complication(GRect bounds, GPoint hour_end, GPoint min_end, Complication comp) {
  s_state.comp_angles[comp] = TRIG_MAX_RATIO * comp / 3 + TRIG_MAX_ANGLE / 8;
  for ( int i = 0; i < 10; i++ ) {
    s_state.comp_locations[comp] = gpoint_from_polar(bounds, GOvalScaleModeFitCircle, s_state.comp_angles[comp]);
    if ( dist_2(s_state.comp_locations[comp], hour_end) > (COMP_SIZE+ANALOG_SIZE+2)*(COMP_SIZE+ANALOG_SIZE+2) &&
         dist_2(s_state.comp_locations[comp], min_end) > (COMP_SIZE+ANALOG_SIZE+2)*(COMP_SIZE+ANALOG_SIZE+2)
       )
      return;
    
    // Bump by 10 degrees and try again
    s_state.comp_angles[comp] = (s_state.comp_angles[comp] + DEG_TO_TRIGANGLE(10)) % TRIG_MAX_ANGLE;
  }
}

// Draws the analog layer
static void data_layer_update(Layer* layer, GContext* ctx) {
  if ( s_state.data_page == DataPage_Idle )
    return;
  
  GRect bounds = layer_get_bounds(layer);
  GPoint center = grect_center_point(&bounds);
  
  // Black out background
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, center, 42);
  
  // Calculate actual size for vertical alignment
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_18);
  GSize used = graphics_text_layout_get_content_size(s_state.data[s_state.data_page], font, bounds, GTextOverflowModeWordWrap, GTextAlignmentCenter);
  GRect content = {.size = used};
  grect_align(&content, &bounds, GAlignCenter, false);
  content.origin.y -= 4; // Remove vertical padding
  
  graphics_context_set_text_color(ctx, COLORS[g_settings.theme].main);
  graphics_draw_text(ctx, s_state.data[s_state.data_page], font, content, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  
  // Umbrella
  if ( strlen(s_state.percent_stub[s_state.data_page]) != 0 ) {
    // This is a hack since there's no umbrella emoji available.
    // Instead, we leave enough blank spaces to draw one in. To
    // get the position right, make a separate string of that
    // same line and ask for its size.  Center it and do some
    // fixed offsets from there to position the icon.
    GSize used_percent = graphics_text_layout_get_content_size(s_state.percent_stub[s_state.data_page], font, bounds, GTextOverflowModeWordWrap, GTextAlignmentCenter);
    GRect content_percent = {.size = used_percent};
    grect_align(&content_percent, &bounds, GAlignCenter, false);
    content_percent.origin.x += -2;
    content_percent.origin.y += s_state.data_page == DataPage_Weather ? -7 : -16;
    gpath_move_to(s_umbrella_path, content_percent.origin);
  
    graphics_context_set_stroke_color(ctx, COLORS[g_settings.theme].main);
    gpath_draw_outline(ctx, s_umbrella_path);
  }
    
  // Compass
  if ( s_state.data_page == DataPage_Date && s_state.heading >= 0 ) {
    graphics_context_set_fill_color(ctx, COLORS[g_settings.theme].main);
    graphics_context_set_stroke_color(ctx, COLORS[g_settings.theme].main);
    graphics_context_set_stroke_width(ctx, 1);
    
    // North
    gpath_move_to(s_north_path, center);
    gpath_rotate_to(s_north_path, TRIG_MAX_ANGLE - s_state.heading);
    gpath_draw_filled(ctx, s_north_path);
    
    // East, South, and West
    for ( int i = 1; i < 4; i++ ) {
      int32_t angle = (TRIG_MAX_ANGLE - s_state.heading) + TRIG_MAX_ANGLE * i / 4;
      graphics_draw_line(ctx, 
        gpoint_from_polar(bounds, GOvalScaleModeFitCircle, angle),
        gpoint_from_polar(grect_crop(bounds, 5), GOvalScaleModeFitCircle, angle));
    }
  }
}

// Update the device data page
static void update_device_page(void) {
  WatchInfoVersion version = watch_info_get_firmware_version();
  
  snprintf(s_state.data[DataPage_Device], DATA_LENGTH,
           "BT %s\n%s %d%%\n%zu MB\nv%d.%d.%d",
           s_state.bluetooth_connected ? "On" : "Off",
           s_state.battery.is_plugged ? "CHG" : "BAT",
           s_state.battery.charge_percent,
           heap_bytes_free(),
           version.major, version.minor, version.patch
          );
  layer_mark_dirty(s_data_layer);
}

// Rounded division
static int div_round(int32_t num, int32_t den) {
  return (num + (num > 0 ? den / 2 : -den / 2)) / den;
}

// Converts the temperature units for display
static int temp_convert(int32_t temp) {
  if ( g_settings.temp_celcius )
    return div_round(temp, 10);
  else
    return div_round(temp * 9 + 32 * 50, 50);
}

// Request weather update
static void request_weather() {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_uint8(iter, MESSAGE_KEY_WEATHER, 0);
    app_message_outbox_send();
  }
}

// Safely get the weather code
static const char* safe_weather_code(unsigned weather_code) {
  return weather_code < sizeof(WEATHER_CODES) / sizeof(*WEATHER_CODES) && WEATHER_CODES[weather_code]
    ? WEATHER_CODES[weather_code] : "(Unknown)";
}

// Handles periodic time updates
static void tick_handler(struct tm* tick_time, TimeUnits units_changed) {
  // Update the hands
  if ( clock_is_24h_style() )
    strftime(s_state.data[DataPage_Date], DATA_LENGTH, "%A\n%Y-%02m-%02d\n%H:%M:%S", tick_time);
  else
    strftime(s_state.data[DataPage_Date], DATA_LENGTH, "%A\n%Y-%02m-%02d\n%l:%M:%S %P", tick_time);

  s_state.min_angle = TRIG_MAX_RATIO * tick_time->tm_min / 60;
  s_state.hour_angle = (TRIG_MAX_RATIO * tick_time->tm_hour + s_state.min_angle) / 12;

  // Have complications dodge the hands so there's no overlap
  GRect bounds = layer_get_bounds(s_analog_layer);
  GPoint hour_end = gpoint_for_display(grect_crop(bounds, 45), s_state.hour_angle);
  GPoint min_end = gpoint_for_display(grect_crop(bounds, ANALOG_SIZE), s_state.min_angle);
  GRect box = {.size = {160, 160}};
  grect_align(&box, &bounds, GAlignCenter, false);
  for ( int i = 0; i < Comp_Count; i++ )
    place_complication(box, hour_end, min_end, i);

  // Request weather update every 30 minutes or if expired (to quickly recover after an extended blackout)
  if ( (tick_time->tm_min == 0 ||
        tick_time->tm_min == 30 ||
        g_analog_weather.record_high == g_analog_weather.record_low ||
        time(NULL) > g_analog_weather.last_forecast + 30 * 60 ||
        time(NULL) > g_analog_weather.forecast_expires) )
    request_weather();

  // Cleear the weather pages if the data is stale
  if ( time(NULL) >= g_analog_weather.forecast_expires ) {
    strcpy(s_state.data[DataPage_Weather], "Weather\nUnavailable");
    strcpy(s_state.data[DataPage_Forecast], "Forecast\nUnavailable");
  } else if ( time(NULL) >= g_analog_weather.last_forecast + 120 * 60 ) {
    snprintf(s_state.data[DataPage_Weather], DATA_LENGTH,
             "--°\n    %d%%\n%s\n%d°-%d°",
             g_analog_weather.rain_percent,
             safe_weather_code(g_analog_weather.weather),
             temp_convert(g_analog_weather.today_low),
             temp_convert(g_analog_weather.today_high));
  }

  layer_mark_dirty(s_analog_layer);
}

// Handles updates to the connection state
static void connection_handler(bool connected) {
  s_state.bluetooth_connected = connected;
  layer_mark_dirty(s_analog_layer);
  update_device_page();
}

// Handles updates to the battery level
static void battery_handler(BatteryChargeState charge_state) {
  s_state.battery = charge_state;
  layer_mark_dirty(s_analog_layer);
  update_device_page();
}

// Restores the display
static void restore_display(void* data) {
  timer = NULL;
  s_state.data_page = DataPage_Idle;
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  layer_mark_dirty(s_data_layer);
}

// Handles wrist shakes
static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
  s_state.data_page = (s_state.data_page + 1) % (DataPage_Idle + 1);
  layer_mark_dirty(s_data_layer);
  
  if ( s_state.data_page == DataPage_Date ) {
    tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
    time_t now = time(NULL);
    struct tm* timestamp = localtime(&now);
    tick_handler(timestamp, SECOND_UNIT);
  }
  else {
    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  }
  
  // Restore display after 15 second wait
  if ( !g_settings.persistent_data ) {
    if ( timer )
      app_timer_cancel(timer);
    timer = app_timer_register(15000, restore_display, NULL);
  }
}

// Handles touch events
static void touch_handler(const TouchEvent* event, void* context) {
  if ( event->type != TouchEvent_Liftoff )
    return;
  
  GRect bounds = layer_get_bounds(s_analog_layer);
  
  // Center
  if ( dist_2 ((GPoint){event->x, event->y}, grect_center_point(&bounds))  < DATAPAGE_SIZE * DATAPAGE_SIZE )
    accel_tap_handler(ACCEL_AXIS_Z, 0);
  
  // Weather complication
  if ( dist_2 ((GPoint){event->x, event->y}, s_state.comp_locations[Comp_Weather]) < COMP_SIZE * COMP_SIZE ) {
    s_state.data_page = DataPage_Health;
    accel_tap_handler(ACCEL_AXIS_Z, 0);
  }
  
  // Step complication
  if ( dist_2 ((GPoint){event->x, event->y}, s_state.comp_locations[Comp_Step]) < COMP_SIZE * COMP_SIZE ) {
    s_state.data_page = DataPage_Date;
    accel_tap_handler(ACCEL_AXIS_Z, 0);
  }  
  
  // Battery complication
  if ( dist_2 ((GPoint){event->x, event->y}, s_state.comp_locations[Comp_Batt]) < COMP_SIZE * COMP_SIZE ) {
    s_state.data_page = DataPage_Forecast;
    accel_tap_handler(ACCEL_AXIS_Z, 0);
  }  
}

// Handles updates to compass data
static void compass_handler(CompassHeadingData heading) {
  s_state.heading = heading.compass_status == CompassStatusCalibrated ? heading.magnetic_heading : -1;
  if ( s_state.data_page == DataPage_Date )
    layer_mark_dirty(s_data_layer);
}

// Handles updates to health information
static void health_handler(HealthEventType event, void* context) {
  if ( event != HealthEventSignificantUpdate &&
       event != HealthEventMovementUpdate &&
       event != HealthEventHeartRateUpdate )
    return;
  
  int hr = health_service_peek_current_value(HealthMetricHeartRateBPM);
  s_state.steps = health_service_sum_today(HealthMetricStepCount);
  int dist_m = health_service_sum_today(HealthMetricWalkedDistanceMeters);
  bool is_metric = health_service_get_measurement_system_for_display(HealthMetricWalkedDistanceMeters) == MeasurementSystemMetric;
  const char* dist_unit = is_metric ? "km" : "mi";
  
  int dist_whole;
  int dist_frac;
  
  if ( is_metric ) {
    dist_whole = dist_m / 1000;
    dist_frac = dist_m % 1000 / 10;
  } else {
    int dist_hundredths_mi = dist_m * 3125 / 50292;
    dist_whole = dist_hundredths_mi / 100;
    dist_frac = dist_hundredths_mi % 100;
  }
  
  if ( hr > 0 )
    snprintf(s_state.data[DataPage_Health], DATA_LENGTH, "%d bpm\n%d steps\n%d.%02d %s", hr, (int)s_state.steps, dist_whole, dist_frac, dist_unit);
  else
    snprintf(s_state.data[DataPage_Health], DATA_LENGTH, "-- bpm\n%d steps\n%d.%02d %s", (int)s_state.steps, dist_whole, dist_frac, dist_unit);
  
  // Update the data page if it's being shown. Let the analog layer update naturally.
  if ( s_state.data_page == DataPage_Health )
    layer_mark_dirty(s_data_layer);
}

// Utility to notify of a change in the analog weather
void analog_update_weather(void) {
  // Note: three spaces is (almost) the same width as the umbrella
  // Use | for testing
  snprintf(s_state.data[DataPage_Weather], DATA_LENGTH,
           "%d°\n    %d%%\n%s\n%d°-%d°",
           temp_convert(g_analog_weather.current_temp),
           g_analog_weather.rain_percent,
           safe_weather_code(g_analog_weather.weather),
           temp_convert(g_analog_weather.today_low),
           temp_convert(g_analog_weather.today_high));
  snprintf(s_state.data[DataPage_Forecast], DATA_LENGTH,
           "    %d%%\n%s\n%d°-%d°",
           g_analog_weather.tomorrow_percent,
           safe_weather_code(g_analog_weather.tomorrow_weather),
           temp_convert(g_analog_weather.tomorrow_low),
           temp_convert(g_analog_weather.tomorrow_high));
  snprintf(s_state.percent_stub[DataPage_Weather], PERCENT_STUB_LENGTH, "    %d%%", g_analog_weather.rain_percent);
  snprintf(s_state.percent_stub[DataPage_Forecast], PERCENT_STUB_LENGTH, "    %d%%", g_analog_weather.tomorrow_percent);
  layer_mark_dirty(s_analog_layer);
}

// Initializes the component
void analog_init(Layer* parent) {
  GRect bounds = layer_get_bounds(parent);
  
  // Read stored state
  if ( persist_exists(Keys_Forecast) )
    persist_read_data(Keys_Forecast, &g_analog_weather, sizeof(g_analog_weather));
  
  s_state.data_page = DataPage_Idle;
    
  // Create the layer
  s_analog_layer = layer_create(bounds);
  layer_add_child(parent, s_analog_layer);
  layer_set_update_proc(s_analog_layer, analog_layer_update);
  
  // Create the data layer
  GRect layer_box = {.size = {DATAPAGE_SIZE * 2, DATAPAGE_SIZE * 2}};
  grect_align(&layer_box, &bounds, GAlignCenter, false);
  s_data_layer = layer_create(layer_box);
  layer_add_child(parent, s_data_layer);
  layer_set_update_proc(s_data_layer, data_layer_update);
  
  for ( int i = 0; i < NUM_HAND_LINES; i++ ) {
    s_hour_paths[i] = gpath_create(&HAND_PATH_INFO.hour[i]);
    s_min_paths[i] = gpath_create(&HAND_PATH_INFO.min[i]);
  }
  
  for ( int i = 0; i < NUM_COMP_LINES; i++ )
    s_comp_paths[i] = gpath_create(&HAND_PATH_INFO.comp[i]);
  
  s_north_path = gpath_create(&NORTH_PATH_INFO);
  s_umbrella_path = gpath_create(&UMBRELLA_PATH_INFO);
  
  // Populate random starfield
  for ( unsigned i = 0; i < sizeof(s_state.starfield) / sizeof(GPoint); i++ )
    s_state.starfield[i] = (GPoint){rand() % 260, rand() % 260};
  
  // Register with services
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  bluetooth_connection_service_subscribe(connection_handler);
  battery_state_service_subscribe(battery_handler);
  accel_tap_service_subscribe(accel_tap_handler);
  touch_service_subscribe(touch_handler, NULL);
  health_service_events_subscribe(health_handler, NULL);
  compass_service_subscribe(compass_handler);
  //compass_service_set_heading_filter(DEG_TO_TRIGANGLE(5));
  
  analog_update_weather();
  
  // Make sure the time is displayed from the start
  time_t now = time(NULL);
  struct tm* timestamp = localtime(&now);
  tick_handler(timestamp, SECOND_UNIT);
  connection_handler(bluetooth_connection_service_peek());
  battery_handler(battery_state_service_peek());
  CompassHeadingData heading;
  compass_service_peek(&heading);
  compass_handler(heading);
}

// Tears down the component
void analog_deinit(void) {
  tick_timer_service_unsubscribe();
  bluetooth_connection_service_unsubscribe();
  battery_state_service_unsubscribe();
  accel_tap_service_unsubscribe();
  touch_service_unsubscribe();
  health_service_events_unsubscribe();
  compass_service_unsubscribe();
  
  layer_destroy(s_analog_layer);
  layer_destroy(s_data_layer);
  
  for ( int i = 0; i < NUM_HAND_LINES; i++ ) {
    gpath_destroy(s_hour_paths[i]);
    gpath_destroy(s_min_paths[i]);
  }
  
  for ( int i = 0; i < NUM_COMP_LINES; i++ )
    gpath_destroy(s_comp_paths[i]);
  
  gpath_destroy(s_north_path);
  gpath_destroy(s_umbrella_path);
}