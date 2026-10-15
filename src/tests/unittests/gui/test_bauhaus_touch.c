/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "dtgtk/togglebutton.h"

// exercise real GTK gesture recognition against the private widget callbacks
#define dt_key_modifier_state dt_test_key_modifier_state
#include "bauhaus/bauhaus.c"
#undef dt_key_modifier_state

GdkModifierType dt_test_key_modifier_state(void)
{
  return 0;
}

#ifdef _WIN32
#include "win/main_wrapper.h"
#endif

#if !GTK_CHECK_VERSION(4, 0, 0)
typedef struct touch_fixture_t
{
  GtkWidget *window;
  GtkWidget *widget;
  GtkWidget *target;
  GtkGestureSingle *click;
  GtkGesture *drag;
  GdkDevice *touch;
  gboolean propagate;
  guint32 time;
} touch_fixture_t;

static touch_fixture_t _fixture(const gboolean toggle)
{
  touch_fixture_t f = { 0 };
  dt_bauhaus_widget_t *w = g_object_new(DT_BAUHAUS_WIDGET_TYPE, NULL);
  f.widget = toggle ? dt_bauhaus_toggle_from_widget(w, NULL)
                   : dt_bauhaus_slider_from_widget(w, NULL, 0, 1, .01, .5, 2, FALSE);
  f.target = f.widget;
  f.window = gtk_offscreen_window_new();
  gtk_container_add(GTK_CONTAINER(f.window), f.widget);
  gtk_widget_show_all(f.window);
  GtkAllocation allocation = { 0, 0, 400, 60 };
  gtk_widget_size_allocate(f.widget, &allocation);

  // drive these controllers directly, without sending input to the desktop
  f.click = dt_gui_connect_click(f.widget, _widget_button_press, _widget_button_release, f.widget);
  f.drag = dt_gui_connect_drag(f.widget, NULL, _widget_touch_end, _widget_touch_motion, f.widget);
  g_signal_connect(f.drag, "cancel", G_CALLBACK(_widget_touch_cancel), f.widget);
  gtk_gesture_group(GTK_GESTURE(f.click), f.drag);
  static GType touch_type = 0;
  if(!touch_type)
  {
    GTypeQuery query;
    g_type_query(GDK_TYPE_DEVICE, &query);
    touch_type = g_type_register_static_simple(GDK_TYPE_DEVICE, "DtTestTouchDevice",
                                               query.class_size, NULL, query.instance_size, NULL, 0);
  }
  f.touch = g_object_new(touch_type, "display", gdk_display_get_default(),
                          "name", "test touch", "type", GDK_DEVICE_TYPE_SLAVE,
                          "input-source", GDK_SOURCE_TOUCHSCREEN,
                          "input-mode", GDK_MODE_SCREEN, NULL);
  g_object_set_data_full(G_OBJECT(f.window), "test-touch-device", f.touch, g_object_unref);
  return f;
}

static void _touch(touch_fixture_t *f, const GdkEventType type, const double x, const double y)
{
  GdkEvent *event = gdk_event_new(type);
  event->touch.window = g_object_ref(GTK_IS_BUTTON(f->target)
                                    ? gtk_button_get_event_window(GTK_BUTTON(f->target))
                                    : gtk_widget_get_window(f->target));
  event->touch.sequence = (GdkEventSequence *)f;
  event->touch.time = ++f->time;
  event->touch.x = event->touch.x_root = x;
  event->touch.y = event->touch.y_root = y;
  event->touch.emulating_pointer = TRUE;
  GdkDevice *device = gdk_seat_get_pointer(gdk_display_get_default_seat(gdk_display_get_default()));
  gdk_event_set_device(event, device);
  gdk_event_set_source_device(event, f->touch);
  if(f->propagate)
    gtk_main_do_event(event);
  else
  {
    gtk_event_controller_handle_event(GTK_EVENT_CONTROLLER(f->click), event);
    gtk_event_controller_handle_event(GTK_EVENT_CONTROLLER(f->drag), event);
  }
  gdk_event_free(event);
}

static void test_vertical_swipe_preserves_slider(void **state)
{
  touch_fixture_t f = _fixture(FALSE);
  _touch(&f, GDK_TOUCH_BEGIN, 300, 40);
  assert_float_equal(dt_bauhaus_slider_get(f.widget), .5, .001);
  _touch(&f, GDK_TOUCH_UPDATE, 302, 100);
  _touch(&f, GDK_TOUCH_END, 302, 100);
  assert_float_equal(dt_bauhaus_slider_get(f.widget), .5, .001);
  gtk_widget_destroy(f.window);
}

static void _emulated_touch(touch_fixture_t *f, GdkEventType type, double x, double y)
{
  GdkEvent *event = gdk_event_new(type);
  event->any.window = g_object_ref(gtk_widget_get_window(f->widget));
  if(type == GDK_MOTION_NOTIFY)
  {
    event->motion.x = x;
    event->motion.y = y;
    event->motion.time = ++f->time;
    event->motion.state = GDK_BUTTON1_MASK;
  }
  else
  {
    event->button.x = x;
    event->button.y = y;
    event->button.time = ++f->time;
    event->button.button = GDK_BUTTON_PRIMARY;
  }
  gdk_event_set_device(event, gdk_seat_get_pointer(gdk_display_get_default_seat(gdk_display_get_default())));
  gdk_event_set_source_device(event, f->touch);
  gtk_event_controller_handle_event(GTK_EVENT_CONTROLLER(f->click), event);
  gtk_event_controller_handle_event(GTK_EVENT_CONTROLLER(f->drag), event);
  gdk_event_free(event);
}

static void test_emulated_touch_drag_updates_while_held(void **state)
{
  touch_fixture_t f = _fixture(FALSE);
  _emulated_touch(&f, GDK_BUTTON_PRESS, 150, 40);
  assert_true(DT_BAUHAUS_WIDGET(f.widget)->touch_pending);
  assert_float_equal(dt_bauhaus_slider_get(f.widget), .5, .001);
  _emulated_touch(&f, GDK_MOTION_NOTIFY, 230, 42);
  const float previous = dt_bauhaus_slider_get(f.widget);
  assert_true(previous > .6);
  _emulated_touch(&f, GDK_MOTION_NOTIFY, 270, 42);
  assert_true(dt_bauhaus_slider_get(f.widget) > previous);
  _emulated_touch(&f, GDK_BUTTON_RELEASE, 270, 42);
  assert_false(DT_BAUHAUS_WIDGET(f.widget)->touch_dragging);
  assert_false(DT_BAUHAUS_WIDGET(f.widget)->slider.is_dragging);
  gtk_widget_destroy(f.window);
}

static void test_emulated_touch_vertical_swipe_preserves_slider(void **state)
{
  touch_fixture_t f = _fixture(FALSE);
  _emulated_touch(&f, GDK_BUTTON_PRESS, 150, 40);
  _emulated_touch(&f, GDK_MOTION_NOTIFY, 152, 110);
  _emulated_touch(&f, GDK_BUTTON_RELEASE, 152, 110);
  assert_float_equal(dt_bauhaus_slider_get(f.widget), .5, .001);
  gtk_widget_destroy(f.window);
}

static void test_mouse_slider_press_still_edits(void **state)
{
  touch_fixture_t f = _fixture(FALSE);
  GdkDevice *device = gdk_seat_get_pointer(gdk_display_get_default_seat(gdk_display_get_default()));
  GdkEvent *event = gdk_event_new(GDK_BUTTON_PRESS);
  event->button.window = g_object_ref(gtk_widget_get_window(f.widget));
  event->button.button = GDK_BUTTON_PRIMARY;
  event->button.x = 300;
  event->button.y = 40;
  event->button.time = 1;
  gdk_event_set_device(event, device);
  gtk_event_controller_handle_event(GTK_EVENT_CONTROLLER(f.click), event);
  gtk_event_controller_handle_event(GTK_EVENT_CONTROLLER(f.drag), event);
  assert_true(dt_bauhaus_slider_get(f.widget) > .7);
  event->type = GDK_BUTTON_RELEASE;
  event->button.time = 2;
  gtk_event_controller_handle_event(GTK_EVENT_CONTROLLER(f.click), event);
  gtk_event_controller_handle_event(GTK_EVENT_CONTROLLER(f.drag), event);
  gdk_event_free(event);
  assert_false(DT_BAUHAUS_WIDGET(f.widget)->slider.is_dragging);
  gtk_widget_destroy(f.window);
}

static void test_horizontal_swipe_adjusts_slider(void **state)
{
  touch_fixture_t f = _fixture(FALSE);
  _touch(&f, GDK_TOUCH_BEGIN, 150, 40);
  _touch(&f, GDK_TOUCH_UPDATE, 230, 42);
  _touch(&f, GDK_TOUCH_END, 230, 42);
  assert_true(dt_bauhaus_slider_get(f.widget) > .6);
  assert_false(DT_BAUHAUS_WIDGET(f.widget)->slider.is_dragging);
  gtk_widget_destroy(f.window);
}

static void test_drag_end_without_click_release(void **state)
{
  touch_fixture_t f = _fixture(FALSE);
  _touch(&f, GDK_TOUCH_BEGIN, 150, 40);
  _touch(&f, GDK_TOUCH_UPDATE, 230, 42);
  assert_true(DT_BAUHAUS_WIDGET(f.widget)->touch_dragging);
  // a stopped click recognizer no longer receives this sequence's release
  gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(f.click), GTK_PHASE_NONE);
  _touch(&f, GDK_TOUCH_END, 230, 42);
  assert_false(DT_BAUHAUS_WIDGET(f.widget)->touch_dragging);
  assert_false(DT_BAUHAUS_WIDGET(f.widget)->slider.is_dragging);
  gtk_widget_destroy(f.window);
}

static void test_tab_switch_stops_old_slider(void **state)
{
  touch_fixture_t f = _fixture(FALSE);
  GtkWidget *notebook = gtk_notebook_new();
  g_object_ref(f.widget);
  gtk_container_remove(GTK_CONTAINER(f.window), f.widget);
  GtkWidget *first_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add(GTK_CONTAINER(first_page), f.widget);
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), first_page, gtk_label_new("first"));
  g_object_unref(f.widget);
  GtkWidget *next = dt_bauhaus_slider_new_with_range_and_feedback(NULL, 0, 1, .01, .5, 2, FALSE);
  GtkWidget *second_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add(GTK_CONTAINER(second_page), next);
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), second_page, gtk_label_new("second"));
  gtk_container_add(GTK_CONTAINER(f.window), notebook);
  gtk_widget_show_all(f.window);
  gtk_container_check_resize(GTK_CONTAINER(f.window));
  GtkAllocation allocation = { 0, 0, 400, 60 };
  gtk_widget_size_allocate(f.widget, &allocation);
  _touch(&f, GDK_TOUCH_BEGIN, 150, 20);
  _touch(&f, GDK_TOUCH_UPDATE, 230, 22);
  assert_true(DT_BAUHAUS_WIDGET(f.widget)->touch_dragging);
  const float previous = dt_bauhaus_slider_get(f.widget);
  gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), 1);
  assert_false(DT_BAUHAUS_WIDGET(f.widget)->touch_pending);
  assert_false(DT_BAUHAUS_WIDGET(f.widget)->touch_dragging);
  assert_false(DT_BAUHAUS_WIDGET(f.widget)->slider.is_dragging);
  _widget_touch_motion(GTK_GESTURE_DRAG(f.drag), 160, 2, f.widget);
  assert_float_equal(dt_bauhaus_slider_get(f.widget), previous, .001);
  assert_float_equal(dt_bauhaus_slider_get(next), .5, .001);
  gtk_widget_destroy(f.window);
}

static void test_cancel_stops_slider_drag(void **state)
{
  touch_fixture_t f = _fixture(FALSE);
  _touch(&f, GDK_TOUCH_BEGIN, 150, 40);
  _touch(&f, GDK_TOUCH_UPDATE, 230, 42);
  assert_true(DT_BAUHAUS_WIDGET(f.widget)->touch_dragging);
  _touch(&f, GDK_TOUCH_CANCEL, 230, 42);
  assert_false(DT_BAUHAUS_WIDGET(f.widget)->touch_pending);
  assert_false(DT_BAUHAUS_WIDGET(f.widget)->touch_dragging);
  assert_false(DT_BAUHAUS_WIDGET(f.widget)->slider.is_dragging);
  gtk_widget_destroy(f.window);
}

static void test_pointer_motion_does_not_duplicate_touch_drag(void **state)
{
  touch_fixture_t f = _fixture(FALSE);
  _touch(&f, GDK_TOUCH_BEGIN, 150, 40);
  _touch(&f, GDK_TOUCH_UPDATE, 230, 42);
  const float previous = dt_bauhaus_slider_get(f.widget);
  _widget_motion(NULL, 350, 42, f.widget);
  assert_float_equal(dt_bauhaus_slider_get(f.widget), previous, .001);
  _touch(&f, GDK_TOUCH_END, 230, 42);
  gtk_widget_destroy(f.window);
}

static void _test_header_released(GtkGestureSingle *gesture, gint n_press,
                                   gdouble x, gdouble y, guint *releases)
{
  ++*releases;
}

static void test_touch_group_button_after_module_header(void **state)
{
  touch_fixture_t f = _fixture(FALSE);
  gtk_widget_destroy(f.widget);
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *group = dtgtk_togglebutton_new(NULL, 0, NULL);
  GtkGestureSingle *secondary = dt_gui_connect_click_secondary(group, NULL, NULL, NULL);
  (void)secondary;
  GtkWidget *header = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(header), gtk_label_new("module header"));
  static const GtkTargetEntry targets[] = { { "iop", GTK_TARGET_SAME_APP, 0 } };
  gtk_drag_source_set(header, GDK_BUTTON1_MASK, targets, 1, GDK_ACTION_COPY);
  guint releases = 0;
  dt_gui_connect_click(header, NULL, _test_header_released, &releases);
  gtk_widget_set_size_request(group, 100, 50);
  gtk_widget_set_size_request(header, 100, 50);
  gtk_box_pack_start(GTK_BOX(box), group, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(box), header, FALSE, FALSE, 0);
  gtk_container_add(GTK_CONTAINER(f.window), box);
  gtk_widget_show_all(f.window);
  gtk_container_check_resize(GTK_CONTAINER(f.window));
  assert_true(gdk_window_get_events(gtk_widget_get_window(header)) & GDK_TOUCH_MASK);
  f.propagate = TRUE;
  f.target = header;
  _touch(&f, GDK_TOUCH_BEGIN, 30, 25);
  _touch(&f, GDK_TOUCH_END, 30, 25);
  assert_int_equal(releases, 1);
  f.target = group;
  _touch(&f, GDK_TOUCH_BEGIN, 30, 25);
  _touch(&f, GDK_TOUCH_END, 30, 25);
  assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(group)));
  assert_int_equal(releases, 1);
  gtk_widget_destroy(f.window);
}

static void test_swipe_does_not_toggle(void **state)
{
  touch_fixture_t f = _fixture(TRUE);
  _touch(&f, GDK_TOUCH_BEGIN, 100, 20);
  _touch(&f, GDK_TOUCH_UPDATE, 102, 90);
  _touch(&f, GDK_TOUCH_END, 102, 90);
  assert_false(dt_bauhaus_toggle_get(f.widget));
  gtk_widget_destroy(f.window);
}

static void test_tap_toggles_on_release(void **state)
{
  touch_fixture_t f = _fixture(TRUE);
  _touch(&f, GDK_TOUCH_BEGIN, 100, 20);
  assert_false(dt_bauhaus_toggle_get(f.widget));
  _touch(&f, GDK_TOUCH_END, 100, 20);
  assert_true(dt_bauhaus_toggle_get(f.widget));
  gtk_widget_destroy(f.window);
}

static void test_cancel_does_not_toggle(void **state)
{
  touch_fixture_t f = _fixture(TRUE);
  _touch(&f, GDK_TOUCH_BEGIN, 100, 20);
  _touch(&f, GDK_TOUCH_CANCEL, 100, 20);
  assert_false(dt_bauhaus_toggle_get(f.widget));
  gtk_widget_destroy(f.window);
}

static touch_fixture_t _popup_fixture(void)
{
  touch_fixture_t f = _fixture(FALSE);
  dt_bauhaus_combobox_from_widget(DT_BAUHAUS_WIDGET(f.widget), NULL);
  dt_bauhaus_combobox_add(f.widget, "first");
  dt_bauhaus_combobox_add(f.widget, "second");
  dt_bauhaus_combobox_add(f.widget, "third");
  dt_bauhaus_popup_t *pop = &darktable.bauhaus->popup;
  GtkWidget *parent = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_widget_realize(parent);
  pop->window = gtk_window_new(GTK_WINDOW_POPUP);
  gtk_window_set_transient_for(GTK_WINDOW(pop->window), GTK_WINDOW(parent));
  g_signal_connect_swapped(pop->window, "destroy", G_CALLBACK(gtk_widget_destroy), parent);
  pop->area = gtk_drawing_area_new();
  gtk_container_add(GTK_CONTAINER(pop->window), pop->area);
  gtk_widget_realize(pop->window);
  gtk_widget_realize(pop->area);
  GtkAllocation allocation = { 0, 0, 200, 60 };
  gtk_widget_size_allocate(pop->area, &allocation);
  pop->position = (GdkRectangle){ 0, 0, 200, 60 };
  pop->offcut = 0;
  darktable.bauhaus->current = DT_BAUHAUS_WIDGET(f.widget);
  darktable.bauhaus->change_active = FALSE;
  f.target = pop->area;
  f.click = dt_gui_connect_click(pop->area, _popup_button_press_cb, _popup_button_release_cb, NULL);
  f.drag = dt_gui_connect_drag(pop->area, _popup_touch_begin, NULL, _popup_touch_update, NULL);
  gtk_gesture_single_set_touch_only(GTK_GESTURE_SINGLE(f.drag), TRUE);
  gtk_gesture_group(GTK_GESTURE(f.click), f.drag);
  return f;
}

static void _popup_fixture_free(touch_fixture_t *f)
{
  darktable.bauhaus->current = NULL;
  gtk_widget_destroy(darktable.bauhaus->popup.window);
  memset(&darktable.bauhaus->popup, 0, sizeof(dt_bauhaus_popup_t));
  gtk_widget_destroy(f->window);
}

static void test_dropdown_tap_selects_row(void **state)
{
  touch_fixture_t f = _popup_fixture();
  _touch(&f, GDK_TOUCH_BEGIN, 50, 30);
  assert_int_equal(dt_bauhaus_combobox_get(f.widget), 0);
  _touch(&f, GDK_TOUCH_END, 50, 30);
  assert_int_equal(dt_bauhaus_combobox_get(f.widget), 1);
  assert_null(darktable.bauhaus->current);
  _popup_fixture_free(&f);
}

static void test_dropdown_swipe_preserves_selection(void **state)
{
  touch_fixture_t f = _popup_fixture();
  _touch(&f, GDK_TOUCH_BEGIN, 50, 50);
  _touch(&f, GDK_TOUCH_UPDATE, 50, 5);
  _touch(&f, GDK_TOUCH_END, 50, 5);
  assert_int_equal(dt_bauhaus_combobox_get(f.widget), 0);
  assert_non_null(darktable.bauhaus->current);
  _popup_fixture_free(&f);
}

static void test_touch_theme_loads(void **state)
{
  GtkCssProvider *provider = gtk_css_provider_new();
  GError *error = NULL;
  assert_true(gtk_css_provider_load_from_path(provider, DT_TEST_TOUCH_THEME, &error));
  assert_null(error);
  GtkWidget *button = gtk_button_new_with_label("export");
  g_object_ref_sink(button);
  GtkStyleContext *context = gtk_widget_get_style_context(button);
  gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
  gint minimum = 0;
  gtk_style_context_get(context, GTK_STATE_FLAG_NORMAL, "min-height", &minimum, NULL);
  PangoFontDescription *font = NULL;
  gtk_style_context_get(context, GTK_STATE_FLAG_NORMAL, "font", &font, NULL);
  // a text-button rule in the base theme must not shrink touch targets again
  double font_pixels = pango_font_description_get_size(font) / (double)PANGO_SCALE;
  if(!pango_font_description_get_size_is_absolute(font))
    font_pixels *= gdk_screen_get_resolution(gdk_screen_get_default()) / 72.0;
  assert_true(minimum >= 1.7 * font_pixels);
  assert_true(minimum <= 1.8 * font_pixels);
  pango_font_description_free(font);
  gtk_widget_destroy(button);
  g_object_unref(button);
  g_object_unref(provider);
}

static void test_panel_swipe_scrolls_without_editing(void **state)
{
  touch_fixture_t f = _fixture(FALSE);
  g_object_ref(f.widget);
  gtk_container_remove(GTK_CONTAINER(f.window), f.widget);
  GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start(GTK_BOX(box), f.widget, FALSE, FALSE, 0);
  g_object_unref(f.widget);
  GtkWidget *spacer = gtk_drawing_area_new();
  gtk_widget_set_size_request(spacer, 400, 1200);
  gtk_box_pack_start(GTK_BOX(box), spacer, FALSE, FALSE, 0);
  gtk_container_add(GTK_CONTAINER(scroll), box);
  gtk_container_add(GTK_CONTAINER(f.window), scroll);
  gtk_window_resize(GTK_WINDOW(f.window), 400, 200);
  gtk_widget_show_all(f.window);
  gtk_container_check_resize(GTK_CONTAINER(f.window));
  // use the widget's production controllers for the full propagation test
  gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(f.click), GTK_PHASE_NONE);
  gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(f.drag), GTK_PHASE_NONE);
  f.propagate = TRUE;
  _touch(&f, GDK_TOUCH_BEGIN, 150, 20);
  _touch(&f, GDK_TOUCH_UPDATE, 150, -40);
  _touch(&f, GDK_TOUCH_UPDATE, 150, -100);
  _touch(&f, GDK_TOUCH_END, 150, -100);
  GtkAdjustment *adjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll));
  assert_true(gtk_adjustment_get_value(adjustment) > 0);
  assert_float_equal(dt_bauhaus_slider_get(f.widget), .5, .001);
  gtk_widget_destroy(f.window);
}
#endif

int main(int argc, char *argv[])
{
#if GTK_CHECK_VERSION(4, 0, 0)
  return 77;
#else
  if(!gtk_init_check(&argc, &argv)) return 77;
  g_log_set_always_fatal(G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
  darktable.bauhaus = calloc(1, sizeof(dt_bauhaus_t));
  darktable.gui = calloc(1, sizeof(dt_gui_gtk_t));
  darktable.control = calloc(1, sizeof(dt_control_t));
  darktable.bauhaus->line_height = 20;
  darktable.bauhaus->quad_width = 20;
  darktable.bauhaus->pango_font_desc = pango_font_description_from_string("Sans 12");
  const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_vertical_swipe_preserves_slider),
    cmocka_unit_test(test_emulated_touch_drag_updates_while_held),
    cmocka_unit_test(test_emulated_touch_vertical_swipe_preserves_slider),
    cmocka_unit_test(test_mouse_slider_press_still_edits),
    cmocka_unit_test(test_horizontal_swipe_adjusts_slider),
    cmocka_unit_test(test_drag_end_without_click_release),
    cmocka_unit_test(test_tab_switch_stops_old_slider),
    cmocka_unit_test(test_cancel_stops_slider_drag),
    cmocka_unit_test(test_pointer_motion_does_not_duplicate_touch_drag),
    cmocka_unit_test(test_touch_group_button_after_module_header),
    cmocka_unit_test(test_swipe_does_not_toggle),
    cmocka_unit_test(test_tap_toggles_on_release),
    cmocka_unit_test(test_cancel_does_not_toggle),
    cmocka_unit_test(test_dropdown_tap_selects_row),
    cmocka_unit_test(test_dropdown_swipe_preserves_selection),
    cmocka_unit_test(test_touch_theme_loads),
    cmocka_unit_test(test_panel_swipe_scrolls_without_editing)
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
#endif
}
