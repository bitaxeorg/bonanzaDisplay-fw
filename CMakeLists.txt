cmake_minimum_required(VERSION 3.13)

# Pull in Pico SDK (must be before project())
include(pico_sdk_import.cmake)

# The firmware version lives in the VERSION file; the build id is the git
# revision at configure time (re-run cmake after committing to refresh it).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" BONANZA_FW_VERSION LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
execute_process(
    COMMAND git describe --always --dirty --abbrev=7 --exclude=*
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE BONANZA_FW_BUILD
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT BONANZA_FW_BUILD)
    set(BONANZA_FW_BUILD "unknown")
endif()

project(bonanzaDisplay VERSION ${BONANZA_FW_VERSION} LANGUAGES C CXX ASM)
message(STATUS "bonanzaDisplay firmware ${BONANZA_FW_VERSION} (${BONANZA_FW_BUILD})")

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
option(BONANZA_DISPLAY_BRINGUP_TEST "Build simple serial + bitbang SSD1322 bring-up firmware" OFF)

# Initialize the Pico SDK
pico_sdk_init()

# --------------------------------------------------------------------------
# LVGL
# --------------------------------------------------------------------------
set(LV_BUILD_CONF_DIR "${CMAKE_CURRENT_SOURCE_DIR}/config" CACHE STRING "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "Do not build LVGL examples" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "Do not build LVGL demos" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "No vector engine on RP2350" FORCE)
add_subdirectory(lib/lvgl)

# LVGL Editor exports the UI as a standalone CMake library.
add_subdirectory(ui)

# Remove Helium (ARMv8.1-M MVE) assembly — not supported on RP2350 Cortex-M33
get_target_property(LVGL_SOURCES lvgl SOURCES)
list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*helium.*\\.S$")
set_target_properties(lvgl PROPERTIES SOURCES "${LVGL_SOURCES}")

# --------------------------------------------------------------------------
# Main executable
# --------------------------------------------------------------------------
add_executable(${PROJECT_NAME}
    src/main.c
    src/control/bap_client.c
    src/control/bap_control.c
    src/display/ssd1322.c
    src/input/dial_switch.c
    src/storage/settings_store.c
    src/lvgl_port/lv_port_disp.c
    src/lvgl_port/lv_port_indev.c
    src/fonts/lv_font_portfolio_6x8.c
)

# --------------------------------------------------------------------------
# PIO program (must be after add_executable)
# --------------------------------------------------------------------------
pico_generate_pio_header(${PROJECT_NAME}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/display/parallel_8080.pio
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control
    ${CMAKE_CURRENT_SOURCE_DIR}/src/display
    ${CMAKE_CURRENT_SOURCE_DIR}/src/input
    ${CMAKE_CURRENT_SOURCE_DIR}/src/storage
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lvgl_port
    ${CMAKE_CURRENT_SOURCE_DIR}/src/fonts
    ${CMAKE_CURRENT_SOURCE_DIR}/config
)

target_link_libraries(${PROJECT_NAME}
    pico_stdlib
    hardware_gpio
    hardware_pio
    hardware_dma
    hardware_flash
    hardware_sync
    hardware_timer
    hardware_uart
    lvgl
    lib-ui
)

if(BONANZA_DISPLAY_BRINGUP_TEST)
    target_compile_definitions(${PROJECT_NAME} PRIVATE BONANZA_DISPLAY_BRINGUP_TEST=1)
endif()

# Shown on the loading screen and by `picotool info`.
target_compile_definitions(${PROJECT_NAME} PRIVATE
    BONANZA_FW_VERSION="${BONANZA_FW_VERSION}"
    BONANZA_FW_BUILD="${BONANZA_FW_BUILD}")
pico_set_program_version(${PROJECT_NAME} "${BONANZA_FW_VERSION} (${BONANZA_FW_BUILD})")

# USB-only printf debugging; UART1 is reserved for BAP
pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

# Generate UF2 output
pico_add_extra_outputs(${PROJECT_NAME})
