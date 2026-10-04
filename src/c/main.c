#include "main.h"
#include "analog.h"

// Default settings
Settings g_settings = {
  .persistent_data = false,
  .temp_celcius = false,
  .step_goal = 10000,
  .theme = Theme_Green,
};

static Window* s_main_window;

// Parses app messages from the phone
static void inbox_received_handler(DictionaryIterator* iter, void* context) {
  // Parse the app settings message
  Tuple* persistent_data = dict_find(iter, MESSAGE_KEY_PERSISTENT_DATA);
  if ( persistent_data ) {
    g_settings.persistent_data = persistent_data->value->int32 != 0;
    
    Tuple* temp_celsius = dict_find(iter, MESSAGE_KEY_TEMP_CELSIUS );
    if ( temp_celsius )
      g_settings.temp_celcius = temp_celsius->value->int32 != 0;

    Tuple* step_goal = dict_find(iter, MESSAGE_KEY_STEP_GOAL);
    if ( step_goal )
      g_settings.step_goal = step_goal->value->uint32;
    
    Tuple* theme = dict_find(iter, MESSAGE_KEY_THEME);
    if ( theme ) {
      if ( strcmp(theme->value->cstring, "green") == 0 )
        g_settings.theme = Theme_Green;
      else if ( strcmp(theme->value->cstring, "blue") == 0 )
        g_settings.theme = Theme_Blue;
      else if ( strcmp(theme->value->cstring, "red") == 0 )
        g_settings.theme = Theme_Red;
    }
    
    // Store the new settings and apply
    persist_write_data(Keys_Settings, &g_settings, sizeof(g_settings));
    analog_update_weather();
    layer_mark_dirty(window_get_root_layer(s_main_window));
  }
  
  // Parse the forecast message
  Tuple* weather = dict_find(iter, MESSAGE_KEY_WEATHER);
  if ( weather ) {
    AnalogWeather prev_weather = g_analog_weather;
    
    g_analog_weather.weather = weather->value->int32;
    g_analog_weather.rain_percent = dict_find(iter, MESSAGE_KEY_RAIN_PERCENT)->value->int8;
    g_analog_weather.tomorrow_weather = dict_find(iter, MESSAGE_KEY_TOMORROW_WEATHER)->value->int32;
    g_analog_weather.tomorrow_percent = dict_find(iter, MESSAGE_KEY_TOMORROW_PERCENT)->value->int8;
    g_analog_weather.current_temp = dict_find(iter, MESSAGE_KEY_CURRENT_TEMP)->value->int32;
    g_analog_weather.today_high = dict_find(iter, MESSAGE_KEY_TODAY_HIGH)->value->int32;
    g_analog_weather.today_low = dict_find(iter, MESSAGE_KEY_TODAY_LOW)->value->int32;
    g_analog_weather.tomorrow_high = dict_find(iter, MESSAGE_KEY_TOMORROW_HIGH)->value->int32;
    g_analog_weather.tomorrow_low = dict_find(iter, MESSAGE_KEY_TOMORROW_LOW)->value->int32;
    
    Tuple* record_high = dict_find(iter, MESSAGE_KEY_RECORD_HIGH);
    if ( record_high ) {
      g_analog_weather.record_high = record_high->value->int32;
      g_analog_weather.record_low = dict_find(iter, MESSAGE_KEY_RECORD_LOW)->value->int32;
    }
    
    // Update the layer if something actually changed
    if ( g_analog_weather.weather != prev_weather.weather ||
         g_analog_weather.rain_percent != prev_weather.rain_percent ||
         g_analog_weather.tomorrow_weather != prev_weather.tomorrow_weather ||
         g_analog_weather.tomorrow_percent != prev_weather.tomorrow_percent ||
         g_analog_weather.current_temp != prev_weather.current_temp ||
         g_analog_weather.today_high != prev_weather.today_high ||
         g_analog_weather.today_low != prev_weather.today_low ||
         g_analog_weather.tomorrow_high != prev_weather.tomorrow_high ||
         g_analog_weather.tomorrow_low != prev_weather.tomorrow_low ||
         g_analog_weather.record_high != prev_weather.record_high ||
         g_analog_weather.record_low != prev_weather.record_low )
      analog_update_weather();
    
    // Make the forecast expire at 2 am tomorrow. This will allow for a couple chances
    // to update before the user sees and also accounts for DST changes and the like.
    g_analog_weather.last_forecast = time(NULL);
    struct tm* tm_info = localtime(&g_analog_weather.last_forecast);
    tm_info->tm_sec = 0;
    tm_info->tm_min = 0;
    tm_info->tm_hour = 2;
    tm_info->tm_mday++;
    tm_info->tm_isdst = -1;
    g_analog_weather.forecast_expires = mktime(tm_info);
      
    // Store the new forecast
    persist_write_data(Keys_Forecast, &g_analog_weather, sizeof(g_analog_weather));
  }
}

static void main_window_load(Window* window) {
  app_message_register_inbox_received(inbox_received_handler);
  app_message_open(256, 256);
  
  Layer* window_layer = window_get_root_layer(window);
  analog_init(window_layer);
}

static void main_window_unload(Window* window) {
  analog_deinit();
}

static void init() {
  if ( persist_exists(Keys_Settings) )
    persist_read_data(Keys_Settings, &g_settings, sizeof(g_settings));
  
  // Create main Window element and assign to pointer
  s_main_window = window_create();
  window_set_background_color(s_main_window, GColorBlack);

  // Set handlers to manage the elements inside the Window
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });

  // Show the Window on the watch
  window_stack_push(s_main_window, true);
}

static void deinit() {
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
