// Desktop simulator for the bonanzaDisplay UI.
//
// Runs the real UI (ui/app) and the real BAP client against a pretend
// ESP-Miner host, on a virtual clock. Two front ends:
//
//   Scripted: arguments are steps run in order, e.g.
//       sim host:boot run:8000 shot:mining.png click run:300 shot:menu.png
//   Interactive (built with SIM_SDL, run without arguments): a window driven
//   from the keyboard.
//
// Steps: host:off|boot|up|ap|restart   pretend host state
//        run:<ms>                      advance time
//        turn:<n>  click  hold         dial input (n may be negative)
//        shot:<file.png>               save the panel, 4x enlarged
//        strip:<file.png>:<n>:<ms>     n frames, ms apart, stacked in one image
//        snap  sheet:<file.png>        collect frames, then stack them in one image
//        tint:green|gray               colour of later images (default gray)
//        expect:boot|mining|menu       fail unless the UI is in that mode
//        set:<page>:<digits>:<m,m,..>  store settings (metric ids) and power-cycle
//        reboot  status

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "app/bz_app.h"
#include "bap_client.h"
#include "bap_ui_bridge.h"

#define TICK_MS 33u
#define SCALE 4

// ---- Platform hooks ---------------------------------------------------------------

static uint8_t stored_blob[BZ_SETTINGS_BLOB_SIZE];
static bool stored_valid;
static unsigned save_count;
static int brightness = -1;

static bool sim_load(uint8_t *blob, size_t size) {
    if (!stored_valid || size != sizeof(stored_blob)) return false;
    memcpy(blob, stored_blob, size);
    return true;
}

static bool sim_save(const uint8_t *blob, size_t size) {
    if (size != sizeof(stored_blob)) return false;
    memcpy(stored_blob, blob, size);
    stored_valid = true;
    save_count++;
    return true;
}

static void sim_brightness(uint8_t level) {
    brightness = level;
}

// tools/sim.sh passes the same version and build id as the firmware build.
#ifndef BONANZA_FW_VERSION
#define BONANZA_FW_VERSION "0.0.0"
#endif
#ifndef BONANZA_FW_BUILD
#define BONANZA_FW_BUILD "sim"
#endif

static const bz_platform_t platform = {sim_load, sim_save, sim_brightness, BONANZA_FW_VERSION,
                                       BONANZA_FW_BUILD};

// ---- Pretend host -----------------------------------------------------------------

typedef enum { HOST_OFF, HOST_BOOT, HOST_UP, HOST_AP, HOST_RESTART } host_mode_t;

static bonanza_bap_client_t client;
static uint32_t now_ms = 1000;
static host_mode_t host_mode = HOST_OFF;
static uint32_t host_since;
static uint32_t host_next_update;
static uint32_t lcg = 12345;

static uint32_t host_age(void) {
    return now_ms - host_since;
}

static void host_send(const char *command, const char *parameter, const char *value) {
    char frame[BONANZA_BAP_MAX_MESSAGE];
    size_t n = bonanza_bap_format(frame, sizeof(frame), command, parameter, value);
    if (n) bonanza_bap_client_receive(&client, (const uint8_t *)frame, n, now_ms);
}

static void host_sendf(const char *parameter, const char *format, double value) {
    char text[32];
    snprintf(text, sizeof(text), format, value);
    host_send("RES", parameter, text);
}

static double jitter(double amount) {
    lcg = lcg * 1664525u + 1013904223u;
    return ((double)(lcg >> 8) / (double)(1u << 24) - 0.5) * 2.0 * amount;
}

// A booting host answers straight away but takes a while to join WiFi and
// longer still to start hashing.
static bool host_has_wifi(void) {
    return host_mode == HOST_UP || (host_mode == HOST_BOOT && host_age() >= 2500);
}

static double host_hashrate(void) {
    if (host_mode == HOST_UP) return 3000.0 + jitter(120);
    if (host_mode != HOST_BOOT || host_age() < 5000) return 0;
    double ramp = (double)(host_age() - 5000) / 4000.0;
    return (ramp > 1 ? 1 : ramp) * 3000.0 + jitter(80);
}

static void host_telemetry(void) {
    double hashrate = host_hashrate();
    host_sendf("hashrate", "%.2f", hashrate < 0 ? 0 : hashrate);
    host_sendf("chipTemp", "%f", 58.25 + jitter(1.5));
    host_sendf("vrTemp", "%f", 47.0 + jitter(1));
    host_sendf("power", "%.2f", hashrate > 0 ? 55.0 + jitter(1.2) : 6.2);
    host_sendf("voltage", "%.2f", 12010 + jitter(25));
    host_sendf("current", "%.2f", hashrate > 0 ? 4580 + jitter(90) : 515);
    host_sendf("fan_speed", "%.0f", 4200 + jitter(80));
    host_send("RES", "best_difficulty", "4.29G");
    char shares[32];
    snprintf(shares, sizeof(shares), "%lu/3", (unsigned long)(host_age() / 4000u));
    host_send("RES", "shares", shares);
    host_send("RES", "block_height", "921034");
    if (host_has_wifi()) {
        host_send("RES", "wifi_ssid", "bitaxe-net");
        host_send("RES", "wifi_password", "not-shown");
        host_sendf("wifi_rssi", "%.0f", -58 + jitter(3));
        host_send("RES", "wifi_ip", "192.168.1.234");
    }
}

// Reacts to one sentence from the display, as ESP-Miner would.
static void host_receive(const char *sentence) {
    if (host_mode == HOST_OFF || host_mode == HOST_RESTART) return;
    if (host_mode == HOST_AP) {
        host_send("ERR", "systemInfo", strstr(sentence, ",REQ,") ? "ap_mode_no_requests"
                                                                  : "ap_mode_no_subscriptions");
        return;
    }
    if (strstr(sentence, "REQ,systemInfo")) {
        host_send("RES", "deviceModel", "Bonanza");
        host_send("RES", "asicModel", "BZM");
        host_send("RES", "pool", "public-pool.io");
        host_send("RES", "poolPort", "21496");
        host_send("RES", "poolUser", "bc1qexample.bonanza");
    }
}

static void host_poll(void) {
    if ((int32_t)(now_ms - host_next_update) < 0) return;
    host_next_update = now_ms + 1000;
    if (host_mode == HOST_AP) host_send("CMD", "mode", "ap_mode");
    else if (host_mode == HOST_UP || (host_mode == HOST_BOOT && host_age() >= 600)) host_telemetry();
}

static void host_set(host_mode_t mode) {
    host_mode = mode;
    host_since = now_ms;
    host_next_update = now_ms;
    if (mode == HOST_RESTART) host_send("STA", "status", "restarting");
}

// ---- Simulation loop ----------------------------------------------------------------

static int dial_steps;
static bool dial_pressed;

static void step(void) {
    now_ms += TICK_MS;
    char message[BONANZA_BAP_MAX_MESSAGE];
    if (bonanza_bap_client_next_message(&client, now_ms, message, sizeof(message)))
        host_receive(message);
    host_poll();
    bonanza_metrics_t metrics;
    if (bonanza_bap_client_take_update(&client, &metrics)) {
        bz_data_t data;
        bonanza_ui_data_from_metrics(&metrics, &data);
        bz_app_set_data(&data);
    }
    bz_app_input(dial_steps, dial_pressed, now_ms);
    dial_steps = 0;
    bz_app_tick(now_ms);
}

static void run_ms(uint32_t ms) {
    for (uint32_t end = now_ms + ms; (int32_t)(end - now_ms) > 0;) step();
}

static void press(uint32_t held_ms) {
    dial_pressed = true;
    run_ms(held_ms);
    dial_pressed = false;
    run_ms(2 * TICK_MS);
}

// ---- PNG output ---------------------------------------------------------------------

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t size) {
    uint8_t header[8];
    put32(header, size);
    memcpy(header + 4, type, 4);
    fwrite(header, 1, 8, f);
    if (size) fwrite(data, 1, size, f);
    uint32_t crc = (uint32_t)crc32(crc32(0, (const Bytef *)type, 4), data, size);
    put32(header, crc);
    fwrite(header, 1, 4, f);
}

// Writes `frames` panel images stacked vertically, each SCALE times enlarged
// with a dark gutter between them. Pixels are quantised to the panel's 16 levels.
// Colour of a fully lit pixel. The bonanzaDisplay panel is green.
static const uint8_t tint_green[3] = {110, 255, 90};
static const uint8_t tint_gray[3] = {255, 255, 255};
static const uint8_t *image_tint = tint_gray;

static bool write_png(const char *path, const uint8_t *frames_l8, int frames) {
    const int gap = frames > 1 ? SCALE * 2 : 0;
    const int w = BZ_W * SCALE, h = frames * BZ_H * SCALE + (frames - 1) * gap;
    const size_t stride = (size_t)w * 3 + 1;
    size_t raw_size = (size_t)h * stride;
    uint8_t *raw = malloc(raw_size);
    uLongf packed_size = compressBound((uLong)raw_size);
    uint8_t *packed = malloc(packed_size);
    if (!raw || !packed) return false;
    memset(raw, 0x28, raw_size);
    for (int f = 0; f < frames; ++f) {
        for (int y = 0; y < BZ_H * SCALE; ++y) {
            uint8_t *row = raw + (size_t)(f * (BZ_H * SCALE + gap) + y) * stride;
            const uint8_t *src = frames_l8 + (size_t)f * BZ_W * BZ_H + (size_t)(y / SCALE) * BZ_W;
            row[0] = 0;
            for (int x = 0; x < w; ++x) {
                unsigned level = src[x / SCALE] >> 4; // 0..15
                for (int c = 0; c < 3; ++c) row[1 + x * 3 + c] = (uint8_t)(level * image_tint[c] / 15);
            }
        }
    }
    for (int y = 0; y < h; ++y) raw[(size_t)y * stride] = 0; // Filter: none
    compress2(packed, &packed_size, raw, (uLong)raw_size, 9);

    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ihdr[13] = {0};
    put32(ihdr, (uint32_t)w);
    put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; // 8 bits per channel
    ihdr[9] = 2; // RGB
    chunk(f, "IHDR", ihdr, sizeof(ihdr));
    chunk(f, "IDAT", packed, (uint32_t)packed_size);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(packed);
    return true;
}

// ---- Front ends -----------------------------------------------------------------------

static void start(void) {
    bonanza_bap_client_init(&client, now_ms);
    bz_app_init(&platform, now_ms);
}

static const char *mode_name(void) {
    bz_mode_t mode = bz_app_mode();
    return mode == BZ_MODE_BOOT ? "boot" : mode == BZ_MODE_MINING ? "mining" : "menu";
}

#ifdef SIM_SDL
static int interactive(void);
#endif

static int fail(const char *step_text, const char *why) {
    fprintf(stderr, "sim: %s: %s\n", step_text, why);
    return 1;
}

int main(int argc, char **argv) {
#ifdef SIM_SDL
    if (argc == 1) return interactive();
#endif
    if (argc == 1) {
        fprintf(stderr, "usage: sim <step>...   (steps are listed at the top of sim/sim_main.c)\n");
        return 2;
    }
    static uint8_t sheet[32][BZ_W * BZ_H];
    int snaps = 0;
    start();
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (!strncmp(arg, "host:", 5)) {
            const char *names[] = {"off", "boot", "up", "ap", "restart"};
            int mode = -1;
            for (int j = 0; j < 5; ++j)
                if (!strcmp(arg + 5, names[j])) mode = j;
            if (mode < 0) return fail(arg, "unknown host state");
            host_set((host_mode_t)mode);
        } else if (!strncmp(arg, "run:", 4)) {
            run_ms((uint32_t)strtoul(arg + 4, NULL, 10));
        } else if (!strncmp(arg, "turn:", 5)) {
            int n = atoi(arg + 5);
            for (int j = 0; j < abs(n); ++j) {
                dial_steps = n < 0 ? -1 : 1;
                step();
            }
        } else if (!strcmp(arg, "click")) {
            press(100);
        } else if (!strcmp(arg, "hold")) {
            press(900);
        } else if (!strncmp(arg, "set:", 4)) {
            // set:<page>:<digits>:<m0,m1,...> stores settings, then power-cycles.
            bz_settings_t s;
            bz_settings_defaults(&s);
            char *end;
            s.page = (uint8_t)strtoul(arg + 4, &end, 10);
            s.digits = (uint8_t)strtoul(*end ? end + 1 : end, &end, 10);
            for (int slot = 0; *end && slot < BZ_MAX_SLOTS; ++slot)
                s.slots[s.page][slot] = (uint8_t)strtoul(end + 1, &end, 10);
            bz_settings_encode(&s, stored_blob);
            stored_valid = true;
            start();
        } else if (!strcmp(arg, "reboot")) {
            start(); // Power-cycle the display; saved settings survive.
        } else if (!strncmp(arg, "shot:", 5)) {
            if (!write_png(arg + 5, bz_fb, 1)) return fail(arg, "cannot write image");
        } else if (!strncmp(arg, "strip:", 6)) {
            char path[512];
            int frames = 0, interval = 0;
            if (sscanf(arg + 6, "%511[^:]:%d:%d", path, &frames, &interval) != 3 || frames < 1 ||
                frames > 64) return fail(arg, "expected strip:<file>:<frames>:<ms>");
            uint8_t *buffer = malloc((size_t)frames * sizeof(bz_fb));
            if (!buffer) return fail(arg, "out of memory");
            for (int j = 0; j < frames; ++j) {
                if (j) run_ms((uint32_t)interval);
                memcpy(buffer + (size_t)j * sizeof(bz_fb), bz_fb, sizeof(bz_fb));
            }
            bool ok = write_png(path, buffer, frames);
            free(buffer);
            if (!ok) return fail(arg, "cannot write image");
        } else if (!strcmp(arg, "tint:green")) {
            image_tint = tint_green;
        } else if (!strcmp(arg, "tint:gray")) {
            image_tint = tint_gray;
        } else if (!strcmp(arg, "snap")) {
            if (snaps == 32) return fail(arg, "too many frames");
            memcpy(sheet[snaps++], bz_fb, sizeof(bz_fb));
        } else if (!strncmp(arg, "sheet:", 6)) {
            if (!snaps || !write_png(arg + 6, sheet[0], snaps)) return fail(arg, "cannot write image");
            snaps = 0;
        } else if (!strncmp(arg, "expect:", 7)) {
            if (strcmp(arg + 7, mode_name())) return fail(arg, mode_name());
        } else if (!strcmp(arg, "status")) {
            const bz_settings_t *s = bz_app_settings();
            printf("t=%lu mode=%s page=%s digits=%d brightness=%d applied=%d saves=%u slots=",
                   (unsigned long)now_ms, mode_name(), bz_page_name((bz_page_t)s->page), s->digits,
                   s->brightness, brightness, save_count);
            for (int j = 0; j < bz_page_slot_count((bz_page_t)s->page); ++j)
                printf("%s%s", j ? "," : "", bz_metric_name((bz_metric_t)s->slots[s->page][j]));
            printf("\n");
        } else {
            return fail(arg, "unknown step");
        }
    }
    return 0;
}

#ifdef SIM_SDL

#define SDL_MAIN_HANDLED
#include <SDL.h>

static int interactive(void) {
    if (SDL_Init(SDL_INIT_VIDEO)) return 1;
    SDL_Window *window = SDL_CreateWindow("bonanzaDisplay simulator", SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED, BZ_W * SCALE, BZ_H * SCALE, 0);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                             SDL_TEXTUREACCESS_STREAMING, BZ_W, BZ_H);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    puts("Left/Right: turn dial   Space: push (hold to go back)\n"
         "B: host boots   U: host up   A: host WiFi setup   R: host restarts   O: host off\n"
         "P: power-cycle display   Esc: quit");
    start();
    host_set(HOST_BOOT);
    uint32_t last = SDL_GetTicks();
    for (bool running = true; running;) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_SPACE) dial_pressed = false;
            if (event.type != SDL_KEYDOWN) continue;
            switch (event.key.keysym.sym) {
            case SDLK_ESCAPE: running = false; break;
            case SDLK_RIGHT: case SDLK_DOWN: dial_steps++; break;
            case SDLK_LEFT: case SDLK_UP: dial_steps--; break;
            case SDLK_SPACE: dial_pressed = true; break;
            case SDLK_b: host_set(HOST_BOOT); break;
            case SDLK_u: host_set(HOST_UP); break;
            case SDLK_a: host_set(HOST_AP); break;
            case SDLK_r: host_set(HOST_RESTART); break;
            case SDLK_o: host_set(HOST_OFF); break;
            case SDLK_p: start(); break;
            default: break;
            }
        }
        for (uint32_t ticks = SDL_GetTicks(); ticks - last >= TICK_MS; last += TICK_MS) step();

        uint32_t pixels[BZ_W * BZ_H];
        // Tint like the panel; scale brightness as the contrast setting would.
        int scale = 100 + 52 * (brightness < 0 ? 3 : brightness);
        for (int i = 0; i < BZ_W * BZ_H; ++i) {
            uint32_t level = (uint32_t)((bz_fb[i] >> 4) * 17 * scale / 256);
            pixels[i] = 0xff000000u | (level * tint_green[0] / 255) << 16 |
                        (level * tint_green[1] / 255) << 8 | level * tint_green[2] / 255;
        }
        SDL_UpdateTexture(texture, NULL, pixels, BZ_W * 4);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
    }
    SDL_Quit();
    return 0;
}

#endif
