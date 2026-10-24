#include <pebble.h>

// NEA publishes hourly; fetch a few minutes past each hour and half hour.
#define REFRESH_OFFSET_MIN 5
#define PERSIST_KEY_DATA 1
#define MISSING -1
#define NUM_REGIONS 5
// Readings older than this are flagged on screen.
#define STALE_SECONDS (2 * 60 * 60)

typedef struct {
  int32_t psi;
  int32_t pm25;
  int32_t regions[NUM_REGIONS];  // north, south, east, west, central
  char updated[8];
  int32_t epoch;                 // UTC seconds of the reading, or MISSING
  bool valid;
} HazeData;

static Window *s_window;
static TextLayer *s_title_layer;
static TextLayer *s_psi_layer;
static TextLayer *s_desc_layer;
static TextLayer *s_pm25_layer;
static TextLayer *s_region_layer;
static TextLayer *s_status_layer;

static HazeData s_data;
static char s_psi_buf[8];
static char s_pm25_buf[40];
static char s_region_buf[48];
static char s_status_buf[40];
static char s_age_buf[16];

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

// 1-hr PM2.5 (ug/m3) bands I-IV.
static const char *pm25_descriptor(int32_t pm25) {
  if (pm25 <= 55) return "Normal";
  if (pm25 <= 150) return "Elevated";
  if (pm25 <= 250) return "High";
  return "Very High";
}

static GColor band_bg(int band) {
#ifdef PBL_COLOR
  switch (band) {
    case 0: return GColorGreen;
    case 1: return GColorYellow;
    case 2: return GColorOrange;
    case 3: return GColorRed;
    default: return GColorPurple;
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

// ---- Rendering -----------------------------------------------------------

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

static void render(void) {
  if (!s_data.valid) {
    text_layer_set_text(s_psi_layer, "--");
    text_layer_set_text(s_desc_layer, "No data yet");
    text_layer_set_text(s_pm25_layer, "");
    text_layer_set_text(s_region_layer, "");
    return;
  }

  int band = psi_band(s_data.psi);
  GColor fg = band_fg(band);
  window_set_background_color(s_window, band_bg(band));
  text_layer_set_text_color(s_title_layer, fg);
  text_layer_set_text_color(s_psi_layer, fg);
  text_layer_set_text_color(s_desc_layer, fg);
  text_layer_set_text_color(s_pm25_layer, fg);
  text_layer_set_text_color(s_region_layer, fg);
  text_layer_set_text_color(s_status_layer, fg);

  snprintf(s_psi_buf, sizeof(s_psi_buf), "%d", (int)s_data.psi);
  text_layer_set_text(s_psi_layer, s_psi_buf);
  text_layer_set_text(s_desc_layer, psi_descriptor(s_data.psi));

  if (s_data.pm25 == MISSING) {
    snprintf(s_pm25_buf, sizeof(s_pm25_buf), "PM2.5 1h: n/a");
  } else {
    snprintf(s_pm25_buf, sizeof(s_pm25_buf), "PM2.5 1h: %d %s",
             (int)s_data.pm25, pm25_descriptor(s_data.pm25));
  }
  text_layer_set_text(s_pm25_layer, s_pm25_buf);

  const int32_t *r = s_data.regions;
  snprintf(s_region_buf, sizeof(s_region_buf), "N %d  S %d  C %d\nE %d  W %d",
           (int)r[0], (int)r[1], (int)r[4], (int)r[2], (int)r[3]);
  text_layer_set_text(s_region_layer, s_region_buf);
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

  int old_band = s_data.valid ? psi_band(s_data.psi) : 0;

  s_data.psi = psi->value->int32;
  s_data.pm25 = tuple_int(iter, MESSAGE_KEY_PM25);
  s_data.regions[0] = tuple_int(iter, MESSAGE_KEY_PSI_NORTH);
  s_data.regions[1] = tuple_int(iter, MESSAGE_KEY_PSI_SOUTH);
  s_data.regions[2] = tuple_int(iter, MESSAGE_KEY_PSI_EAST);
  s_data.regions[3] = tuple_int(iter, MESSAGE_KEY_PSI_WEST);
  s_data.regions[4] = tuple_int(iter, MESSAGE_KEY_PSI_CENTRAL);
  Tuple *updated = dict_find(iter, MESSAGE_KEY_UPDATED);
  snprintf(s_data.updated, sizeof(s_data.updated), "%s",
           updated ? updated->value->cstring : "--:--");
  s_data.epoch = tuple_int(iter, MESSAGE_KEY_UPDATED_EPOCH);
  s_data.valid = true;
  persist_write_data(PERSIST_KEY_DATA, &s_data, sizeof(s_data));

  // Buzz when air quality crosses into a worse band.
  if (psi_band(s_data.psi) > old_band) {
    vibes_short_pulse();
  }

  render();
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

static void click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  if (tick_time->tm_min % 30 == REFRESH_OFFSET_MIN) {
    request_refresh();
  }
}

// ---- Window --------------------------------------------------------------

static TextLayer *make_layer(Layer *root, GRect frame, const char *font_key) {
  TextLayer *layer = text_layer_create(frame);
  text_layer_set_background_color(layer, GColorClear);
  text_layer_set_text_alignment(layer, GTextAlignmentCenter);
  text_layer_set_font(layer, fonts_get_system_font(font_key));
  layer_add_child(root, text_layer_get_layer(layer));
  return layer;
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_bounds(root);
  int w = b.size.w;
  // Round screens clip the corners, so push content toward the centre.
  int top = PBL_IF_ROUND_ELSE(6, 0) + (b.size.h - 168) / 3;

  s_title_layer = make_layer(root, GRect(0, top + 2, w, 18), FONT_KEY_GOTHIC_14_BOLD);
  s_psi_layer = make_layer(root, GRect(0, top + 14, w, 50), FONT_KEY_BITHAM_42_BOLD);
  s_desc_layer = make_layer(root, GRect(0, top + 62, w, 26), FONT_KEY_GOTHIC_24_BOLD);
  s_pm25_layer = make_layer(root, GRect(0, top + 88, w, 20), FONT_KEY_GOTHIC_18);
  s_region_layer = make_layer(root, GRect(0, top + 108, w, 36), FONT_KEY_GOTHIC_14);
  s_status_layer = make_layer(root, GRect(0, b.size.h - PBL_IF_ROUND_ELSE(30, 20), w, 18),
                              FONT_KEY_GOTHIC_14);

  text_layer_set_text(s_title_layer, "SG 24-hr PSI");
  set_status(s_data.valid ? "Cached" : "Fetching...");
  render();
}

static void window_unload(Window *window) {
  text_layer_destroy(s_title_layer);
  text_layer_destroy(s_psi_layer);
  text_layer_destroy(s_desc_layer);
  text_layer_destroy(s_pm25_layer);
  text_layer_destroy(s_region_layer);
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
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
