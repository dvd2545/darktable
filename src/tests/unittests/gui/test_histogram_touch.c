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

// capture exposure requests while exercising the production gesture callbacks
#define dt_dev_exposure_handle_event dt_test_exposure_event
#define dt_control_change_cursor dt_test_change_cursor
#include "libs/histogram.c"
#undef dt_dev_exposure_handle_event
#undef dt_control_change_cursor

#ifdef _WIN32
#include "win/main_wrapper.h"
#endif

static int edits;
static double adjustment;
static gboolean black_point;

void dt_test_exposure_event(int n_press, gdouble delta, GdkModifierType state,
                            const gboolean is_blackpoint)
{
  ++edits;
  adjustment += delta;
  black_point = is_blackpoint;
}

void dt_test_change_cursor(const char *cursor_name)
{
}

#if !GTK_CHECK_VERSION(4, 0, 0)
static const char *_test_name(const dt_scopes_mode_t *mode)
{
  return "histogram";
}

static dt_scopes_highlight_t _test_highlight(const dt_scopes_mode_t *mode,
                                            double x, double y)
{
  return x < .2 ? DT_SCOPES_HIGHLIGHT_BLACK_POINT : DT_SCOPES_HIGHLIGHT_EXPOSURE;
}

static double _test_delta(const dt_scopes_mode_t *mode, double x, double y)
{
  return x / gtk_widget_get_allocated_width(mode->scopes->scope_draw);
}

static void _event(GtkGesture *drag, GdkDevice *source, GdkEventType type,
                    double x, double y)
{
  GdkEvent *event = gdk_event_new(type);
  GtkWidget *widget = dt_gui_get_widget(drag);
  event->any.window = g_object_ref(gtk_widget_get_window(widget));
  if(type >= GDK_TOUCH_BEGIN && type <= GDK_TOUCH_CANCEL)
  {
    event->touch.sequence = (GdkEventSequence *)drag;
    event->touch.x = x;
    event->touch.y = y;
    event->touch.emulating_pointer = TRUE;
  }
  else if(type == GDK_MOTION_NOTIFY)
  {
    event->motion.x = x;
    event->motion.y = y;
    event->motion.state = GDK_BUTTON1_MASK;
  }
  else
  {
    event->button.x = x;
    event->button.y = y;
    event->button.button = GDK_BUTTON_PRIMARY;
  }
  gdk_event_set_device(event, gdk_seat_get_pointer(gdk_display_get_default_seat(gdk_display_get_default())));
  gdk_event_set_source_device(event, source);
  gtk_event_controller_handle_event(GTK_EVENT_CONTROLLER(drag), event);
  gdk_event_free(event);
}

static void _drag_test(gboolean native_touch, gboolean start_black, gboolean cancel,
                        gboolean mouse)
{
  static const dt_scopes_functions_t functions = {
    .name = _test_name, .get_highlight = _test_highlight,
    .get_exposure_delta = _test_delta
  };
  dt_scopes_t s = { 0 };
  dt_scopes_mode_t mode = { .functions = &functions, .scopes = &s };
  s.cur_mode = &mode;
  GtkWidget *window = gtk_offscreen_window_new();
  s.scope_draw = gtk_drawing_area_new();
  gtk_container_add(GTK_CONTAINER(window), s.scope_draw);
  gtk_widget_show_all(window);
  GtkAllocation allocation = { 0, 0, 400, 200 };
  gtk_widget_size_allocate(s.scope_draw, &allocation);
  GtkGesture *drag = dt_gui_connect_drag(s.scope_draw, _drawable_drag_begin,
                                         _drawable_drag_end, _drawable_drag_update, &s);
  GTypeQuery query;
  g_type_query(GDK_TYPE_DEVICE, &query);
  static GType touch_type;
  if(!touch_type)
    touch_type = g_type_register_static_simple(GDK_TYPE_DEVICE, "DtHistogramTestTouch",
                                               query.class_size, NULL, query.instance_size, NULL, 0);
  GdkDevice *touch = g_object_new(touch_type, "display", gdk_display_get_default(),
                                  "name", "test touch", "type", GDK_DEVICE_TYPE_SLAVE,
                                  "input-source", GDK_SOURCE_TOUCHSCREEN,
                                  "input-mode", GDK_MODE_SCREEN, NULL);
  GdkDevice *source = mouse ? gdk_seat_get_pointer(gdk_display_get_default_seat(gdk_display_get_default())) : touch;
  edits = 0;
  adjustment = 0;
  const double x = start_black ? 20 : 200;
  _event(drag, source, native_touch ? GDK_TOUCH_BEGIN : GDK_BUTTON_PRESS, x, 80);
  assert_true(s.dragging);
  assert_int_equal(s.highlight, start_black ? DT_SCOPES_HIGHLIGHT_BLACK_POINT : DT_SCOPES_HIGHLIGHT_EXPOSURE);
  _event(drag, source, native_touch ? GDK_TOUCH_UPDATE : GDK_MOTION_NOTIFY, x + 80, 80);
  assert_int_equal(edits, 1);
  assert_float_equal(adjustment, .2, .001);
  assert_int_equal(black_point, start_black);
  _drawable_leave(NULL, &s);
  assert_int_equal(s.highlight, start_black ? DT_SCOPES_HIGHLIGHT_BLACK_POINT : DT_SCOPES_HIGHLIGHT_EXPOSURE);
  _event(drag, source, native_touch ? GDK_TOUCH_UPDATE : GDK_MOTION_NOTIFY, x + 120, 80);
  assert_float_equal(adjustment, .3, .001);
  _event(drag, source, cancel ? GDK_TOUCH_CANCEL : native_touch ? GDK_TOUCH_END : GDK_BUTTON_RELEASE,
           x + 120, 80);
  assert_false(s.dragging);
  assert_int_equal(edits, 2);
  gtk_widget_destroy(window);
  g_object_unref(touch);
}

static void test_finger_exposure_without_hover(void **state) { _drag_test(TRUE, FALSE, FALSE, FALSE); }
static void test_finger_black_point_without_hover(void **state) { _drag_test(TRUE, TRUE, FALSE, FALSE); }
static void test_emulated_finger_drag(void **state) { _drag_test(FALSE, FALSE, FALSE, FALSE); }
static void test_cancel_stops_histogram_drag(void **state) { _drag_test(TRUE, FALSE, TRUE, FALSE); }
static void test_mouse_histogram_drag(void **state) { _drag_test(FALSE, FALSE, FALSE, TRUE); }
#endif

int main(int argc, char **argv)
{
#if GTK_CHECK_VERSION(4, 0, 0)
  return 77;
#else
  if(!gtk_init_check(&argc, &argv)) return 77;
  g_log_set_always_fatal(G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
  const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_finger_exposure_without_hover),
    cmocka_unit_test(test_finger_black_point_without_hover),
    cmocka_unit_test(test_emulated_finger_drag),
    cmocka_unit_test(test_cancel_stops_histogram_drag),
    cmocka_unit_test(test_mouse_histogram_drag)
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
#endif
}
