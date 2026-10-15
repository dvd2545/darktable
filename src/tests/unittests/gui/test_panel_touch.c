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

static const char *info_position = "bottom center";
static const char *dt_test_info_position(const char *key) { return info_position; }
// exercise the real label setup without registering application-wide signals
#undef DT_CONTROL_SIGNAL_HANDLE
#define DT_CONTROL_SIGNAL_HANDLE(signal, cb) (void)(cb)
#define dt_conf_get_string_const dt_test_info_position
#include "libs/tools/image_infos.c"
#undef dt_conf_get_string_const

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
  GtkWidget *toggle = gtk_drawing_area_new();
  gtk_widget_set_size_request(toggle, 32, 32);
  GtkWidget *border = _panel_resize_border(target, toggle,
    panel == DT_UI_PANEL_LEFT ? "panel-handle-left"
    : panel == DT_UI_PANEL_RIGHT ? "panel-handle-right" : "panel-handle-bottom");
  gtk_grid_attach(GTK_GRID(grid), border, 3, 0, 1, 1);
  GtkWidget *handle = gtk_bin_get_child(GTK_BIN(border));
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

static void _toolbar_test(gboolean top)
{
  GtkWidget *window = gtk_offscreen_window_new();
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add(GTK_CONTAINER(window), box);
  if(top) _ui_init_panel_center_top(darktable.gui->ui, box);
  else _ui_init_panel_center_bottom(darktable.gui->ui, box);
  GtkWidget *controls = darktable.gui->ui->containers[top
    ? DT_UI_CONTAINER_PANEL_CENTER_TOP_LEFT : DT_UI_CONTAINER_PANEL_CENTER_BOTTOM_LEFT];
  for(int i = 0; i < 12; i++)
    gtk_box_pack_start(GTK_BOX(controls), gtk_button_new_with_label("toolbar control"), FALSE, FALSE, 0);
  gtk_window_set_default_size(GTK_WINDOW(window), 400, 100);
  gtk_widget_show_all(window);
  gtk_container_check_resize(GTK_CONTAINER(window));
  GtkWidget *scroll = darktable.gui->ui->panels[top
    ? DT_UI_PANEL_CENTER_TOP : DT_UI_PANEL_CENTER_BOTTOM];
  int minimum, natural;
  gtk_widget_get_preferred_width(box, &minimum, &natural);
  assert_true(minimum < 400);
  assert_int_equal(gtk_widget_get_allocated_width(window), 400);
  GtkAdjustment *adjustment = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(scroll));
  assert_true(gtk_adjustment_get_upper(adjustment) > gtk_adjustment_get_page_size(adjustment));
  assert_true(gtk_scrolled_window_get_kinetic_scrolling(GTK_SCROLLED_WINDOW(scroll)));
  gtk_adjustment_set_value(adjustment, 100);
  assert_float_equal(gtk_adjustment_get_value(adjustment), 100, .01);
  GTypeQuery query;
  g_type_query(GDK_TYPE_DEVICE, &query);
  static GType touch_type;
  if(!touch_type)
    touch_type = g_type_register_static_simple(GDK_TYPE_DEVICE, "DtToolbarTestTouch",
                                               query.class_size, NULL, query.instance_size, NULL, 0);
  GdkDevice *touch = g_object_new(touch_type, "display", gdk_display_get_default(),
                                  "name", "test touch", "type", GDK_DEVICE_TYPE_SLAVE,
                                  "input-source", GDK_SOURCE_TOUCHSCREEN,
                                  "input-mode", GDK_MODE_SCREEN, NULL);
  GList *children = gtk_container_get_children(GTK_CONTAINER(controls));
  GdkWindow *event_window = gtk_button_get_event_window(GTK_BUTTON(children->data));
  g_list_free(children);
  for(int i = 0; i < 3; i++)
  {
    GdkEvent *event = gdk_event_new(i == 0 ? GDK_TOUCH_BEGIN : i == 1 ? GDK_TOUCH_UPDATE : GDK_TOUCH_END);
    event->touch.window = g_object_ref(event_window);
    event->touch.sequence = (GdkEventSequence *)scroll;
    event->touch.x = 50 - i * 40;
    event->touch.y = 10;
    event->touch.x_root = 1000 - i * 40;
    event->touch.y_root = 500;
    event->touch.time = 1000 + i * 100;
    event->touch.emulating_pointer = TRUE;
    gdk_event_set_device(event, gdk_seat_get_pointer(gdk_display_get_default_seat(gdk_display_get_default())));
    gdk_event_set_source_device(event, touch);
    gtk_main_do_event(event);
    gdk_event_free(event);
  }
  assert_true(gtk_adjustment_get_value(adjustment) > 100);
  g_object_unref(touch);
  gtk_widget_destroy(window);
}

static void test_top_toolbar_overflow(void **state) { _toolbar_test(TRUE); }
static void test_bottom_toolbar_overflow(void **state) { _toolbar_test(FALSE); }

static void _border_layout_test(gboolean horizontal)
{
  GtkWidget *window = gtk_offscreen_window_new();
  GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  g_object_ref_sink(panel);
  GtkWidget *toggle = _init_outer_border(32, 32,
    horizontal ? DT_UI_BORDER_BOTTOM : DT_UI_BORDER_LEFT);
  GtkWidget *border = _panel_resize_border(panel, toggle,
    horizontal ? "panel-handle-bottom" : "panel-handle-left");
  gtk_container_add(GTK_CONTAINER(window), border);
  gtk_window_set_default_size(GTK_WINDOW(window), horizontal ? 400 : 32, horizontal ? 32 : 400);
  gtk_widget_show_all(window);
  gtk_container_check_resize(GTK_CONTAINER(window));
  GtkAllocation allocation;
  gtk_widget_get_allocation(toggle, &allocation);
  int x, y;
  assert_true(gtk_widget_translate_coordinates(toggle, border, 0, 0, &x, &y));
  assert_int_equal(x + allocation.width / 2, gtk_widget_get_allocated_width(border) / 2);
  assert_int_equal(y + allocation.height / 2, gtk_widget_get_allocated_height(border) / 2);
  GtkWidget *handle = gtk_bin_get_child(GTK_BIN(border));
  assert_int_equal(gtk_widget_get_allocated_width(handle), gtk_widget_get_allocated_width(border));
  assert_int_equal(gtk_widget_get_allocated_height(handle), gtk_widget_get_allocated_height(border));
  gtk_widget_hide(toggle);
  assert_false(gtk_widget_get_visible(border));
  gtk_widget_show(toggle);
  assert_true(gtk_widget_get_visible(border));
  gtk_widget_destroy(window);
  g_object_unref(panel);
}

static void test_vertical_border_layout(void **state) { _border_layout_test(FALSE); }
static void test_horizontal_border_layout(void **state) { _border_layout_test(TRUE); }

static void test_info_text_overflow(void **state)
{
  dt_lib_module_t module = { 0 };
  info_position = "bottom center";
  gui_init(&module);
  GtkWidget *label = ((dt_lib_imageinfo_t *)module.data)->tview;
  gtk_label_set_text(GTK_LABEL(label), "image.raw | 1/125s | f/8 | ISO 100 | camera and lens information");
  assert_int_equal(gtk_label_get_ellipsize(GTK_LABEL(label)), PANGO_ELLIPSIZE_NONE);
  GtkWidget *window = gtk_offscreen_window_new();
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add(GTK_CONTAINER(window), box);
  GtkWidget *scroll = _center_toolbar_scroll(box, module.widget);
  gtk_window_set_default_size(GTK_WINDOW(window), 160, 60);
  gtk_widget_show_all(window);
  gtk_container_check_resize(GTK_CONTAINER(window));
  GtkAdjustment *adjustment = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(scroll));
  int minimum, natural;
  gtk_widget_get_preferred_width(label, &minimum, &natural);
  assert_true(minimum > 160);
  assert_true(gtk_adjustment_get_upper(adjustment) > gtk_adjustment_get_page_size(adjustment));
  gtk_adjustment_set_value(adjustment, 100);
  assert_float_equal(gtk_adjustment_get_value(adjustment), 100, .01);
  gui_cleanup(&module);
  gtk_widget_destroy(window);

  info_position = "top left";
  gui_init(&module);
  label = ((dt_lib_imageinfo_t *)module.data)->tview;
  assert_int_equal(gtk_label_get_ellipsize(GTK_LABEL(label)), PANGO_ELLIPSIZE_MIDDLE);
  g_object_ref_sink(module.widget);
  gui_cleanup(&module);
  gtk_widget_destroy(module.widget);
  g_object_unref(module.widget);
}
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
    cmocka_unit_test(test_cancel_resize), cmocka_unit_test(test_mouse_resize),
    cmocka_unit_test(test_top_toolbar_overflow), cmocka_unit_test(test_bottom_toolbar_overflow),
    cmocka_unit_test(test_vertical_border_layout), cmocka_unit_test(test_horizontal_border_layout),
    cmocka_unit_test(test_info_text_overflow)
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
#endif
}
