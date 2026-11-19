#ifndef BZ_GFX_H
#define BZ_GFX_H

// Pixel-level drawing for the 256x64, 16-gray SSD1322 panel. Everything the
// retro UI shows is drawn here into one L8 framebuffer; LVGL only blits it.
// No LVGL or Pico dependencies, so the same code runs in the desktop simulator.

#include <stdbool.h>
#include <stdint.h>

#define BZ_W 256
#define BZ_H 64

// Brightness levels are the panel's native 0..15.
#define BZ_BLACK 0
#define BZ_DIM 5
#define BZ_MID 9
#define BZ_WHITE 15

typedef enum {
    BZ_FONT_TINY,  // 3x5 in a 4x6 cell, upper case only
    BZ_FONT_SMALL, // Portfolio 6x8
    BZ_FONT_LARGE, // FM Towns 8x16
} bz_font_t;

// One byte per pixel, level * 17, row-major. Suitable as an LVGL L8 image.
extern uint8_t bz_fb[BZ_W * BZ_H];

extern const uint8_t bz_font_small_data[95][8];
extern const uint8_t bz_font_large_data[95][16];
extern const uint8_t bz_logo_w, bz_logo_h;
extern const uint8_t bz_logo_bits[];

void bz_clear(void);
void bz_clip(int x, int y, int w, int h);
void bz_clip_reset(void);

void bz_px(int x, int y, int level);
void bz_hline(int x, int y, int w, int level);
void bz_vline(int x, int y, int h, int level);
void bz_fill(int x, int y, int w, int h, int level);
void bz_rect(int x, int y, int w, int h, int level);
// Every other pixel lit, as in a dot-matrix guide line.
void bz_hdots(int x, int y, int w, int level);
void bz_vdots(int x, int y, int h, int level);
void bz_dotted_rect(int x, int y, int w, int h, int level);
// Checkerboard fill; phase 0/1 selects which diagonal is lit.
void bz_dither(int x, int y, int w, int h, int level, int phase);
// Swap light and dark inside a rectangle (inverse video).
void bz_invert(int x, int y, int w, int h);

int bz_font_w(bz_font_t font);
int bz_font_h(bz_font_t font);
int bz_text_w(const char *text, bz_font_t font, int scale);
// Draws text with its top-left corner at (x, y); returns the x after it.
int bz_text(int x, int y, const char *text, bz_font_t font, int level);
// scale >= 1 enlarges each font pixel. With dots set, each font pixel becomes
// a (scale-1) square with a one pixel gap: a dot-matrix display look.
int bz_text_ex(int x, int y, const char *text, bz_font_t font, int scale,
               bool dots, int level);
void bz_text_right(int right, int y, const char *text, bz_font_t font, int level);
void bz_text_center(int cx, int y, const char *text, bz_font_t font, int level);

// 1bpp sprite, rows padded to whole bytes, MSB is the left pixel.
void bz_bitmap(int x, int y, const uint8_t *bits, int w, int h, int level);

#endif
