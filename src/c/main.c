#include <pebble.h>

// NEA publishes hourly; fetch a few minutes past each hour and half hour.
#define REFRESH_OFFSET_MIN 5
#define PERSIST_KEY_DATA 1
#define MISSING -1
#define NUM_REGIONS 5
// Readings older than this are flagged on screen.
#define STALE_SECONDS (2 * 60 * 60)

// Font sizes per screen. The Time 2 (emery) gets the biggest numbers.
#if PBL_DISPLAY_HEIGHT >= 228
#define VALUE_FONT FONT_KEY_ROBOTO_BOLD_SUBSET_49
#define VALUE_H 58
#define VALUE_DY -10
#define CAPTION_FONT FONT_KEY_GOTHIC_18_BOLD
#define CAPTION_H 22
#define DESC_FONT FONT_KEY_GOTHIC_24_BOLD
#define DESC_H 28
#define STATUS_FONT FONT_KEY_GOTHIC_18_BOLD
#define STATUS_H 24
#define TABLE_FONT FONT_KEY_GOTHIC_24_BOLD
#else
#define VALUE_FONT FONT_KEY_LECO_36_BOLD_NUMBERS
#define VALUE_H 42
#define VALUE_DY -4
#define CAPTION_FONT FONT_KEY_GOTHIC_14_BOLD
#define CAPTION_H 16
#define DESC_FONT FONT_KEY_GOTHIC_18_BOLD
#define DESC_H 22
#define STATUS_FONT FONT_KEY_GOTHIC_14_BOLD
#define STATUS_H 20
#define TABLE_FONT FONT_KEY_GOTHIC_18_BOLD
#endif

typedef struct {
  int32_t psi;
  int32_t pm25;
  int32_t psi_regions[NUM_REGIONS];   // north, south, east, west, central
  int32_t pm25_regions[NUM_REGIONS];  // same order
  char updated[8];
  int32_t epoch;                      // UTC seconds of the reading, or MISSING
  bool valid;
} HazeData;

// One half of the main screen: PSI on top, PM2.5 below, each with equal space.
typedef struct {
  Layer *bg;
  TextLayer *caption;
  TextLayer *value;
  TextLayer *desc;
  int band;
  char value_buf[8];
} Panel;

enum { PANEL_PSI, PANEL_PM25, NUM_PANELS };

static Window *s_window;
static Panel s_panels[NUM_PANELS];
static TextLayer *s_status_layer;

static Window *s_region_window;
static TextLayer *s_region_title;
static TextLayer *s_region_cols[3];

static HazeData s_data;
static char s_status_buf[40];
static char s_age_buf[16];
static char s_region_bufs[3][96];

static const char *REGION_NAMES[NUM_REGIONS] = {"North", "South", "East", "West", "Central"};

// ---- NEA bands -----------------------------------------------------------

// 24-hr PSI: 0 Good, 1 Moderate, 2 Unhealthy, 3 Very unhealthy, 4 Hazardous.
static int psi_band(int32_t psi) {
  if (psi <= 50) return 0;
  if (psi <= 100) return 1;
  if (psi <= 200) return 2;
  if (psi <= 300) return 3;
  return 4;
}

static const char *psi_descriptor(int32_t psi) {
  static const char *names[] = {"Good", "Moderate", "Unhealthy", "Very Unhealthy", "Hazardous"};
  return names[psi_band(psi)];
}

// 1-hr PM2.5 (ug/m3): 0 Normal, 1 Elevated, 2 High, 3 Very High (NEA bands I-IV).
static int pm25_band(int32_t pm25) {
  if (pm25 <= 55) return 0;
  if (pm25 <= 150) return 1;
  if (pm25 <= 250) return 2;
  return 3;
}

static const char *pm25_descriptor(int32_t pm25) {
  static const char *names[] = {"Normal", "Elevated", "High", "Very High"};
  return names[pm25_band(pm25)];
}

// Bands share colours: green, yellow, orange, red, purple. -1 means no data.
static GColor band_bg(int band) {
#ifdef PBL_COLOR
  switch (band) {
    case 0: return GColorGreen;
    case 1: return GColorYellow;
    case 2: return GColorOrange;
    case 3: return GColorRed;
    case 4: return GColorPurple;
    default: return GColorLightGray;
  }
#else
  return GColorWhite;
#endif
}

static GColor band_fg(int band) {
#ifdef PBL_COLOR
  return band >= 3 ? GColorWhite : GColorBlack;
#else
  return GColorBlack;
#endif
}

// ---- Status line ---------------------------------------------------------

static bool is_stale(void) {
  return s_data.epoch == MISSING || time(NULL) - s_data.epoch > STALE_SECONDS;
}

// "14:00" while fresh, then "5h old" / "3d old" so old data never looks current.
static const char *age_label(void) {
  if (!is_stale()) {
    return s_data.updated;
  }
  if (s_data.epoch == MISSING) {
    return "age unknown";
  }
  int hours = (int)((time(NULL) - s_data.epoch) / 3600);
  if (hours < 48) {
    snprintf(s_age_buf, sizeof(s_age_buf), "%dh old", hours);
  } else {
    snprintf(s_age_buf, sizeof(s_age_buf), "%dd old", hours / 24);
  }
  return s_age_buf;
}

// Shows a status message, plus the age of the cached reading when there is one.
static void set_status(const char *text) {
  if (s_data.valid) {
    snprintf(s_status_buf, sizeof(s_status_buf), "%s - %s", text, age_label());
  } else {
    snprintf(s_status_buf, sizeof(s_status_buf), "%s", text);
  }
  text_layer_set_text(s_status_layer, s_status_buf);
}

// ---- Main screen ---------------------------------------------------------

static void panel_update_proc(Layer *layer, GContext *ctx) {
  Panel *panel = &s_panels[*(int *)layer_get_data(layer)];
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, band_bg(panel->band));
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
#ifndef PBL_COLOR
  // Without colour, a rule keeps the two readings visually separate.
  if (*(int *)layer_get_data(layer) == PANEL_PSI) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect(0, bounds.size.h - 2, bounds.size.w, 2), 0, GCornerNone);
  }
#endif
}

static void set_panel(Panel *panel, int32_t value, int band, const char *desc) {
  panel->band = value == MISSING ? -1 : band;
  GColor fg = band_fg(panel->band);
  text_layer_set_text_color(panel->caption, fg);
  text_layer_set_text_color(panel->value, fg);
  text_layer_set_text_color(panel->desc, fg);
  if (value == MISSING) {
    // The number fonts only contain digits, so show nothing rather than "--".
    panel->value_buf[0] = '\0';
    text_layer_set_text(panel->desc, "No data");
  } else {
    snprintf(panel->value_buf, sizeof(panel->value_buf), "%d", (int)value);
    text_layer_set_text(panel->desc, desc);
  }
  text_layer_set_text(panel->value, panel->value_buf);
  layer_mark_dirty(panel->bg);
}

static void render(void) {
  if (!s_data.valid) {
    set_panel(&s_panels[PANEL_PSI], MISSING, 0, NULL);
    set_panel(&s_panels[PANEL_PM25], MISSING, 0, NULL);
    return;
  }
  set_panel(&s_panels[PANEL_PSI], s_data.psi, psi_band(s_data.psi), psi_descriptor(s_data.psi));
  set_panel(&s_panels[PANEL_PM25], s_data.pm25, pm25_band(s_data.pm25),
            s_data.pm25 == MISSING ? NULL : pm25_descriptor(s_data.pm25));
}

static TextLayer *make_text(Layer *parent, GRect frame, const char *font_key,
                            GTextAlignment align) {
  TextLayer *layer = text_layer_create(frame);
  text_layer_set_background_color(layer, GColorClear);
  text_layer_set_text_alignment(layer, align);
  text_layer_set_font(layer, fonts_get_system_font(font_key));
  layer_add_child(parent, text_layer_get_layer(layer));
  return layer;
}

// text_top is where the text starts inside the panel (round screens push it inward).
static void panel_create(Panel *panel, int index, Layer *root, GRect frame, int text_top,
                         int text_h, const char *caption) {
  panel->bg = layer_create_with_data(frame, sizeof(int));
  *(int *)layer_get_data(panel->bg) = index;
  layer_set_update_proc(panel->bg, panel_update_proc);
  layer_add_child(root, panel->bg);

  int w = frame.size.w;
  panel->caption = make_text(panel->bg, GRect(0, text_top, w, CAPTION_H), CAPTION_FONT,
                             GTextAlignmentCenter);
  panel->value = make_text(panel->bg, GRect(0, text_top + CAPTION_H + VALUE_DY, w, VALUE_H),
                           VALUE_FONT, GTextAlignmentCenter);
  panel->desc = make_text(panel->bg, GRect(0, text_top + text_h - DESC_H, w, DESC_H), DESC_FONT,
                          GTextAlignmentCenter);
  text_layer_set_text(panel->caption, caption);
}

static void panel_destroy(Panel *panel) {
  text_layer_destroy(panel->caption);
  text_layer_destroy(panel->value);
  text_layer_destroy(panel->desc);
  layer_destroy(panel->bg);
}

// ---- Region screen -------------------------------------------------------

static void append_value(char *buf, size_t size, int32_t value) {
  size_t len = strlen(buf);
  if (value == MISSING) {
    snprintf(buf + len, size - len, "\n-");
  } else {
    snprintf(buf + len, size - len, "\n%d", (int)value);
  }
}

static void render_regions(void) {
  if (!s_region_window || !window_is_loaded(s_region_window)) return;
  snprintf(s_region_bufs[0], sizeof(s_region_bufs[0]), "Region");
  snprintf(s_region_bufs[1], sizeof(s_region_bufs[1]), "PSI");
  snprintf(s_region_bufs[2], sizeof(s_region_bufs[2]), "PM2.5");
  for (int i = 0; i < NUM_REGIONS; i++) {
    size_t len = strlen(s_region_bufs[0]);
    snprintf(s_region_bufs[0] + len, sizeof(s_region_bufs[0]) - len, "\n%s", REGION_NAMES[i]);
    append_value(s_region_bufs[1], sizeof(s_region_bufs[1]),
                 s_data.valid ? s_data.psi_regions[i] : MISSING);
    append_value(s_region_bufs[2], sizeof(s_region_bufs[2]),
                 s_data.valid ? s_data.pm25_regions[i] : MISSING);
  }
  for (int i = 0; i < 3; i++) {
    text_layer_set_text(s_region_cols[i], s_region_bufs[i]);
  }
}

static void region_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_bounds(root);
  int w = b.size.w;
  int top = PBL_IF_ROUND_ELSE(18, 0);
  int inset = PBL_IF_ROUND_ELSE(18, 4);

  s_region_title = make_text(root, GRect(0, top, w, CAPTION_H + 4), DESC_FONT,
                             GTextAlignmentCenter);
  text_layer_set_text(s_region_title, "By region");

  GRect table = GRect(inset, top + CAPTION_H + 8, w - 2 * inset, b.size.h - top - CAPTION_H - 8);
  int name_w = table.size.w * 46 / 100;
  int col_w = (table.size.w - name_w) / 2;
  s_region_cols[0] = make_text(root, GRect(table.origin.x, table.origin.y, name_w, table.size.h),
                               TABLE_FONT, GTextAlignmentLeft);
  s_region_cols[1] = make_text(root, GRect(table.origin.x + name_w, table.origin.y, col_w,
                                           table.size.h), TABLE_FONT, GTextAlignmentRight);
  s_region_cols[2] = make_text(root, GRect(table.origin.x + name_w + col_w, table.origin.y, col_w,
                                           table.size.h), TABLE_FONT, GTextAlignmentRight);
  render_regions();
}

static void region_window_unload(Window *window) {
  text_layer_destroy(s_region_title);
  for (int i = 0; i < 3; i++) {
    text_layer_destroy(s_region_cols[i]);
  }
}

static void show_regions(void) {
  if (!s_region_window) {
    s_region_window = window_create();
    window_set_window_handlers(s_region_window, (WindowHandlers) {
      .load = region_window_load,
      .unload = region_window_unload,
    });
  }
  window_stack_push(s_region_window, true);
}

// ---- Phone communication -------------------------------------------------

static void request_refresh(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) {
    set_status("Busy");
    return;
  }
  dict_write_int8(iter, MESSAGE_KEY_REQUEST, 1);
  app_message_outbox_send();
  set_status("Refreshing");
}

static int32_t tuple_int(DictionaryIterator *iter, uint32_t key) {
  Tuple *t = dict_find(iter, key);
  return t ? t->value->int32 : MISSING;
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *err = dict_find(iter, MESSAGE_KEY_ERROR);
  if (err) {
    set_status(err->value->cstring);
    return;
  }

  Tuple *psi = dict_find(iter, MESSAGE_KEY_PSI);
  if (!psi) return;

  int old_psi_band = s_data.valid ? psi_band(s_data.psi) : 0;
  int old_pm25_band = s_data.valid && s_data.pm25 != MISSING ? pm25_band(s_data.pm25) : 0;

  s_data.psi = psi->value->int32;
  s_data.pm25 = tuple_int(iter, MESSAGE_KEY_PM25);
  s_data.psi_regions[0] = tuple_int(iter, MESSAGE_KEY_PSI_NORTH);
  s_data.psi_regions[1] = tuple_int(iter, MESSAGE_KEY_PSI_SOUTH);
  s_data.psi_regions[2] = tuple_int(iter, MESSAGE_KEY_PSI_EAST);
  s_data.psi_regions[3] = tuple_int(iter, MESSAGE_KEY_PSI_WEST);
  s_data.psi_regions[4] = tuple_int(iter, MESSAGE_KEY_PSI_CENTRAL);
  s_data.pm25_regions[0] = tuple_int(iter, MESSAGE_KEY_PM25_NORTH);
  s_data.pm25_regions[1] = tuple_int(iter, MESSAGE_KEY_PM25_SOUTH);
  s_data.pm25_regions[2] = tuple_int(iter, MESSAGE_KEY_PM25_EAST);
  s_data.pm25_regions[3] = tuple_int(iter, MESSAGE_KEY_PM25_WEST);
  s_data.pm25_regions[4] = tuple_int(iter, MESSAGE_KEY_PM25_CENTRAL);
  Tuple *updated = dict_find(iter, MESSAGE_KEY_UPDATED);
  snprintf(s_data.updated, sizeof(s_data.updated), "%s",
           updated ? updated->value->cstring : "--:--");
  s_data.epoch = tuple_int(iter, MESSAGE_KEY_UPDATED_EPOCH);
  s_data.valid = true;
  persist_write_data(PERSIST_KEY_DATA, &s_data, sizeof(s_data));

  // Buzz when either reading crosses into a worse band.
  bool worse = psi_band(s_data.psi) > old_psi_band ||
               (s_data.pm25 != MISSING && pm25_band(s_data.pm25) > old_pm25_band);
  if (worse) {
    vibes_short_pulse();
  }

  render();
  render_regions();
  // NEA itself can lag, so a fresh fetch may still carry an old reading.
  set_status(is_stale() ? "NEA data" : "Updated");
}

static void outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  set_status("No phone");
}

// ---- Input and timers ----------------------------------------------------

static void select_click(ClickRecognizerRef recognizer, void *context) {
  request_refresh();
}

static void down_click(ClickRecognizerRef recognizer, void *context) {
  show_regions();
}

static void click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  if (tick_time->tm_min % 30 == REFRESH_OFFSET_MIN) {
    request_refresh();
  }
}

// ---- Main window ---------------------------------------------------------

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_bounds(root);
  int w = b.size.w;
  // Round screens clip the top and bottom, so keep text away from the edges.
  int inset = PBL_IF_ROUND_ELSE(10, 0);
  int panel_h = (b.size.h - STATUS_H - 2 * inset) / 2;

  panel_create(&s_panels[PANEL_PSI], PANEL_PSI, root, GRect(0, 0, w, inset + panel_h),
               inset, panel_h, "24-hr PSI");
  panel_create(&s_panels[PANEL_PM25], PANEL_PM25, root, GRect(0, inset + panel_h, w, panel_h),
               0, panel_h, "1-hr PM2.5");

  int status_y = inset + 2 * panel_h;
  s_status_layer = make_text(root, GRect(0, status_y, w, b.size.h - status_y), STATUS_FONT,
                             GTextAlignmentCenter);
  text_layer_set_background_color(s_status_layer, GColorBlack);
  text_layer_set_text_color(s_status_layer, GColorWhite);

  set_status(s_data.valid ? "Cached" : "Fetching...");
  render();
}

static void window_unload(Window *window) {
  for (int i = 0; i < NUM_PANELS; i++) {
    panel_destroy(&s_panels[i]);
  }
  text_layer_destroy(s_status_layer);
}

static void init(void) {
  if (persist_exists(PERSIST_KEY_DATA) &&
      persist_read_data(PERSIST_KEY_DATA, &s_data, sizeof(s_data)) == (int)sizeof(s_data)) {
    s_data.updated[sizeof(s_data.updated) - 1] = '\0';
  } else {
    s_data.valid = false;
  }

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_click_config_provider(s_window, click_config);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  app_message_register_inbox_received(inbox_received);
  app_message_register_outbox_failed(outbox_failed);
  app_message_open(256, 64);

  // The phone fetches on its own as soon as PebbleKit JS is ready.
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  if (s_region_window) {
    window_destroy(s_region_window);
  }
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
