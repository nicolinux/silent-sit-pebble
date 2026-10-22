#include "diag.h"

#ifdef MARKER_DIAG

// Persisted log of the most recent session. One packed blob under PKEY_DIAG
// (header + DIAG_MAX records = 1 + 5*40 = 201 B, within the 256 B
// PERSIST_DATA_MAX_LENGTH limit) and the start wall-clock time under
// PKEY_DIAG_START. Both are rewritten on every record so a crash or forced exit
// still leaves the log. When full, the oldest record is dropped.
#define PKEY_DIAG       100
#define PKEY_DIAG_START 101
#define DIAG_MAX        40

typedef enum { D_START, D_TICK, D_HALF, D_END, D_STOP, D_ABORT } DiagKind;

typedef struct __attribute__((packed)) {
  uint16_t elapsed_s;   // when it was delivered
  uint16_t due_s;       // its scheduled second (lateness = elapsed - due)
  uint8_t  kind;        // DiagKind
} DiagRecord;

typedef struct __attribute__((packed)) {
  uint8_t    count;
  DiagRecord rec[DIAG_MAX];
} DiagLog;

_Static_assert(sizeof(DiagLog) <= PERSIST_DATA_MAX_LENGTH, "diag log exceeds one persist value");

static DiagLog s_log;
static bool    s_stopped;   // STOP recorded for the current session

static uint16_t clamp16(int v) {
  return (uint16_t)(v < 0 ? 0 : v > 0xFFFF ? 0xFFFF : v);
}

static void add(DiagKind kind, int due, int elapsed) {
  if (s_log.count == DIAG_MAX) {
    memmove(&s_log.rec[0], &s_log.rec[1], sizeof(DiagRecord) * (DIAG_MAX - 1));
    s_log.count--;
  }
  s_log.rec[s_log.count++] = (DiagRecord){
    .elapsed_s = clamp16(elapsed), .due_s = clamp16(due), .kind = kind };
  persist_write_data(PKEY_DIAG, &s_log, sizeof(s_log));
}

void diag_session_start(time_t start, int elapsed) {
  memset(&s_log, 0, sizeof(s_log));
  s_stopped = false;
  persist_write_int(PKEY_DIAG_START, (int32_t)start);
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

void diag_stop(int elapsed) {
  s_stopped = true;
  add(D_STOP, elapsed, elapsed);
}

void diag_close(int elapsed) {
  if (!s_stopped) add(D_ABORT, elapsed, elapsed);
  s_stopped = true;
}

// --- Viewer -------------------------------------------------------------------

#define LINE_MAX 32
#define TEXT_MAX (LINE_MAX * (DIAG_MAX + 2) + 1)

static Window      *s_win;
static ScrollLayer *s_scroll;
static TextLayer   *s_text;
static char         s_buf[TEXT_MAX];

// Append `line` to s_buf at offset n (truncating at the end); returns the new offset.
static int append(int n, const char *line) {
  int room = (int)sizeof(s_buf) - 1 - n;
  int len = (int)strlen(line);
  if (len > room) len = room;
  memcpy(s_buf + n, line, len);
  s_buf[n + len] = '\0';
  return n + len;
}

static void format_log(void) {
  DiagLog log;
  memset(&log, 0, sizeof(log));
  if (persist_exists(PKEY_DIAG)) persist_read_data(PKEY_DIAG, &log, sizeof(log));
  if (log.count > DIAG_MAX) log.count = DIAG_MAX;

  char line[LINE_MAX];
  int n = 0;
  s_buf[0] = '\0';
  if (log.count == 0) { append(0, "no log yet"); return; }

  for (int i = 0; i < log.count; i++) {
    const DiagRecord *r = &log.rec[i];
    int m = r->elapsed_s / 60, s = r->elapsed_s % 60;
    int late = (int)r->elapsed_s - (int)r->due_s;
    switch (r->kind) {
      case D_START: {
        time_t t = persist_exists(PKEY_DIAG_START) ? (time_t)persist_read_int(PKEY_DIAG_START) : 0;
        struct tm *tm = localtime(&t);
        snprintf(line, sizeof(line), "start %02d:%02d\n", tm->tm_hour, tm->tm_min);
        break;
      }
      case D_STOP:
        snprintf(line, sizeof(line), "stop %d:%02d\n", m, s);
        break;
      case D_ABORT:
        snprintf(line, sizeof(line), "ABORT %d:%02d\n", m, s);
        break;
      default: {
        const char *name = r->kind == D_END ? "END" : r->kind == D_HALF ? "HALF" : "TICK";
        if (late > 0) {
          snprintf(line, sizeof(line), "%02d:%02d %s late %ds\n", m, s, name, late);
        } else {
          snprintf(line, sizeof(line), "%02d:%02d %s\n", m, s, name);
        }
      }
    }
    n = append(n, line);
  }
  if (log.rec[log.count - 1].kind != D_STOP && log.rec[log.count - 1].kind != D_ABORT) {
    append(n, "(no stop: killed?)");
  }
}

static void win_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);
  format_log();

  s_scroll = scroll_layer_create(b);
  scroll_layer_set_click_config_onto_window(s_scroll, w);
  s_text = text_layer_create(GRect(4, 0, b.size.w - 8, 2000));
  text_layer_set_font(s_text, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text(s_text, s_buf);
  GSize sz = text_layer_get_content_size(s_text);
  text_layer_set_size(s_text, GSize(b.size.w - 8, sz.h + 4));
  scroll_layer_set_content_size(s_scroll, GSize(b.size.w, sz.h + 12));
  scroll_layer_add_child(s_scroll, text_layer_get_layer(s_text));
  layer_add_child(root, scroll_layer_get_layer(s_scroll));
}

static void win_unload(Window *w) {
  text_layer_destroy(s_text);
  scroll_layer_destroy(s_scroll);
  window_destroy(s_win);
  s_win = NULL;
}

void diag_window_push(void) {
  if (s_win) return;
  s_win = window_create();
  window_set_window_handlers(s_win, (WindowHandlers){ .load = win_load, .unload = win_unload });
  window_stack_push(s_win, true);
}

#endif  // MARKER_DIAG
