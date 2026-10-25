#include "diag.h"

#ifdef MARKER_DIAG

#include "app.h"
#include "session_window.h"

// Vibration self-test (MARKER_DIAG builds only). Plays one marker pattern
// TEST_RUNS times, TEST_GAP_MS apart, at the configured strength, then asks how
// many were felt and stores the answer in the diagnostics history. It separates
// "the motor/firmware drops this pattern" from Stand-By Mode, which can't engage
// during a short attended test. Back at any point cancels without a result.

#define TEST_RUNS   10
#define TEST_GAP_MS 10000

typedef enum { T_PICK, T_RUN, T_ASK } TestState;

static const MarkerEvent PATTERNS[] = { MARKER_TICK, MARKER_HALF, MARKER_END };
static const char *const PATTERN_NAMES[] = { "TICK", "HALF", "END" };
#define N_PATTERNS 3

static Window    *s_win;
static TextLayer *s_head, *s_big, *s_help;
static AppTimer  *s_timer;
static TestState  s_state;
static int        s_pattern = 2;   // default END
static int        s_runs;          // pulses played so far
static int        s_felt;
static char       s_big_buf[16];

static TextLayer *make_text(GRect r, const char *font, const char *text) {
  TextLayer *t = text_layer_create(r);
  text_layer_set_background_color(t, GColorClear);
  text_layer_set_text_color(t, GColorWhite);
  text_layer_set_font(t, fonts_get_system_font(font));
  text_layer_set_text_alignment(t, GTextAlignmentCenter);
  if (text) text_layer_set_text(t, text);
  return t;
}

static void show(void) {
  switch (s_state) {
    case T_PICK:
      text_layer_set_text(s_head, "Vibe test");
      text_layer_set_text(s_big, PATTERN_NAMES[s_pattern]);
      text_layer_set_text(s_help, "Up/Down = pattern\nSelect = start\n10x, 10 s apart");
      break;
    case T_RUN:
      snprintf(s_big_buf, sizeof(s_big_buf), "%s %d/%d", PATTERN_NAMES[s_pattern], s_runs, TEST_RUNS);
      text_layer_set_text(s_head, "Vibe test");
      text_layer_set_text(s_big, s_big_buf);
      text_layer_set_text(s_help, "Back = cancel");
      break;
    case T_ASK:
      snprintf(s_big_buf, sizeof(s_big_buf), "%d", s_felt);
      text_layer_set_text(s_head, "How many did you feel?");
      text_layer_set_text(s_big, s_big_buf);
      text_layer_set_text(s_help, "Up/Down = pick\nSelect = save");
      break;
  }
}

static void step(void *data) {
  s_timer = NULL;
  if (s_runs == TEST_RUNS) {   // the last pulse has had its gap: ask
    s_state = T_ASK;
    s_felt = TEST_RUNS;
    show();
    return;
  }
  s_runs++;
  vibe_play_marker(PATTERNS[s_pattern]);
  show();
  s_timer = app_timer_register(TEST_GAP_MS, step, NULL);
}

static void adjust(int delta) {
  if (s_state == T_PICK) {
    s_pattern = (s_pattern + delta + N_PATTERNS) % N_PATTERNS;
  } else if (s_state == T_ASK) {
    s_felt = s_felt + delta;
    if (s_felt < 0) s_felt = 0;
    if (s_felt > TEST_RUNS) s_felt = TEST_RUNS;
  } else {
    return;
  }
  show();
}

static void up_click(ClickRecognizerRef rec, void *ctx)   { adjust(-1); }
static void down_click(ClickRecognizerRef rec, void *ctx) { adjust(1); }

static void select_click(ClickRecognizerRef rec, void *ctx) {
  if (s_state == T_PICK) {
    s_state = T_RUN;
    s_runs = 0;
    step(NULL);
  } else if (s_state == T_ASK) {
    diag_test_save(PATTERNS[s_pattern], s_felt, TEST_RUNS);
    window_stack_pop(true);
  }
}

static void click_config(void *ctx) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 150, up_click);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 150, down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  // BACK keeps its default: pop, cancelling with no result.
}

static void win_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);
  s_head = make_text(GRect(0, 8, b.size.w, 56), FONT_KEY_GOTHIC_24, NULL);
  s_big  = make_text(GRect(0, 64, b.size.w, 50), FONT_KEY_BITHAM_30_BLACK, NULL);
  s_help = make_text(GRect(0, 130, b.size.w, 72), FONT_KEY_GOTHIC_18, NULL);
  layer_add_child(root, text_layer_get_layer(s_head));
  layer_add_child(root, text_layer_get_layer(s_big));
  layer_add_child(root, text_layer_get_layer(s_help));
  s_state = T_PICK;
  show();
}

static void win_unload(Window *w) {
  if (s_timer) { app_timer_cancel(s_timer); s_timer = NULL; }
  text_layer_destroy(s_head);
  text_layer_destroy(s_big);
  text_layer_destroy(s_help);
  window_destroy(s_win);
  s_win = NULL;
}

void vibe_test_push(void) {
  if (s_win) return;
  s_win = window_create();
  window_set_background_color(s_win, GColorBlack);
  window_set_click_config_provider(s_win, click_config);
  window_set_window_handlers(s_win, (WindowHandlers){ .load = win_load, .unload = win_unload });
  window_stack_push(s_win, true);
}

#endif  // MARKER_DIAG
