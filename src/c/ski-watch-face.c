#include <pebble.h>
#include <stdlib.h>

static Window *s_window;
static Layer *s_canvas;

static char s_time[8] = "12:00";
static char s_date[16] = "";
static char s_temp[8] = "--";
static char s_status[32] = "Waiting for phone";   // shown only until weather arrives
static bool s_have_weather = false;
static AppTimer *s_retry_timer = NULL;
static char s_snow[16] = "--";
static char s_batt[8] = "";
static BatteryChargeState s_charge;

// What the sky should look like
typedef enum {
  SKY_CLEAR, SKY_PARTLY, SKY_OVERCAST, SKY_FOG, SKY_RAIN, SKY_SNOW, SKY_STORM
} SkyType;
static SkyType s_sky = SKY_CLEAR;

// Sunrise/sunset in minutes after local midnight (sent by the phone, saved across restarts)
#define PERSIST_SUNRISE 1
#define PERSIST_SUNSET  2
static int s_sunrise = 7 * 60;
static int s_sunset = 19 * 60;
static bool s_night = false;

// ---------- Path data (must live at file scope) ----------

// Tall mountain range, filled down to the bottom of the screen
static GPoint s_mtn_pts[] = {{0, 168}, {0, 100}, {22, 72}, {40, 92}, {66, 58},
                             {94, 94}, {118, 66}, {144, 98}, {144, 168}};
static GPathInfo s_mtn_info = { .num_points = 9, .points = s_mtn_pts };

// Snow caps on the three peaks
static GPoint s_cap1_pts[] = {{22, 72}, {15, 81}, {19, 79}, {23, 82}, {27, 79}, {30, 81}};
static GPathInfo s_cap1_info = { .num_points = 6, .points = s_cap1_pts };
static GPoint s_cap2_pts[] = {{66, 58}, {55, 72}, {60, 69}, {66, 74}, {72, 69}, {77, 72}};
static GPathInfo s_cap2_info = { .num_points = 6, .points = s_cap2_pts };
static GPoint s_cap3_pts[] = {{118, 66}, {108, 78}, {112, 76}, {118, 80}, {124, 76}, {128, 78}};
static GPathInfo s_cap3_info = { .num_points = 6, .points = s_cap3_pts };

// White snowy ground: the ski run and everything beneath it
static GPoint s_slope_pts[] = {{0, 126}, {144, 153}, {144, 168}, {0, 168}};
static GPathInfo s_slope_info = { .num_points = 4, .points = s_slope_pts };

static GPoint s_torso_pts[] = {{64, 112}, {90, 112}, {94, 88}, {70, 82}};
static GPathInfo s_torso_info = { .num_points = 4, .points = s_torso_pts };

// Lightning bolt for storms
static GPoint s_bolt_pts[] = {{100, 2}, {92, 14}, {97, 14}, {93, 25}, {105, 10},
                              {99, 10}, {103, 2}};
static GPathInfo s_bolt_info = { .num_points = 7, .points = s_bolt_pts };

// ---------- Drawing helpers ----------

static void draw_text_shadowed(GContext *ctx, const char *text, GFont font,
                               GRect box, GTextAlignment align) {
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, text, font, GRect(box.origin.x + 1, box.origin.y + 1,
                     box.size.w, box.size.h),
                     GTextOverflowModeTrailingEllipsis, align, NULL);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, text, font, box,
                     GTextOverflowModeTrailingEllipsis, align, NULL);
}

static void draw_line(GContext *ctx, int x1, int y1, int x2, int y2,
                      GColor color, int width) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, width);
  graphics_draw_line(ctx, GPoint(x1, y1), GPoint(x2, y2));
}

static void fill_path(GContext *ctx, GPathInfo *info, GColor color) {
  GPath *path = gpath_create(info);
  graphics_context_set_fill_color(ctx, color);
  gpath_draw_filled(ctx, path);
  gpath_destroy(path);
}

// Six-armed snowflake with a small shadow so it reads on the sky
static void draw_snowflake(GContext *ctx, int cx, int cy) {
  static const int8_t arms[3][2] = {{8, 0}, {4, 7}, {-4, 7}};
  for (int pass = 0; pass < 2; pass++) {
    int off = (pass == 0) ? 1 : 0;
    GColor color = (pass == 0) ? GColorBlack : GColorWhite;
    for (int i = 0; i < 3; i++) {
      draw_line(ctx, cx - arms[i][0] + off, cy - arms[i][1] + off,
                cx + arms[i][0] + off, cy + arms[i][1] + off, color, 2);
    }
    graphics_context_set_fill_color(ctx, color);
    graphics_fill_circle(ctx, GPoint(cx + off, cy + off), 2);
  }
}

// Battery icon (18x9 body + nub): white outline with a shadow, colored fill level
static void draw_battery(GContext *ctx, int x, int y) {
  int pct = s_charge.charge_percent;
  for (int pass = 0; pass < 2; pass++) {
    int off = (pass == 0) ? 1 : 0;
    GColor color = (pass == 0) ? GColorBlack : GColorWhite;
    graphics_context_set_stroke_color(ctx, color);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_rect(ctx, GRect(x + off, y + off, 18, 9));
    graphics_context_set_fill_color(ctx, color);
    graphics_fill_rect(ctx, GRect(x + 18 + off, y + 3 + off, 2, 3), 0, GCornerNone);
  }
  GColor level = s_charge.is_charging ? GColorYellow
               : (pct <= 10 ? GColorRed : (pct <= 30 ? GColorOrange : GColorGreen));
  graphics_context_set_fill_color(ctx, level);
  graphics_fill_rect(ctx, GRect(x + 2, y + 2, (14 * pct) / 100, 5), 0, GCornerNone);
}

// ---------- Sky ----------

static SkyType sky_from_wmo(int code) {
  if (code <= 1) return SKY_CLEAR;
  if (code == 2) return SKY_PARTLY;
  if (code == 3) return SKY_OVERCAST;
  if (code == 45 || code == 48) return SKY_FOG;
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return SKY_RAIN;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return SKY_SNOW;
  if (code >= 95) return SKY_STORM;
  return SKY_CLEAR;
}

static bool is_night_now(void) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  int mins = t->tm_hour * 60 + t->tm_min;
  return mins < s_sunrise || mins >= s_sunset;
}

static GColor sky_color(void) {
  if (s_night) {
    switch (s_sky) {
      case SKY_CLEAR:
      case SKY_PARTLY: return GColorOxfordBlue;
      case SKY_OVERCAST:
      case SKY_FOG:
      case SKY_SNOW:   return GColorDarkGray;
      default:         return GColorBlack;   // rain, storm
    }
  }
  switch (s_sky) {
    case SKY_OVERCAST:
    case SKY_FOG:
    case SKY_SNOW:  return GColorLightGray;
    case SKY_RAIN:  return GColorDarkGray;
    case SKY_STORM: return GColorOxfordBlue;
    default:        return GColorVividCerulean;
  }
}

// Mountains get darker under a pale sky so they stay visible
static GColor mountain_color(void) {
  if (s_night) {
    switch (s_sky) {
      case SKY_OVERCAST:
      case SKY_FOG:
      case SKY_SNOW:  return GColorBlack;
      default:        return GColorDarkGray;
    }
  }
  switch (s_sky) {
    case SKY_OVERCAST:
    case SKY_FOG:
    case SKY_SNOW:  return GColorDarkGray;
    default:        return GColorLightGray;
  }
}

static void draw_sun(GContext *ctx, int cx, int cy) {
  static const int8_t rays[8][4] = {
    {8, 0, 11, 0}, {-8, 0, -11, 0}, {0, -8, 0, -11}, {0, 8, 0, 11},
    {6, 6, 8, 8}, {-6, 6, -8, 8}, {6, -6, 8, -8}, {-6, -6, -8, -8}
  };
  for (int i = 0; i < 8; i++) {
    draw_line(ctx, cx + rays[i][0], cy + rays[i][1],
              cx + rays[i][2], cy + rays[i][3], GColorYellow, 2);
  }
  graphics_context_set_fill_color(ctx, GColorYellow);
  graphics_fill_circle(ctx, GPoint(cx, cy), 6);
}

// Crescent moon: a pale disc with a sky-colored disc cut out of it
static void draw_moon(GContext *ctx, int cx, int cy) {
  graphics_context_set_fill_color(ctx, GColorPastelYellow);
  graphics_fill_circle(ctx, GPoint(cx, cy), 8);
  graphics_context_set_fill_color(ctx, sky_color());
  graphics_fill_circle(ctx, GPoint(cx + 5, cy - 3), 7);
}

static void draw_stars(GContext *ctx) {
  static const int8_t stars[][2] = {
    {8, 24}, {30, 50}, {52, 14}, {70, 6}, {112, 46}, {136, 54},
    {18, 64}, {60, 70}, {100, 30}, {126, 74}, {44, 32}, {84, 52}
  };
  graphics_context_set_fill_color(ctx, GColorWhite);
  for (unsigned i = 0; i < ARRAY_LENGTH(stars); i++) {
    if (i % 4 == 0) {
      graphics_fill_circle(ctx, GPoint(stars[i][0], stars[i][1]), 1);
    } else {
      graphics_fill_rect(ctx, GRect(stars[i][0], stars[i][1], 1, 1), 0, GCornerNone);
    }
  }
}

// Sun by day, moon by night
static void draw_sun_or_moon(GContext *ctx, int cx, int cy) {
  if (s_night) draw_moon(ctx, cx, cy);
  else draw_sun(ctx, cx, cy);
}

static void draw_cloud(GContext *ctx, int x, int y, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_circle(ctx, GPoint(x + 7, y + 8), 6);
  graphics_fill_circle(ctx, GPoint(x + 15, y + 5), 7);
  graphics_fill_circle(ctx, GPoint(x + 23, y + 8), 6);
  graphics_fill_rect(ctx, GRect(x + 7, y + 8, 17, 6), 0, GCornerNone);
}

static void draw_sky(GContext *ctx, GRect b) {
  graphics_context_set_fill_color(ctx, sky_color());
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  // Clouds are dimmer at night
  GColor cloud = s_night ? GColorLightGray : GColorWhite;
  GColor rain_cloud = s_night ? GColorDarkGray : GColorLightGray;

  switch (s_sky) {
    case SKY_CLEAR:
      if (s_night) draw_stars(ctx);
      draw_sun_or_moon(ctx, 92, 12);
      break;
    case SKY_PARTLY:
      if (s_night) draw_stars(ctx);
      draw_sun_or_moon(ctx, 104, 10);
      draw_cloud(ctx, 68, 8, cloud);
      break;
    case SKY_OVERCAST:
      draw_cloud(ctx, 66, 4, cloud);
      draw_cloud(ctx, -4, 40, cloud);
      draw_cloud(ctx, 118, 38, cloud);
      break;
    case SKY_RAIN:
      draw_cloud(ctx, 66, 4, rain_cloud);
      draw_cloud(ctx, -4, 40, rain_cloud);
      draw_cloud(ctx, 118, 38, rain_cloud);
      for (int i = 0; i < 14; i++) {
        int x = (i * 13 + 7) % 144;
        int y = (i * 29) % 80 + 14;
        draw_line(ctx, x, y, x - 3, y + 7, GColorPictonBlue, 1);
      }
      break;
    case SKY_SNOW:
      for (int i = 0; i < 16; i++) {
        int x = (i * 19 + 5) % 144;
        int y = (i * 31) % 90 + 6;
        graphics_context_set_fill_color(ctx, GColorWhite);
        graphics_fill_circle(ctx, GPoint(x, y), (i % 3 == 0) ? 2 : 1);
      }
      break;
    case SKY_STORM:
      draw_cloud(ctx, 66, 4, GColorDarkGray);
      draw_cloud(ctx, -4, 40, GColorDarkGray);
      draw_cloud(ctx, 118, 38, GColorDarkGray);
      fill_path(ctx, &s_bolt_info, GColorYellow);
      break;
    case SKY_FOG:
      break;   // haze is drawn over the mountains in draw_skier()
  }
}

// ---------- Skier ----------

static void draw_skier(GContext *ctx) {
  // Mountains, snow caps, then all-white ground
  fill_path(ctx, &s_mtn_info, mountain_color());
  fill_path(ctx, &s_cap1_info, GColorWhite);
  fill_path(ctx, &s_cap2_info, GColorWhite);
  fill_path(ctx, &s_cap3_info, GColorWhite);

  // Fog: pale bands across the mountains
  if (s_sky == SKY_FOG) {
    graphics_context_set_fill_color(ctx, s_night ? GColorLightGray : GColorWhite);
    graphics_fill_rect(ctx, GRect(0, 84, 120, 3), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(24, 96, 120, 4), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(0, 108, 100, 3), 0, GCornerNone);
  }

  fill_path(ctx, &s_slope_info, GColorWhite);

  // Poles (tips touch the snow)
  draw_line(ctx, 106, 101, 116, 148, GColorDarkGray, 2);
  draw_line(ctx, 54, 101, 44, 134, GColorDarkGray, 2);

  // Legs (dark navy pants) stand on top of the skis; skis are drawn after
  // so the boots sit on them instead of poking through
  draw_line(ctx, 66, 134, 70, 108, GColorOxfordBlue, 6);
  draw_line(ctx, 84, 137, 80, 108, GColorOxfordBlue, 6);

  // Skis (thick)
  draw_line(ctx, 40, 130, 106, 143, GColorBlack, 5);

  // Red jacket torso, leaning forward
  fill_path(ctx, &s_torso_info, GColorRed);

  // Red jacket arms
  draw_line(ctx, 90, 90, 105, 100, GColorRed, 5);
  draw_line(ctx, 72, 88, 56, 100, GColorRed, 5);

  // Yellow leather gloves
  graphics_context_set_fill_color(ctx, GColorYellow);
  graphics_fill_circle(ctx, GPoint(106, 101), 4);
  graphics_fill_circle(ctx, GPoint(54, 101), 4);
  graphics_context_set_stroke_color(ctx, GColorWindsorTan);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_circle(ctx, GPoint(106, 101), 4);
  graphics_draw_circle(ctx, GPoint(54, 101), 4);

  // Head and hat with a pom on top (no goggles)
  graphics_context_set_fill_color(ctx, GColorMelon);
  graphics_fill_circle(ctx, GPoint(88, 77), 7);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(81, 69, 14, 5), 2, GCornersTop);
  graphics_context_set_fill_color(ctx, GColorRed);
  graphics_fill_circle(ctx, GPoint(88, 66), 3);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_circle(ctx, GPoint(88, 66), 3);
}

// ---------- Bottom row: temperature + date, centered together ----------

static void draw_bottom_row(GContext *ctx, GRect b) {
  GFont row_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  graphics_context_set_text_color(ctx, GColorBlack);

  if (!s_have_weather) {
    // Until weather arrives, show what the watch is waiting on
    graphics_draw_text(ctx, s_status, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                       GRect(0, 150, b.size.w, 18),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    return;
  }

  GRect probe = GRect(0, 0, b.size.w, 22);
  GSize ts = graphics_text_layout_get_content_size(
      s_temp, row_font, probe, GTextOverflowModeWordWrap, GTextAlignmentLeft);
  GSize ds = graphics_text_layout_get_content_size(
      s_date, row_font, probe, GTextOverflowModeWordWrap, GTextAlignmentLeft);

  const int gap = 8;
  const int nudge_left = 4;       // shift the pair left a little
  int x0 = (b.size.w - (ts.w + gap + ds.w)) / 2 - nudge_left;
  if (x0 < 0) x0 = 0;

  graphics_draw_text(ctx, s_temp, row_font, GRect(x0, 146, ts.w + 4, 22),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, s_date, row_font, GRect(x0 + ts.w + gap, 146, ds.w + 4, 22),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

// ---------- Layer update ----------

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);

  draw_sky(ctx, b);
  draw_skier(ctx);

  GFont small = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  GFont big = fonts_get_system_font(FONT_KEY_LECO_36_BOLD_NUMBERS);

  // Battery: top left, percentage to the right of the icon
  draw_battery(ctx, 4, 5);
  draw_text_shadowed(ctx, s_batt, small, GRect(27, 1, 40, 16), GTextAlignmentLeft);

  // Predicted weekly snowfall: snowflake icon with the total below, top right
  draw_snowflake(ctx, b.size.w - 14, 11);
  draw_text_shadowed(ctx, s_snow, small, GRect(70, 20, 70, 18), GTextAlignmentRight);

  // Time: middle
  draw_text_shadowed(ctx, s_time, big, GRect(0, 22, b.size.w, 44), GTextAlignmentCenter);

  // Temperature + date: bottom, black, on the snow
  draw_bottom_row(ctx, b);
}

// ---------- Battery ----------

static void battery_handler(BatteryChargeState charge) {
  s_charge = charge;
  snprintf(s_batt, sizeof(s_batt), "%d%%", charge.charge_percent);
  if (s_canvas) layer_mark_dirty(s_canvas);
}

// ---------- Time ----------

static void update_time(void) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  strftime(s_time, sizeof(s_time), clock_is_24h_style() ? "%H:%M" : "%I:%M", t);
  if (!clock_is_24h_style() && s_time[0] == '0') {
    memmove(s_time, s_time + 1, strlen(s_time));
  }
  // e.g. "Tue Oct 6" (no leading zero on the day)
  char wday_month[12];
  strftime(wday_month, sizeof(wday_month), "%a %b", t);
  snprintf(s_date, sizeof(s_date), "%s %d", wday_month, t->tm_mday);
  s_night = is_night_now();
  layer_mark_dirty(s_canvas);
}

static void request_weather(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_uint8(iter, MESSAGE_KEY_TEMPERATURE, 0);
    app_message_outbox_send();
  }
}

// Keep asking the phone until weather arrives
static void retry_weather(void *data) {
  s_retry_timer = NULL;
  if (s_have_weather) return;
  request_weather();
  s_retry_timer = app_timer_register(30000, retry_weather, NULL);
}

static void tick_handler(struct tm *t, TimeUnits changed) {
  update_time();
  if (t->tm_min % 30 == 0) request_weather();
}

// ---------- AppMessage ----------

static void inbox_received(DictionaryIterator *iter, void *ctx) {
  Tuple *temp = dict_find(iter, MESSAGE_KEY_TEMPERATURE);
  Tuple *cond = dict_find(iter, MESSAGE_KEY_CONDITIONS);
  Tuple *snow = dict_find(iter, MESSAGE_KEY_SNOWFALL);
  Tuple *rise = dict_find(iter, MESSAGE_KEY_SUNRISE);
  Tuple *set = dict_find(iter, MESSAGE_KEY_SUNSET);

  if (cond && temp) {
    // With a temperature, CONDITIONS carries the numeric weather code (as text)
    s_have_weather = true;
    snprintf(s_temp, sizeof(s_temp), "%d°", (int)temp->value->int32);
    s_sky = sky_from_wmo(atoi(cond->value->cstring));
  } else if (cond && !s_have_weather) {
    // Status / error text from the phone. Once real weather has been shown,
    // keep it on screen instead of replacing it with an error.
    snprintf(s_status, sizeof(s_status), "%s", cond->value->cstring);
  }
  if (snow) {
    int tenths = (int)snow->value->int32;
    snprintf(s_snow, sizeof(s_snow), "%d.%d\"", tenths / 10, tenths % 10);
  }
  if (rise && set) {
    s_sunrise = (int)rise->value->int32;
    s_sunset = (int)set->value->int32;
    persist_write_int(PERSIST_SUNRISE, s_sunrise);
    persist_write_int(PERSIST_SUNSET, s_sunset);
    s_night = is_night_now();
  }
  layer_mark_dirty(s_canvas);
}

static void inbox_dropped(AppMessageResult reason, void *ctx) {
  if (s_have_weather) return;
  snprintf(s_status, sizeof(s_status), "Msg dropped (%d)", (int)reason);
  layer_mark_dirty(s_canvas);
}

static void outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *ctx) {
  if (s_have_weather) return;
  if (reason == APP_MSG_NOT_CONNECTED) {
    snprintf(s_status, sizeof(s_status), "Phone not connected");
  } else {
    snprintf(s_status, sizeof(s_status), "Send failed (%d)", (int)reason);
  }
  layer_mark_dirty(s_canvas);
}

// ---------- Window ----------

static void window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
  update_time();
}

static void window_unload(Window *w) {
  layer_destroy(s_canvas);
}

static void init(void) {
  if (persist_exists(PERSIST_SUNRISE) && persist_exists(PERSIST_SUNSET)) {
    s_sunrise = persist_read_int(PERSIST_SUNRISE);
    s_sunset = persist_read_int(PERSIST_SUNSET);
  }

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load, .unload = window_unload });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_handler);
  battery_handler(battery_state_service_peek());
  app_message_register_inbox_received(inbox_received);
  app_message_register_inbox_dropped(inbox_dropped);
  app_message_register_outbox_failed(outbox_failed);
  app_message_open(128, 64);
  s_retry_timer = app_timer_register(8000, retry_weather, NULL);
}

static void deinit(void) {
  if (s_retry_timer) app_timer_cancel(s_retry_timer);
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
