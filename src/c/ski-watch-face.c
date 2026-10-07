#include <pebble.h>

static Window *s_window;
static Layer *s_canvas;

static char s_time[8] = "12:00";
static char s_date[16] = "";
static char s_weather[32] = "Waiting\nfor phone";
static bool s_have_weather = false;
static AppTimer *s_retry_timer = NULL;
static char s_snow[16] = "--";
static char s_batt[8] = "";
static BatteryChargeState s_charge;

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

// Six-armed snowflake with a small shadow so it reads on the blue sky
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

// Small black battery icon (18x9 body + nub) with a fill level
static void draw_battery(GContext *ctx, int x, int y) {
  int pct = s_charge.charge_percent;
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_draw_rect(ctx, GRect(x, y, 18, 9));
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(x + 18, y + 3, 2, 3), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(x + 2, y + 2, (14 * pct) / 100, 5), 0, GCornerNone);
}

static void fill_path(GContext *ctx, GPathInfo *info, GColor color) {
  GPath *path = gpath_create(info);
  graphics_context_set_fill_color(ctx, color);
  gpath_draw_filled(ctx, path);
  gpath_destroy(path);
}

static void draw_skier(GContext *ctx) {
  // Mountains, snow caps, then all-white ground
  fill_path(ctx, &s_mtn_info, GColorLightGray);
  fill_path(ctx, &s_cap1_info, GColorWhite);
  fill_path(ctx, &s_cap2_info, GColorWhite);
  fill_path(ctx, &s_cap3_info, GColorWhite);
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

  // Head, helmet and goggles
  graphics_context_set_fill_color(ctx, GColorMelon);
  graphics_fill_circle(ctx, GPoint(88, 77), 7);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(81, 69, 14, 5), 2, GCornersTop);
  graphics_context_set_fill_color(ctx, GColorOrange);
  graphics_fill_rect(ctx, GRect(88, 75, 7, 3), 1, GCornersAll);
}

// ---------- Layer update ----------

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);

  // Blue sky
  graphics_context_set_fill_color(ctx, GColorVividCerulean);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  draw_skier(ctx);

  GFont small = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  GFont big = fonts_get_system_font(FONT_KEY_LECO_36_BOLD_NUMBERS);
  GFont date_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);

  // Weather: top left
  draw_text_shadowed(ctx, s_weather, small, GRect(4, 2, 70, 36), GTextAlignmentLeft);
  // Predicted weekly snowfall: snowflake icon with the total below, top right
  draw_snowflake(ctx, b.size.w - 14, 11);
  draw_text_shadowed(ctx, s_snow, small, GRect(70, 20, 70, 18), GTextAlignmentRight);
  // Time: middle
  draw_text_shadowed(ctx, s_time, big, GRect(0, 22, b.size.w, 44), GTextAlignmentCenter);

  // Date: bottom, black, on the mountains
  graphics_context_set_text_color(ctx, GColorBlack);
  // Battery + date sit side by side as one group, centered on the screen
  GSize ds = graphics_text_layout_get_content_size(
      s_date, date_font, GRect(0, 0, b.size.w, 22),
      GTextOverflowModeWordWrap, GTextAlignmentLeft);
  const int batt_w = 28, gap = 6;
  int x0 = (b.size.w - (ds.w + gap + batt_w)) / 2;
  int bx = x0;                       // battery on the left
  int dx = x0 + batt_w + gap;        // date to its right

  graphics_draw_text(ctx, s_date, date_font, GRect(dx, 146, ds.w + 2, 22),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  // Battery: black, icon over percent
  draw_battery(ctx, bx + 4, 149);
  graphics_draw_text(ctx, s_batt, small, GRect(bx, 155, batt_w, 14),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
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

  if (cond && temp) {
    s_have_weather = true;
    snprintf(s_weather, sizeof(s_weather), "%d°F\n%s",
             (int)temp->value->int32, cond->value->cstring);
  } else if (cond && !s_have_weather) {
    // Status / error text from the phone. Once real weather has been shown,
    // keep it on screen instead of replacing it with an error.
    snprintf(s_weather, sizeof(s_weather), "%s", cond->value->cstring);
  }
  if (snow) {
    int tenths = (int)snow->value->int32;
    snprintf(s_snow, sizeof(s_snow), "%d.%d\"", tenths / 10, tenths % 10);
  }
  layer_mark_dirty(s_canvas);
}

static void inbox_dropped(AppMessageResult reason, void *ctx) {
  if (s_have_weather) return;
  snprintf(s_weather, sizeof(s_weather), "Msg dropped\n(%d)", (int)reason);
  layer_mark_dirty(s_canvas);
}

static void outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *ctx) {
  if (s_have_weather) return;
  if (reason == APP_MSG_NOT_CONNECTED) {
    snprintf(s_weather, sizeof(s_weather), "Phone not\nconnected");
  } else {
    snprintf(s_weather, sizeof(s_weather), "Send failed\n(%d)", (int)reason);
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
