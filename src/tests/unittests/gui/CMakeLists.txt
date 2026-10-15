add_cmocka_test(test_bauhaus_touch
                SOURCES test_bauhaus_touch.c
                LINK_LIBRARIES lib_darktable cmocka)
target_compile_definitions(test_bauhaus_touch PRIVATE
    DT_TEST_TOUCH_THEME="${CMAKE_SOURCE_DIR}/data/themes/darktable-touch.css")

set_tests_properties(test_bauhaus_touch PROPERTIES SKIP_RETURN_CODE 77)

if(WIN32)
    _copy_required_library(test_bauhaus_touch lib_darktable)
endif(WIN32)
