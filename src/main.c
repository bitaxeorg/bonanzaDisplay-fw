/**
 * @file main.c
 * bonanzaDisplay — Main entry point
 *
 * Bare-metal super loop driving SSD1322 OLED via LVGL on RPi Pico 2W.
 */

#include <stdio.h>
#include <stdarg.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico/time.h"
#include "hardware/timer.h"
#include "hardware/clocks.h"

#include "lvgl.h"
#include "ssd1322.h"
#include "ssd1322_regs.h"
#include "lv_port_disp.h"
#include "pin_config.h"
#include "dial_switch.h"
#include "bonanza_ui.h"
#include "bap_control.h"
#include "bap_ui_bridge.h"
#include "settings_store.h"

// Set by CMake from the VERSION file and the git revision.
#ifndef BONANZA_FW_VERSION
#define BONANZA_FW_VERSION "0.0.0"
#endif
#ifndef BONANZA_FW_BUILD
#define BONANZA_FW_BUILD "unknown"
#endif

// ==========================================================================
// LVGL Tick — Pico SDK monotonic ms clock
// ==========================================================================

static uint32_t pico_tick_get_cb(void) {
    return to_ms_since_boot(get_absolute_time());
}

// ==========================================================================
// UI platform hooks — settings storage and panel brightness
// ==========================================================================

// SSD1322 contrast current per UI brightness level; the top level is the
// driver's power-on default.
static const uint8_t brightness_contrast[BZ_BRIGHTNESS_LEVELS] = {0x18, 0x40, 0x70, 0x9F};
static bool contrast_pending;
static uint8_t contrast_value;

// The panel shares its bus with frame transfers, so the command is only
// queued here and sent from the main loop between frames.
static void ui_set_brightness(uint8_t level) {
    if (level >= BZ_BRIGHTNESS_LEVELS) level = BZ_BRIGHTNESS_LEVELS - 1;
    contrast_value = brightness_contrast[level];
    contrast_pending = true;
}

static bool ui_save_settings(const uint8_t *blob, size_t size) {
    // Let the frame in flight finish so its completion interrupt is not held
    // off by the flash write.
    while (ssd1322_is_busy()) tight_loop_contents();
    return settings_store_save(blob, size);
}

static const bz_platform_t ui_platform = {
    .load = settings_store_load,
    .save = ui_save_settings,
    .set_brightness = ui_set_brightness,
    .version = BONANZA_FW_VERSION,
    .build = BONANZA_FW_BUILD,
};

// ==========================================================================
// Debug Logging
// ==========================================================================

static void debug_log(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    stdio_flush();
}

static void wait_for_usb_serial(void) {
    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }

    // Give the host-side terminal a moment to finish configuring the port.
    sleep_ms(250);
}

#if BONANZA_DISPLAY_BRINGUP_TEST
static void run_display_bringup_test(void) {
    debug_log("\n=== bonanzaDisplay SSD1322 bring-up test ===\n");
    debug_log("Boot ms: %lu\n", (unsigned long)to_ms_since_boot(get_absolute_time()));
    debug_log("System clock: %lu MHz\n", clock_get_hz(clk_sys) / 1000000);
    debug_log("Data bus: D0=GPIO%d D1=GPIO%d D2=GPIO%d D3=GPIO%d D4=GPIO%d D5=GPIO%d D6=GPIO%d D7=GPIO%d\n",
              PIN_DB0, PIN_DB1, PIN_DB2, PIN_DB3, PIN_DB4, PIN_DB5, PIN_DB6, PIN_DB7);
    debug_log("Control: RD#=GPIO%d WR#=GPIO%d DC#=GPIO%d CS#=GPIO%d RES#=GPIO%d\n",
              PIN_RD, PIN_WR, PIN_DC, PIN_CS, PIN_RES);

    debug_log("Initializing SSD1322 with GPIO bit-bang path...\n");
    ssd1322_init_bitbang();
    stdio_flush();
    debug_log("Bit-bang init returned.\n");

    ssd1322_debug_gpio_state();
    stdio_flush();

    uint32_t cycle = 0;
    const uint8_t patterns[] = {
        0xFF,  // all white
        0x00,  // all black
        0xF0,  // alternating bright/dark pixels
        0x0F,  // alternating dark/bright pixels
        0xAA,  // checker-ish nibble pattern
        0x55
    };

    while (1) {
        debug_log("Cycle %lu: SSD1322 all-on command (0xA5)\n", (unsigned long)cycle);
        ssd1322_bitbang_cmd(0xA5);
        sleep_ms(1000);

        debug_log("Cycle %lu: normal display command (0xA6)\n", (unsigned long)cycle);
        ssd1322_bitbang_cmd(0xA6);
        sleep_ms(250);

        for (uint32_t i = 0; i < sizeof(patterns); i++) {
            debug_log("Cycle %lu: visible-window fill pattern 0x%02X\n",
                      (unsigned long)cycle, patterns[i]);
            ssd1322_test_bitbang_fill(patterns[i]);
            stdio_flush();
            sleep_ms(1000);
        }

        debug_log("Cycle %lu complete.\n", (unsigned long)cycle);
        cycle++;
    }
}
#endif

// ==========================================================================
// Main
// ==========================================================================

int main(void) {
    // --- Platform init ---
    stdio_init_all();

#if BONANZA_DISPLAY_BRINGUP_TEST
    wait_for_usb_serial();
    run_display_bringup_test();
#else
    sleep_ms(500);

    debug_log("\n=== bonanzaDisplay %s (%s) ===\n", BONANZA_FW_VERSION, BONANZA_FW_BUILD);
    debug_log("Boot ms: %lu\n", (unsigned long)to_ms_since_boot(get_absolute_time()));
    debug_log("System clock: %lu MHz\n\n", clock_get_hz(clk_sys) / 1000000);

    // --- Display hardware init (PIO + DMA) ---
    debug_log("Init SSD1322 (PIO 8080 mode)...\n");
    ssd1322_init();
    debug_log("SSD1322 initialized.\n");

    // --- Dial switch init ---
    debug_log("Init dial switch...\n");
    dial_switch_init();
    debug_log("Dial switch ready: CCW=GPIO%d CW=GPIO%d SW=GPIO%d\n",
              PIN_DIAL_CCW, PIN_DIAL_CW, PIN_DIAL_SW);

    // --- bitaxe BAP control interface ---
    debug_log("Init BAP UART1: %u baud TX=GPIO%d RX=GPIO%d...\n",
              BONANZA_BAP_BAUD_RATE, PIN_CONTROL_TX, PIN_CONTROL_RX);
    bonanza_bap_control_init();
    debug_log("BAP control interface ready.\n");

    // Quick blink to confirm display alive
    debug_log("SSD1322 all-on blink...\n");
    ssd1322_write_cmd(0xA5);  // ALL_ON
    sleep_ms(200);
    ssd1322_write_cmd(0xA6);  // Normal mode
    sleep_ms(100);
    debug_log("SSD1322 blink complete.\n");

    // --- LVGL init ---
    debug_log("Init LVGL...\n");
    lv_init();
    lv_tick_set_cb(pico_tick_get_cb);
    lv_port_disp_init();
    debug_log("LVGL ready.\n");

    // Fonts and images generated by LVGL Editor from ui/*.xml, then the UI:
    // loading screen, mining pages and setup menu.
    bonanza_ui_init(NULL);
    bonanza_ui_start(&ui_platform);

    debug_log("Entering LVGL main loop...\n");

    // --- Main loop ---
    uint32_t frame = 0;
    uint32_t last_heartbeat_ms = to_ms_since_boot(get_absolute_time());
    int32_t last_dial_position = dial_switch_get_state()->position;
    while (1) {
        bonanza_bap_control_poll();
        bonanza_metrics_t metrics;
        if (bonanza_bap_control_take_update(&metrics)) {
            bz_data_t data;
            bonanza_ui_data_from_metrics(&metrics, &data);
            bonanza_ui_set_data(&data);
        }

        dial_switch_update();
        const dial_switch_state_t *dial = dial_switch_get_state();
        bonanza_ui_input(dial->position - last_dial_position, dial->switch_pressed);
        last_dial_position = dial->position;

        if (contrast_pending && !ssd1322_is_busy()) {
            ssd1322_set_contrast(contrast_value);
            contrast_pending = false;
        }

        const uint32_t wait_ms = lv_timer_handler();
        sleep_ms(wait_ms > 5 ? 5 : (wait_ms == 0 ? 1 : wait_ms));

        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        if (now_ms - last_heartbeat_ms >= 1000) {
            debug_log("Heartbeat: frame=%lu uptime_ms=%lu\n",
                      (unsigned long)frame, (unsigned long)now_ms);
            last_heartbeat_ms = now_ms;
        }
        frame++;
    }
#endif

    return 0;
}
