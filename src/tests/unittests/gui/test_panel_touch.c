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

// keep size persistence isolated from the user's configuration
#define dt_conf_get_int dt_test_panel_get_int
#define dt_conf_set_int dt_test_panel_set_int
#include "gui/gtk.c"
#undef dt_conf_get_int
#undef dt_conf_set_int

#ifdef _WIN32
#include "win/main_wrapper.h"
#endif

int dt_test_panel_get_int(const char *name)
{
  if(!strcmp(name, "min_panel_width")) return 200;
  if(!strcmp(name, "max_panel_width")) return 600;
  if(!strcmp(name, "min_center_width")) return 400;
  if(!strcmp(name, "min_panel_height")) return 80;
  if(!strcmp(name, "max_panel_height")) return 300;
  return 0;
}

void dt_test_panel_set_int(const char *name, const int value)
{
}

#if !GTK_CHECK_VERSION(4, 0, 0)
static void _panel_event(GtkWidget *handle, GdkDevice *source, GdkEventType type,
                          double dx, double dy)
{
  GdkEvent *event = gdk_event_new(type);
  event->any.window = g_object_ref(gtk_widget_get_window(handle));
  if(type >= GDK_TOUCH_BEGIN && type <= GDK_TOUCH_CANCEL)
  {
    event->touch.sequence = (GdkEventSequence *)handle;
    event->touch.x = 7 + dx;
    event->touch.y = 80 + dy;
    event->touch.x_root = 1000 + dx;
    event->touch.y_root = 500 + dy;
    event->touch.emulating_pointer = TRUE;
  }
  else if(type == GDK_MOTION_NOTIFY)
  {
    event->motion.x = 7 + dx;
    event->motion.y = 80 + dy;
    event->motion.x_root = 1000 + dx;
    event->motion.y_root = 500 + dy;
    event->motion.state = GDK_BUTTON1_MASK;
  }
  else
  {
    event->button.x = 7 + dx;
    event->button.y = 80 + dy;
    event->button.x_root = 1000 + dx;
    event->button.y_root = 500 + dy;
    event->button.button = GDK_BUTTON_PRIMARY;
  }
  GdkDevice *pointer = gdk_seat_get_pointer(gdk_display_get_default_seat(gdk_display_get_default()));
  gdk_event_set_device(event, pointer);
  gdk_event_set_source_device(event, source ? source : pointer);
  gtk_main_do_event(event);
  gdk_event_free(event);
}

static void _resize_test(dt_ui_panel_t panel, double dx, double dy,
                          int expected, gboolean mouse, gboolean cancel)
{
  GtkWidget *window = gtk_offscreen_window_new();
  darktable.gui->ui->main_window = window;
  GtkWidget *grid = gtk_grid_new();
  gtk_container_add(GTK_CONTAINER(window), grid);
  for(int i = 0; i < DT_UI_PANEL_SIZE; i++)
    darktable.gui->ui->panels[i] = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *left = darktable.gui->ui->panels[DT_UI_PANEL_LEFT];
  GtkWidget *right = darktable.gui->ui->panels[DT_UI_PANEL_RIGHT];
  GtkWidget *bottom = darktable.gui->ui->panels[DT_UI_PANEL_BOTTOM];
  gtk_widget_set_size_request(left, 300, 400);
  gtk_widget_set_size_request(right, 300, 400);
  gtk_widget_set_size_request(bottom, 600, 120);
  gtk_grid_attach(GTK_GRID(grid), left, 0, 0, 1, 1);
  GtkWidget *center = gtk_drawing_area_new();
  gtk_widget_set_size_request(center, 600, 400);
  gtk_grid_attach(GTK_GRID(grid), center, 1, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), right, 2, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), bottom, 0, 1, 3, 1);
  GtkWidget *target = darktable.gui->ui->panels[panel];
  GtkWidget *handle = gtk_drawing_area_new();
  gtk_widget_set_name(handle, panel == DT_UI_PANEL_LEFT ? "panel-handle-left"
                              : panel == DT_UI_PANEL_RIGHT ? "panel-handle-right"
                              : "panel-handle-bottom");
  gtk_container_add(GTK_CONTAINER(target), handle);
  GtkGesture *drag = _panel_handle_connect(handle, target);
  gtk_widget_show_all(window);
  gtk_container_check_resize(GTK_CONTAINER(window));
  GtkAllocation allocation = { 0, 0, panel == DT_UI_PANEL_BOTTOM ? 1200 : 300,
                                panel == DT_UI_PANEL_BOTTOM ? 120 : 400 };
  gtk_widget_size_allocate(target, &allocation);
  GTypeQuery query;
  g_type_query(GDK_TYPE_DEVICE, &query);
  static GType touch_type;
  if(!touch_type)
    touch_type = g_type_register_static_simple(GDK_TYPE_DEVICE, "DtPanelTestTouch",
                                               query.class_size, NULL, query.instance_size, NULL, 0);
  GdkDevice *touch = g_object_new(touch_type, "display", gdk_display_get_default(),
                                  "name", "test touch", "type", GDK_DEVICE_TYPE_SLAVE,
                                  "input-source", GDK_SOURCE_TOUCHSCREEN,
                                  "input-mode", GDK_MODE_SCREEN, NULL);
  _panel_event(handle, mouse ? NULL : touch, mouse ? GDK_BUTTON_PRESS : GDK_TOUCH_BEGIN, 0, 0);
  assert_true(darktable.gui->widgets.panel_handle_dragging);
  if(!mouse) assert_true(gtk_gesture_handles_sequence(drag, (GdkEventSequence *)handle));
  _panel_event(handle, mouse ? NULL : touch, mouse ? GDK_MOTION_NOTIFY : GDK_TOUCH_UPDATE, dx, dy);
  int width, height;
  gtk_widget_get_size_request(target, &width, &height);
  assert_int_equal(panel == DT_UI_PANEL_BOTTOM ? height : width, expected);
  _panel_event(handle, mouse ? NULL : touch,
                cancel ? GDK_TOUCH_CANCEL : mouse ? GDK_BUTTON_RELEASE : GDK_TOUCH_END, dx, dy);
  assert_false(darktable.gui->widgets.panel_handle_dragging);
  gtk_widget_destroy(window);
  g_object_unref(touch);
}

static void test_left_finger_resize(void **state) { _resize_test(DT_UI_PANEL_LEFT, 80, 0, 380, FALSE, FALSE); }
static void test_right_finger_resize(void **state) { _resize_test(DT_UI_PANEL_RIGHT, -80, 0, 380, FALSE, FALSE); }
static void test_bottom_finger_resize(void **state) { _resize_test(DT_UI_PANEL_BOTTOM, 0, -40, 160, FALSE, FALSE); }
static void test_bottom_finger_minimum(void **state) { _resize_test(DT_UI_PANEL_BOTTOM, 0, 300, 80, FALSE, FALSE); }
static void test_cancel_resize(void **state) { _resize_test(DT_UI_PANEL_LEFT, 40, 0, 340, FALSE, TRUE); }
static void test_mouse_resize(void **state) { _resize_test(DT_UI_PANEL_LEFT, 80, 0, 380, TRUE, FALSE); }
#endif

int main(int argc, char **argv)
{
#if GTK_CHECK_VERSION(4, 0, 0)
  return 77;
#else
  if(!gtk_init_check(&argc, &argv)) return 77;
  g_log_set_always_fatal(G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
  darktable.gui = calloc(1, sizeof(dt_gui_gtk_t));
  darktable.gui->ui = calloc(1, sizeof(dt_ui_t));
  darktable.gui->dpi_factor = 1;
  const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_left_finger_resize), cmocka_unit_test(test_right_finger_resize),
    cmocka_unit_test(test_bottom_finger_resize), cmocka_unit_test(test_bottom_finger_minimum),
    cmocka_unit_test(test_cancel_resize), cmocka_unit_test(test_mouse_resize)
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
#endif
}
