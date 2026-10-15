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

// observe graph edits without running an image pipeline
#define dt_bauhaus_slider_set_val dt_test_graph_value
#define dt_key_modifier_state dt_test_graph_modifiers
#include "iop/colorequal.c"
#undef dt_bauhaus_slider_set_val
#undef dt_key_modifier_state

#ifdef _WIN32
#include "win/main_wrapper.h"
#endif

static GtkWidget *edited;
static float value;
void dt_test_graph_value(GtkWidget *widget, float val) { edited = widget; value = val; }
GdkModifierType dt_test_graph_modifiers(void) { return 0; }

#if !GTK_CHECK_VERSION(4, 0, 0)
static void _event(GtkWidget *area, GdkDevice *touch, GdkEventType type, double x, double y)
{
  GdkEvent *event = gdk_event_new(type);
  event->touch.window = g_object_ref(gtk_widget_get_window(area));
  event->touch.sequence = (GdkEventSequence *)area;
  event->touch.x = event->touch.x_root = x;
  event->touch.y = event->touch.y_root = y;
  event->touch.emulating_pointer = TRUE;
  static guint32 time = 1000;
  event->touch.time = time += 100;
  if(type == GDK_BUTTON_PRESS || type == GDK_BUTTON_RELEASE)
  {
    event->button.x = event->button.x_root = x;
    event->button.y = event->button.y_root = y;
    event->button.time = time;
    event->button.button = GDK_BUTTON_PRIMARY;
  }
  else if(type == GDK_MOTION_NOTIFY)
  {
    event->motion.x = event->motion.x_root = x;
    event->motion.y = event->motion.y_root = y;
    event->motion.time = time;
    event->motion.state = GDK_BUTTON1_MASK;
  }
  gdk_event_set_device(event, gdk_seat_get_pointer(gdk_display_get_default_seat(gdk_display_get_default())));
  gdk_event_set_source_device(event, touch);
  gtk_main_do_event(event);
  gdk_event_free(event);
}

static void _graph_test(gboolean cancel, gboolean off_node, gboolean emulated)
{
  dt_iop_module_t self = { 0 };
  dt_iop_colorequal_gui_data_t g = { 0 };
  self.gui_data = &g;
  g.channel = SATURATION;
  g.graph_height = 200;
  GtkWidget *window = gtk_offscreen_window_new();
  GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
  gtk_scrolled_window_set_capture_button_press(GTK_SCROLLED_WINDOW(scroll), FALSE);
  gtk_container_add(GTK_CONTAINER(window), scroll);
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add(GTK_CONTAINER(scroll), box);
  g.area = GTK_DRAWING_AREA(gtk_drawing_area_new());
  gtk_widget_set_can_focus(GTK_WIDGET(g.area), TRUE);
  gtk_widget_set_size_request(GTK_WIDGET(g.area), 400, 200);
  gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(g.area), FALSE, FALSE, 0);
  GtkWidget *spacer = gtk_drawing_area_new();
  gtk_widget_set_size_request(spacer, 400, 1000);
  gtk_box_pack_start(GTK_BOX(box), spacer, FALSE, FALSE, 0);
  for(int i = 0; i < NODES; i++)
  {
    g.points[i][0] = 20 + i * 40;
    g.points[i][1] = 100;
    g.sat_sliders[i] = GTK_WIDGET(g.area);
  }
  _area_connect_touch(&self);
  gtk_window_set_default_size(GTK_WINDOW(window), 420, 220);
  gtk_widget_show_all(window);
  gtk_container_check_resize(GTK_CONTAINER(window));
  GTypeQuery query;
  g_type_query(GDK_TYPE_DEVICE, &query);
  static GType touch_type;
  if(!touch_type)
    touch_type = g_type_register_static_simple(GDK_TYPE_DEVICE, "DtColorEqualTouch",
                                               query.class_size, NULL, query.instance_size, NULL, 0);
  GdkDevice *touch = g_object_new(touch_type, "display", gdk_display_get_default(),
                                  "name", "test touch", "type", GDK_DEVICE_TYPE_SLAVE,
                                  "input-source", GDK_SOURCE_TOUCHSCREEN,
                                  "input-mode", GDK_MODE_SCREEN, NULL);
  edited = NULL;
  _event(GTK_WIDGET(g.area), touch, emulated ? GDK_BUTTON_PRESS : GDK_TOUCH_BEGIN,
          140, off_node ? 160 : 116);
  if(!off_node)
  {
    assert_int_equal(g.selected, 3);
    assert_true(g.dragging);
    _event(GTK_WIDGET(g.area), touch, emulated ? GDK_MOTION_NOTIFY : GDK_TOUCH_UPDATE, 140, 60);
    assert_true(g.dragging);
    assert_true(g.on_node);
    assert_ptr_equal(edited, g.sat_sliders[3]);
    assert_float_equal(value, 40, .01);
  }
  else
  {
    assert_false(g.dragging);
    _event(GTK_WIDGET(g.area), touch, GDK_TOUCH_UPDATE, 140, 60);
  }
  _event(GTK_WIDGET(g.area), touch,
          cancel ? GDK_TOUCH_CANCEL : emulated ? GDK_BUTTON_RELEASE : GDK_TOUCH_END, 140, 60);
  assert_false(g.dragging);
  assert_true(gtk_scrolled_window_get_kinetic_scrolling(GTK_SCROLLED_WINDOW(scroll)));
  if(off_node)
    assert_true(gtk_adjustment_get_value(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll))) > 0);
  if(off_node) assert_null(edited);
  gtk_widget_destroy(window);
  g_object_unref(touch);
}
static void test_touch_selects_and_drags_node(void **state) { _graph_test(FALSE, FALSE, FALSE); }
static void test_touch_cancel_stops_drag(void **state) { _graph_test(TRUE, FALSE, FALSE); }
static void test_touch_outside_node_does_not_edit(void **state) { _graph_test(FALSE, TRUE, FALSE); }
static void test_emulated_touch_drags_node(void **state) { _graph_test(FALSE, FALSE, TRUE); }
#endif

int main(int argc, char **argv)
{
#if GTK_CHECK_VERSION(4, 0, 0)
  return 77;
#else
  if(!gtk_init_check(&argc, &argv)) return 77;
  g_log_set_always_fatal(G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
  darktable.gui = calloc(1, sizeof(dt_gui_gtk_t));
  darktable.gui->dpi_factor = 1;
  darktable.control = calloc(1, sizeof(dt_control_t));
  const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_touch_selects_and_drags_node),
    cmocka_unit_test(test_touch_cancel_stops_drag),
    cmocka_unit_test(test_touch_outside_node_does_not_edit),
    cmocka_unit_test(test_emulated_touch_drags_node)
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
#endif
}
