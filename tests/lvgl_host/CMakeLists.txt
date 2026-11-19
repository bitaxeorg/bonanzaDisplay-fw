# Host build of LVGL plus the UI library, with the firmware's own lv_conf.h,
# to check the LVGL glue in ui/bonanza_ui.c without a board. Run through
# tools/test_ui_lvgl.sh.
cmake_minimum_required(VERSION 3.13)
project(bonanza_ui_lvgl_test C CXX ASM)
set(CMAKE_C_STANDARD 11)

get_filename_component(FW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

set(LV_BUILD_CONF_DIR "${FW_DIR}/config" CACHE STRING "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory(${FW_DIR}/lib/lvgl lvgl)
get_target_property(LVGL_SOURCES lvgl SOURCES)
list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*\\.S$")
set_target_properties(lvgl PROPERTIES SOURCES "${LVGL_SOURCES}")

add_subdirectory(${FW_DIR}/ui ui)

add_executable(test_ui_lvgl
    ${FW_DIR}/tests/test_ui_lvgl.c
    ${FW_DIR}/src/fonts/lv_font_portfolio_6x8.c)
target_link_libraries(test_ui_lvgl lvgl lib-ui)
