add_cmocka_test(test_bauhaus_touch
                SOURCES test_bauhaus_touch.c
                LINK_LIBRARIES lib_darktable cmocka)
target_compile_definitions(test_bauhaus_touch PRIVATE
    DT_TEST_TOUCH_THEME="${CMAKE_SOURCE_DIR}/data/themes/darktable-touch.css")

set_tests_properties(test_bauhaus_touch PROPERTIES SKIP_RETURN_CODE 77)

if(WIN32)
    _copy_required_library(test_bauhaus_touch lib_darktable)
endif(WIN32)

add_cmocka_test(test_panel_touch
    SOURCES test_panel_touch.c
    LINK_LIBRARIES lib_darktable cmocka)
set_tests_properties(test_panel_touch PROPERTIES SKIP_RETURN_CODE 77)
if(WIN32)
    target_link_libraries(test_panel_touch PRIVATE dwmapi)
    _copy_required_library(test_panel_touch lib_darktable)
endif(WIN32)

add_cmocka_test(test_histogram_touch
    SOURCES test_histogram_touch.c
            ${CMAKE_SOURCE_DIR}/src/libs/scopes/histogram.c
            ${CMAKE_SOURCE_DIR}/src/libs/scopes/waveform.c
            ${CMAKE_SOURCE_DIR}/src/libs/scopes/vectorscope.c
            ${CMAKE_SOURCE_DIR}/src/libs/scopes/split.c
    LINK_LIBRARIES lib_darktable cmocka)
target_include_directories(test_histogram_touch PRIVATE ${CMAKE_SOURCE_DIR}/src/libs)
set_tests_properties(test_histogram_touch PROPERTIES SKIP_RETURN_CODE 77)
if(WIN32)
    _copy_required_library(test_histogram_touch lib_darktable)
endif(WIN32)
