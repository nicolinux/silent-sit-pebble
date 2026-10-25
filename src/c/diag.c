#include "diag.h"

#ifdef MARKER_DIAG

#include "app.h"

// Persisted history of the last DIAG_SESSIONS sessions. One packed slot per
// session under keys PKEY_DIAG_SLOT0 .. +9 (a ring buffer), plus PKEY_DIAG_NEWEST
// holding the index of the newest slot. A slot is a header (start time, the
// session's config) followed by up to DIAG_MAX records:
//   header 9 B + 27 records * 9 B = 252 B, within the 256 B
//   PERSIST_DATA_MAX_LENGTH limit.
// The slot is rewritten on every record (and every answer) so a crash or forced
// exit still leaves the log. A full slot drops its oldest *marker* record but
// keeps START. Slots are always written at full size, so a value of any other
// size (e.g. ticket 04's old single-session log) is treated as empty.
#define PKEY_DIAG_SLOT0  100
#define PKEY_DIAG_NEWEST 110
#define DIAG_SESSIONS    10
#define DIAG_MAX         27

// Kinds are open-ended on purpose. D_TEST is a vibration self-test result (its
// own one-record slot): elapsed_s = pulses felt, due_s = pulses played,
// still_min = the pattern tested (D_TICK / D_HALF / D_END).
typedef enum { D_START, D_TICK, D_HALF, D_END, D_STOP, D_ABORT, D_TEST } DiagKind;

typedef struct __attribute__((packed)) {
  uint16_t elapsed_s;   // when it was delivered
  uint16_t due_s;       // its scheduled second (lateness = elapsed - due)
  uint8_t  kind;        // DiagKind
  uint8_t  felt;        // 0 = not sure / not asked, 1 = yes, 2 = no
  uint8_t  battery;     // percent; bit 7 = charging
  uint8_t  flags;       // bit 0 = Quiet Time active, bit 1 = phone connected
  uint8_t  still_min;   // consecutive motionless minutes (firmware Stand-By heuristic)
} DiagRecord;

#define FLAG_QUIET 0x01
#define FLAG_PHONE 0x02
#define FELT_YES   1
#define FELT_NO    2

typedef struct __attribute__((packed)) {
  int32_t    start;      // wall-clock time of the session start
  uint8_t    cfg[4];     // duration_min, interval_min, half_enabled, vibe_strength
  uint8_t    count;
  DiagRecord rec[DIAG_MAX];
} DiagSlot;

_Static_assert(sizeof(DiagSlot) <= PERSIST_DATA_MAX_LENGTH, "diag slot exceeds one persist value");

static DiagSlot s_slot;      // the current (or just-stopped) session
static int      s_slot_idx;  // its ring index
static bool     s_stopped;   // STOP recorded for the current session

// --- Stillness (replica of the firmware's Stand-By Mode heuristic) -------------
// accel_service_peek() is documented as unusable while subscribed (returns -2),
// and unsubscribed it only works if something else keeps the accelerometer on. So
// subscribe with the largest batch at the lowest rate (a wake-up every 2.5 s) and
// just remember the latest sample; the once-a-minute check compares it to the
// previous minute's.
#define STILL_THRESHOLD 50   // sum of |dx|+|dy|+|dz| between one-minute samples

static bool      s_accel_on;
static AccelData s_last;
static bool      s_have_last, s_have_prev;
static int16_t   s_prev_x, s_prev_y, s_prev_z;
static int       s_still;

static void accel_handler(AccelData *data, uint32_t num_samples) {
  if (num_samples == 0) return;
  s_last = data[num_samples - 1];
  s_have_last = true;
}

static void accel_start(void) {
  s_still = 0;
  s_have_last = s_have_prev = false;
  if (s_accel_on) return;
  accel_data_service_subscribe(25, accel_handler);
  accel_service_set_sampling_rate(ACCEL_SAMPLING_10HZ);
  s_accel_on = true;
}

static void accel_stop(void) {
  if (!s_accel_on) return;
  accel_data_service_unsubscribe();
  s_accel_on = false;
}

void diag_minute(void) {
  if (!s_accel_on || !s_have_last) return;
  if (s_have_prev) {
    int d = abs(s_last.x - s_prev_x) + abs(s_last.y - s_prev_y) + abs(s_last.z - s_prev_z);
    s_still = (d < STILL_THRESHOLD) ? s_still + 1 : 0;
  }
  s_prev_x = s_last.x; s_prev_y = s_last.y; s_prev_z = s_last.z;
  s_have_prev = true;
}

// --- Recording ------------------------------------------------------------------

static uint16_t clamp16(int v) {
  return (uint16_t)(v < 0 ? 0 : v > 0xFFFF ? 0xFFFF : v);
}

static void add(DiagKind kind, int due, int elapsed) {
  if (s_slot.count == DIAG_MAX) {   // full: drop the oldest marker, keep START
    memmove(&s_slot.rec[1], &s_slot.rec[2], sizeof(DiagRecord) * (DIAG_MAX - 2));
    s_slot.count--;
  }
  BatteryChargeState b = battery_state_service_peek();
  DiagRecord *r = &s_slot.rec[s_slot.count++];
  *r = (DiagRecord){
    .elapsed_s = clamp16(elapsed), .due_s = clamp16(due), .kind = kind,
    .battery   = (uint8_t)(b.charge_percent | (b.is_charging ? 0x80 : 0)),
    .flags     = (uint8_t)((quiet_time_is_active() ? FLAG_QUIET : 0) |
                           (connection_service_peek_pebble_app_connection() ? FLAG_PHONE : 0)),
    .still_min = (uint8_t)(s_still > 255 ? 255 : s_still),
  };
  persist_write_data(PKEY_DIAG_SLOT0 + s_slot_idx, &s_slot, sizeof(s_slot));
}

static bool slot_valid(int idx) {
  return persist_exists(PKEY_DIAG_SLOT0 + idx) &&
         persist_get_size(PKEY_DIAG_SLOT0 + idx) == (int)sizeof(DiagSlot);
}

static int newest_idx(void) {
  int n = persist_exists(PKEY_DIAG_NEWEST) ? persist_read_int(PKEY_DIAG_NEWEST) : -1;
  return (n >= 0 && n < DIAG_SESSIONS) ? n : -1;
}

void diag_session_start(time_t start, int elapsed) {
  int n = newest_idx();
  s_slot_idx = (n + 1) % DIAG_SESSIONS;   // n == -1 -> slot 0
  persist_write_int(PKEY_DIAG_NEWEST, s_slot_idx);

  memset(&s_slot, 0, sizeof(s_slot));
  s_slot.start = (int32_t)start;
  s_slot.cfg[0] = g_config.duration_min;
  s_slot.cfg[1] = g_config.interval_min;
  s_slot.cfg[2] = g_config.half_enabled;
  s_slot.cfg[3] = g_config.vibe_strength;
  s_stopped = false;
  accel_start();
  add(D_START, elapsed, elapsed);
}

void diag_marker(MarkerEvent ev, int due, int elapsed) {
  DiagKind k;
  switch (ev) {
    case MARKER_TICK: k = D_TICK; break;
    case MARKER_HALF: k = D_HALF; break;
    case MARKER_END:  k = D_END;  break;
    default: return;
  }
  add(k, due, elapsed);
}

// A self-test result takes the next slot as a session of its own (cfg[3] = the
// strength tested), so the viewer pages to it with Select like any sitting.
void diag_test_save(MarkerEvent pattern, int felt, int total) {
  DiagKind k = pattern == MARKER_END ? D_END : pattern == MARKER_HALF ? D_HALF : D_TICK;
  int idx = (newest_idx() + 1) % DIAG_SESSIONS;
  BatteryChargeState b = battery_state_service_peek();
  DiagSlot slot;
  memset(&slot, 0, sizeof(slot));
  slot.start = (int32_t)time(NULL);
  slot.cfg[3] = g_config.vibe_strength;
  slot.count = 1;
  slot.rec[0] = (DiagRecord){
    .elapsed_s = clamp16(felt), .due_s = clamp16(total), .kind = D_TEST,
    .battery   = (uint8_t)(b.charge_percent | (b.is_charging ? 0x80 : 0)),
    .still_min = (uint8_t)k,
  };
  persist_write_data(PKEY_DIAG_SLOT0 + idx, &slot, sizeof(slot));
  persist_write_int(PKEY_DIAG_NEWEST, idx);
}

// --- "Did you feel it?" ----------------------------------------------------------

#define Q_LINE_MAX 24

static Window    *s_q_win;
static TextLayer *s_q_count, *s_q_name, *s_q_ask, *s_q_help;
static int        s_q_cur;                 // record index being asked
static char       s_q_count_buf[16];
static char       s_q_name_buf[Q_LINE_MAX];

static bool is_marker(int i) {
  uint8_t k = s_slot.rec[i].kind;
  return k == D_TICK || k == D_HALF || k == D_END;
}

static const char *kind_name(uint8_t kind) {
  return kind == D_END ? "END" : kind == D_HALF ? "HALF" : "TICK";
}

// Point the question window at the first marker at or after record `from`;
// returns false if there is none left.
static bool q_show(int from) {
  int total = 0, pos = 0;
  for (int i = 0; i < s_slot.count; i++) {
    if (!is_marker(i)) continue;
    total++;
    if (i < from) pos++;
  }
  for (int i = from; i < s_slot.count; i++) {
    if (!is_marker(i)) continue;
    const DiagRecord *r = &s_slot.rec[i];
    s_q_cur = i;
    snprintf(s_q_count_buf, sizeof(s_q_count_buf), "%d / %d", pos + 1, total);
    snprintf(s_q_name_buf, sizeof(s_q_name_buf), "%02d:%02d %s",
             r->elapsed_s / 60, r->elapsed_s % 60, kind_name(r->kind));
    text_layer_set_text(s_q_count, s_q_count_buf);
    text_layer_set_text(s_q_name, s_q_name_buf);
    return true;
  }
  return false;
}

static void q_answer(uint8_t felt) {
  s_slot.rec[s_q_cur].felt = felt;
  persist_write_data(PKEY_DIAG_SLOT0 + s_slot_idx, &s_slot, sizeof(s_slot));
  if (!q_show(s_q_cur + 1)) window_stack_pop(true);   // last one: back to the summary
}

static void q_yes(ClickRecognizerRef rec, void *ctx)  { q_answer(FELT_YES); }
static void q_no(ClickRecognizerRef rec, void *ctx)   { q_answer(FELT_NO); }
static void q_skip(ClickRecognizerRef rec, void *ctx) { q_answer(0); }

static void q_click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_UP, q_yes);
  window_single_click_subscribe(BUTTON_ID_DOWN, q_no);
  window_single_click_subscribe(BUTTON_ID_SELECT, q_skip);
  // BACK keeps its default: pop, leaving the remaining answers "not sure".
}

static TextLayer *q_text(GRect r, const char *font, const char *text) {
  TextLayer *t = text_layer_create(r);
  text_layer_set_background_color(t, GColorClear);
  text_layer_set_text_color(t, GColorWhite);
  text_layer_set_font(t, fonts_get_system_font(font));
  text_layer_set_text_alignment(t, GTextAlignmentCenter);
  if (text) text_layer_set_text(t, text);
  return t;
}

static void q_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);
  s_q_count = q_text(GRect(0, 8, b.size.w, 22), FONT_KEY_GOTHIC_18, NULL);
  s_q_name  = q_text(GRect(0, 36, b.size.w, 36), FONT_KEY_GOTHIC_28_BOLD, NULL);
  s_q_ask  = q_text(GRect(0, 80, b.size.w, 30), FONT_KEY_GOTHIC_24, "felt it?");
  s_q_help = q_text(GRect(0, 130, b.size.w, 72), FONT_KEY_GOTHIC_18,
                           "Up = yes\nDown = no\nSelect = not sure");
  layer_add_child(root, text_layer_get_layer(s_q_count));
  layer_add_child(root, text_layer_get_layer(s_q_name));
  layer_add_child(root, text_layer_get_layer(s_q_ask));
  layer_add_child(root, text_layer_get_layer(s_q_help));
  q_show(0);
}

static void q_unload(Window *w) {
  text_layer_destroy(s_q_count);
  text_layer_destroy(s_q_name);
  text_layer_destroy(s_q_ask);
  text_layer_destroy(s_q_help);
  window_destroy(s_q_win);
  s_q_win = NULL;
}

static void ask_felt(void) {
  bool any = false;
  for (int i = 0; i < s_slot.count; i++) any = any || is_marker(i);
  if (!any || s_q_win) return;
  s_q_win = window_create();
  window_set_background_color(s_q_win, GColorBlack);
  window_set_click_config_provider(s_q_win, q_click_config);
  window_set_window_handlers(s_q_win, (WindowHandlers){ .load = q_load, .unload = q_unload });
  window_stack_push(s_q_win, true);
}

void diag_stop(int elapsed) {
  s_stopped = true;
  accel_stop();
  add(D_STOP, elapsed, elapsed);
  ask_felt();
}

void diag_close(int elapsed) {
  if (!s_stopped) add(D_ABORT, elapsed, elapsed);
  s_stopped = true;
  accel_stop();
}

// --- Viewer -------------------------------------------------------------------
// Down on the summary opens the newest session; Select steps to the next older
// one (wrapping); Up/Down scroll.

#define LINE_MAX 56
#define TEXT_MAX (LINE_MAX * (DIAG_MAX + 4) + 1)

static Window      *s_win;
static ScrollLayer *s_scroll;
static TextLayer   *s_text;
static char         s_buf[TEXT_MAX];
static int          s_view;   // 0 = newest session

// Append `line` to s_buf at offset n (truncating at the end); returns the new offset.
static int append(int n, const char *line) {
  int room = (int)sizeof(s_buf) - 1 - n;
  int len = (int)strlen(line);
  if (len > room) len = room;
  memcpy(s_buf + n, line, len);
  s_buf[n + len] = '\0';
  return n + len;
}

// How many sessions are on file, walking back from the newest slot.
static int session_count(void) {
  int newest = newest_idx();
  if (newest < 0) return 0;
  int n = 0;
  while (n < DIAG_SESSIONS && slot_valid((newest - n + DIAG_SESSIONS) % DIAG_SESSIONS)) n++;
  return n;
}

// "82%+ ph QT": battery (+ when charging), phone connected, Quiet Time.
static void context_str(char *out, size_t size, const DiagRecord *r) {
  snprintf(out, size, "%d%%%s%s%s", r->battery & 0x7F, (r->battery & 0x80) ? "+" : "",
           (r->flags & FLAG_PHONE) ? " ph" : "", (r->flags & FLAG_QUIET) ? " QT" : "");
}

static void format_session(void) {
  s_buf[0] = '\0';
  int total = session_count();
  if (total == 0) { append(0, "no log yet"); return; }
  if (s_view >= total) s_view = 0;

  DiagSlot slot;
  memset(&slot, 0, sizeof(slot));
  persist_read_data(PKEY_DIAG_SLOT0 + (newest_idx() - s_view + DIAG_SESSIONS) % DIAG_SESSIONS,
                    &slot, sizeof(slot));
  if (slot.count > DIAG_MAX) slot.count = DIAG_MAX;

  char line[LINE_MAX], ctx[20];
  int n = 0;
  time_t t = (time_t)slot.start;
  struct tm *tm = localtime(&t);
  snprintf(line, sizeof(line), "#%d %02d-%02d %02d:%02d\n", s_view + 1,
           tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min);
  n = append(n, line);

  static const char *const STRENGTH[] = { "Light", "Med", "Strong" };
  const char *strength = STRENGTH[slot.cfg[3] < 3 ? slot.cfg[3] : 0];
  bool test = slot.count > 0 && slot.rec[0].kind == D_TEST;
  char tick[8];
  if (slot.cfg[1]) snprintf(tick, sizeof(tick), "%dm", slot.cfg[1]); else strcpy(tick, "off");
  if (test) {
    snprintf(line, sizeof(line), "vibe test %s\n", strength);
  } else {
    snprintf(line, sizeof(line), "%dm tick %s half %s %s\n", slot.cfg[0], tick,
             slot.cfg[2] ? "on" : "off", strength);
  }
  n = append(n, line);

  for (int i = 0; i < slot.count; i++) {
    const DiagRecord *r = &slot.rec[i];
    int m = r->elapsed_s / 60, s = r->elapsed_s % 60;
    int late = (int)r->elapsed_s - (int)r->due_s;
    context_str(ctx, sizeof(ctx), r);
    switch (r->kind) {
      case D_START:
        snprintf(line, sizeof(line), "start bat %s\n", ctx);
        break;
      case D_TEST:
        snprintf(line, sizeof(line), "TEST %s felt %d/%d bat %s\n", kind_name(r->still_min),
                 r->elapsed_s, r->due_s, ctx);
        break;
      case D_STOP:
      case D_ABORT:
        snprintf(line, sizeof(line), "%s %d:%02d s%d %s\n", r->kind == D_STOP ? "stop" : "ABORT",
                 m, s, r->still_min, ctx);
        break;
      default: {
        const char *felt = r->felt == FELT_YES ? "yes" : r->felt == FELT_NO ? "NO" : "?";
        int len = snprintf(line, sizeof(line), "%02d:%02d %s ", m, s, kind_name(r->kind));
        if (late > 0) len += snprintf(line + len, sizeof(line) - len, "late %ds ", late);
        snprintf(line + len, sizeof(line) - len, "%s s%d %s\n", felt, r->still_min, ctx);
      }
    }
    n = append(n, line);
  }
  if (!test && (slot.count == 0 ||
      (slot.rec[slot.count - 1].kind != D_STOP && slot.rec[slot.count - 1].kind != D_ABORT))) {
    append(n, "(no stop: killed?)");
  }
}

static void refresh(void) {
  GRect b = layer_get_bounds(window_get_root_layer(s_win));
  format_session();
  text_layer_set_text(s_text, s_buf);
  text_layer_set_size(s_text, GSize(b.size.w - 8, 2000));
  GSize sz = text_layer_get_content_size(s_text);
  text_layer_set_size(s_text, GSize(b.size.w - 8, sz.h + 4));
  scroll_layer_set_content_size(s_scroll, GSize(b.size.w, sz.h + 12));
  scroll_layer_set_content_offset(s_scroll, GPointZero, false);
}

static void next_older(ClickRecognizerRef rec, void *ctx) {
  int total = session_count();
  s_view = total ? (s_view + 1) % total : 0;
  refresh();
}

static void view_click_config(void *ctx) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, scroll_layer_scroll_up_click_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, scroll_layer_scroll_down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, next_older);
}

static void win_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);

  s_scroll = scroll_layer_create(b);
  window_set_click_config_provider_with_context(w, view_click_config, s_scroll);
  s_text = text_layer_create(GRect(4, 0, b.size.w - 8, 2000));
  text_layer_set_font(s_text, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  scroll_layer_add_child(s_scroll, text_layer_get_layer(s_text));
  layer_add_child(root, scroll_layer_get_layer(s_scroll));
  refresh();
}

static void win_unload(Window *w) {
  text_layer_destroy(s_text);
  scroll_layer_destroy(s_scroll);
  window_destroy(s_win);
  s_win = NULL;
}

void diag_window_push(void) {
  if (s_win) return;
  s_view = 0;
  s_win = window_create();
  window_set_window_handlers(s_win, (WindowHandlers){ .load = win_load, .unload = win_unload });
  window_stack_push(s_win, true);
}

#endif  // MARKER_DIAG
