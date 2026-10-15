/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable. If not, see <http://www.gnu.org/licenses/>.
*/
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <gtk/gtk.h>

static int reorder_requests;
static GdkDragContext *dt_test_header_drag(GtkWidget *widget, GtkTargetList *targets,
                                          GdkDragAction actions, gint button,
                                          GdkEvent *event, gint x, gint y)
{
  reorder_requests++;
  return NULL;
}
#define gtk_drag_begin_with_coordinates dt_test_header_drag
#define dt_key_modifier_state dt_test_header_modifiers
#define dt_conf_get_bool dt_test_header_conf
#define dt_conf_set_bool dt_test_header_save
#define dt_ui_container_focus_widget dt_test_header_focus_container
#define dt_ui_center dt_test_header_center
#define dt_control_queue_redraw_center dt_test_header_redraw
#include "develop/imageop.c"
#undef gtk_drag_begin_with_coordinates
#undef dt_key_modifier_state
#undef dt_conf_get_bool
#undef dt_conf_set_bool
#undef dt_ui_container_focus_widget
#undef dt_ui_center
#undef dt_control_queue_redraw_center

static GtkWidget *focus_target;
GdkModifierType dt_test_header_modifiers(void) { return 0; }
gboolean dt_test_header_conf(const char *name) { return FALSE; }
void dt_test_header_save(const char *name, const int value) { }
void dt_test_header_focus_container(const struct dt_ui_t *ui, const dt_ui_container_t c, GtkWidget *widget) { }
GtkWidget *dt_test_header_center(const struct dt_ui_t *ui) { return focus_target; }
void dt_test_header_redraw(void) { }

#ifdef _WIN32
#include "win/main_wrapper.h"
#endif

#if !GTK_CHECK_VERSION(4, 0, 0)
static int taps;
static gboolean replay_press, benign_cancel;
static guint32 event_time_override;
static void _released(GtkGestureSingle *gesture, int n_press, double x, double y, gpointer data)
{
  dt_iop_header_gesture_t *state = g_object_get_data(G_OBJECT(dt_gui_get_widget(gesture)), "module-header-gesture");
  if(!state->moved && !state->reordering)
  {
    assert_true(n_press >= 1);
    assert_int_equal(gtk_gesture_single_get_current_button(gesture), GDK_BUTTON_PRIMARY);
    dt_iop_module_t *module = data;
    const gboolean before = module->expanded;
    _iop_plugin_header_released(gesture, n_press, x, y, module);
    if(before != module->expanded) taps++;
  }
  else _iop_plugin_header_released(gesture, n_press, x, y, NULL);
}

static void _event(GtkWidget *header, GdkDevice *source, GdkEventType type, double y)
{
  GdkEvent *event = gdk_event_new(type);
  event->any.window = g_object_ref(gtk_widget_get_window(header));
  static guint32 time = 1000;
  time += 100;
  const guint32 event_time = event_time_override ? event_time_override : time;
  if(type >= GDK_TOUCH_BEGIN && type <= GDK_TOUCH_CANCEL)
  {
    event->touch.sequence = (GdkEventSequence *)header;
    event->touch.x = event->touch.x_root = 80;
    event->touch.y = event->touch.y_root = y;
    event->touch.time = event_time;
    event->touch.emulating_pointer = TRUE;
  }
  else if(type == GDK_MOTION_NOTIFY)
  {
    event->motion.x = event->motion.x_root = 80;
    event->motion.y = event->motion.y_root = y;
    event->motion.time = event_time;
    event->motion.state = GDK_BUTTON1_MASK;
  }
  else
  {
    event->button.x = event->button.x_root = 80;
    event->button.y = event->button.y_root = y;
    event->button.time = event_time;
    event->button.button = GDK_BUTTON_PRIMARY;
  }
  GdkDevice *pointer = gdk_seat_get_pointer(gdk_display_get_default_seat(gdk_display_get_default()));
  gdk_event_set_device(event, pointer);
  gdk_event_set_source_device(event, source ? source : pointer);
  gtk_main_do_event(event);
  gdk_event_free(event);
}

static gboolean _stop_wait(gpointer data) { g_main_loop_quit(data); return G_SOURCE_REMOVE; }

static void _header_test(gboolean hold, gboolean move, gboolean emulated, gboolean mouse, gboolean cancel)
{
  GtkWidget *window = gtk_offscreen_window_new();
  GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
  gtk_scrolled_window_set_capture_button_press(GTK_SCROLLED_WINDOW(scroll), FALSE);
  gtk_container_add(GTK_CONTAINER(window), scroll);
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add(GTK_CONTAINER(scroll), box);
  dt_iop_module_t module = { 0 };
  module.dev = darktable.develop;
  GtkWidget *expander = dtgtk_expander_new(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0), NULL);
  module.expander = expander;
  dtgtk_expander_set_expanded(DTGTK_EXPANDER(expander), FALSE);
  GtkWidget *header = dtgtk_expander_get_header_event_box(DTGTK_EXPANDER(expander));
  GtkWidget *header_content = dtgtk_expander_get_header(DTGTK_EXPANDER(expander));
  GtkWidget *label_box = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(label_box), gtk_label_new("module title"));
  gtk_box_pack_start(GTK_BOX(header_content), label_box, TRUE, TRUE, 0);
  gtk_box_pack_end(GTK_BOX(header_content), gtk_button_new_with_label("reset"), FALSE, FALSE, 0);
  gtk_widget_set_size_request(header, 300, 60);
  gtk_box_pack_start(GTK_BOX(box), expander, FALSE, FALSE, 0);
  focus_target = header;
  gtk_widget_set_can_focus(header, TRUE);
  GtkWidget *spacer = gtk_drawing_area_new();
  gtk_widget_set_size_request(spacer, 300, 1000);
  gtk_box_pack_start(GTK_BOX(box), spacer, FALSE, FALSE, 0);
  GtkTargetEntry target = { "iop", GTK_TARGET_SAME_APP, DND_TARGET_IOP };
  gtk_drag_source_set(header, 0, &target, 1, GDK_ACTION_COPY);
  GtkGestureSingle *click = _header_connect_gestures(header);
  g_signal_connect(click, "released", G_CALLBACK(_released), &module);
  gtk_window_set_default_size(GTK_WINDOW(window), 320, 200);
  gtk_widget_show_all(window);
  gtk_container_check_resize(GTK_CONTAINER(window));
  GTypeQuery query;
  g_type_query(GDK_TYPE_DEVICE, &query);
  static GType touch_type;
  if(!touch_type)
    touch_type = g_type_register_static_simple(GDK_TYPE_DEVICE, "DtHeaderTouch",
                                               query.class_size, NULL, query.instance_size, NULL, 0);
  GdkDevice *touch = g_object_new(touch_type, "display", gdk_display_get_default(),
                                  "name", "test touch", "type", GDK_DEVICE_TYPE_SLAVE,
                                  "input-source", GDK_SOURCE_TOUCHSCREEN,
                                  "input-mode", GDK_MODE_SCREEN, NULL);
  taps = reorder_requests = 0;
  const gboolean pointer = mouse || emulated;
  _event(label_box, mouse ? NULL : touch, pointer ? GDK_BUTTON_PRESS : GDK_TOUCH_BEGIN, 30);
  dt_iop_header_gesture_t *state = g_object_get_data(G_OBJECT(header), "module-header-gesture");
  if(benign_cancel)
    _header_gesture_cancel(GTK_GESTURE(click), gtk_gesture_single_get_current_sequence(click), state);
  if(move) _event(label_box, mouse ? NULL : touch, pointer ? GDK_MOTION_NOTIFY : GDK_TOUCH_UPDATE, -30);
  if(move && replay_press)
  {
    event_time_override = state->press_time;
    _event(label_box, touch, GDK_BUTTON_PRESS, 30);
    event_time_override = 0;
    assert_true(state->moved);
  }
  if(hold)
  {
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_timeout_add(150, _stop_wait, loop);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
  }
  _event(label_box, mouse ? NULL : touch,
          cancel ? GDK_TOUCH_CANCEL : pointer ? GDK_BUTTON_RELEASE : GDK_TOUCH_END, replay_press ? 30 : move ? -30 : 30);
  assert_int_equal(reorder_requests, (!move && hold) || (mouse && move) ? 1 : 0);
  assert_int_equal(taps, !move && !hold && !cancel ? 1 : 0);
  if(!move && !hold && !cancel && !mouse)
  {
    assert_true(module.expanded);
    _event(label_box, touch, pointer ? GDK_BUTTON_PRESS : GDK_TOUCH_BEGIN, 30);
    _event(label_box, touch, pointer ? GDK_BUTTON_RELEASE : GDK_TOUCH_END, 30);
    assert_int_equal(taps, 2);
    assert_false(module.expanded);
  }
  assert_true(gtk_scrolled_window_get_kinetic_scrolling(GTK_SCROLLED_WINDOW(scroll)));
  gtk_widget_destroy(window);
  g_object_unref(touch);
}

static void test_tap(void **state) { _header_test(FALSE, FALSE, FALSE, FALSE, FALSE); }
static void test_emulated_tap(void **state) { _header_test(FALSE, FALSE, TRUE, FALSE, FALSE); }
static void test_hold(void **state) { _header_test(TRUE, FALSE, FALSE, FALSE, FALSE); }
static void test_swipe(void **state) { _header_test(FALSE, TRUE, FALSE, FALSE, FALSE); }
static void test_move_cancels_hold(void **state) { _header_test(TRUE, TRUE, FALSE, FALSE, FALSE); }
static void test_emulated_hold(void **state) { _header_test(TRUE, FALSE, TRUE, FALSE, FALSE); }
static void test_mouse_drag(void **state) { _header_test(FALSE, TRUE, FALSE, TRUE, FALSE); }
static void test_cancel(void **state) { _header_test(FALSE, FALSE, FALSE, FALSE, TRUE); }
static void test_swipe_replayed_press(void **state)
{
  replay_press = TRUE;
  _header_test(FALSE, TRUE, FALSE, FALSE, FALSE);
  replay_press = FALSE;
}
static void test_recognizer_cancel_keeps_tap(void **state)
{
  benign_cancel = TRUE;
  _header_test(FALSE, FALSE, FALSE, FALSE, FALSE);
  benign_cancel = FALSE;
}
#endif

int main(int argc, char **argv)
{
#if GTK_CHECK_VERSION(4, 0, 0)
  return 77;
#else
  if(!gtk_init_check(&argc, &argv)) return 77;
  g_log_set_always_fatal(G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
  g_object_set(gtk_settings_get_default(), "gtk-long-press-time", 40, NULL);
  darktable.gui = calloc(1, sizeof(dt_gui_gtk_t));
  dt_atomic_set_int(&darktable.gui->reset, 1);
  darktable.develop = calloc(1, sizeof(dt_develop_t));
  darktable.lib = calloc(1, sizeof(dt_lib_t));
  darktable.lib->proxy.colorpicker.restrict_histogram = TRUE;
  const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_tap), cmocka_unit_test(test_emulated_tap),
    cmocka_unit_test(test_hold), cmocka_unit_test(test_swipe),
    cmocka_unit_test(test_move_cancels_hold), cmocka_unit_test(test_emulated_hold),
    cmocka_unit_test(test_mouse_drag), cmocka_unit_test(test_cancel),
    cmocka_unit_test(test_swipe_replayed_press), cmocka_unit_test(test_recognizer_cancel_keeps_tap)
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
#endif
}
