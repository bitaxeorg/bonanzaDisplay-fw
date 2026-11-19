#include "bz_gfx.h"

#include <string.h>

_Alignas(8) uint8_t bz_fb[BZ_W * BZ_H];

static int clip_x0, clip_y0, clip_x1 = BZ_W, clip_y1 = BZ_H;

// 3x5 glyphs for 0x20..0x5F, one octal digit per row, top row first.
static const uint16_t tiny_glyphs[64] = {
    000000, 022202, 055000, 057575, 036236, 051245, 025253, 022000, // space..'
    012221, 042224, 052500, 002720, 000024, 000700, 000002, 011244, // (../
    075557, 026227, 071747, 071717, 055711, 074717, 074757, 071222, // 0..7
    075757, 075717, 002020, 002024, 012421, 007070, 042124, 071202, // 8..?
    075547, 025755, 065656, 034443, 065556, 074647, 074644, 034553, // @..G
    055755, 072227, 011152, 055655, 044447, 057755, 065555, 025552, // H..O
    065644, 025563, 065655, 034216, 072222, 055557, 055522, 055775, // P..W
    055255, 055222, 071247, 032223, 044211, 062226, 025000, 000007, // X.._
};

void bz_clear(void) {
    memset(bz_fb, 0, sizeof(bz_fb));
}

void bz_clip(int x, int y, int w, int h) {
    clip_x0 = x < 0 ? 0 : x;
    clip_y0 = y < 0 ? 0 : y;
    clip_x1 = x + w > BZ_W ? BZ_W : x + w;
    clip_y1 = y + h > BZ_H ? BZ_H : y + h;
}

void bz_clip_reset(void) {
    clip_x0 = clip_y0 = 0;
    clip_x1 = BZ_W;
    clip_y1 = BZ_H;
}

static uint8_t shade(int level) {
    if (level < 0) level = 0;
    if (level > 15) level = 15;
    return (uint8_t)(level * 17);
}

void bz_px(int x, int y, int level) {
    if (x < clip_x0 || x >= clip_x1 || y < clip_y0 || y >= clip_y1) return;
    bz_fb[y * BZ_W + x] = shade(level);
}

void bz_fill(int x, int y, int w, int h, int level) {
    int x1 = x + w, y1 = y + h;
    if (x < clip_x0) x = clip_x0;
    if (y < clip_y0) y = clip_y0;
    if (x1 > clip_x1) x1 = clip_x1;
    if (y1 > clip_y1) y1 = clip_y1;
    if (x >= x1) return;
    for (; y < y1; ++y) memset(&bz_fb[y * BZ_W + x], shade(level), (size_t)(x1 - x));
}

void bz_hline(int x, int y, int w, int level) {
    bz_fill(x, y, w, 1, level);
}

void bz_vline(int x, int y, int h, int level) {
    bz_fill(x, y, 1, h, level);
}

void bz_rect(int x, int y, int w, int h, int level) {
    if (w <= 0 || h <= 0) return;
    bz_hline(x, y, w, level);
    bz_hline(x, y + h - 1, w, level);
    bz_vline(x, y, h, level);
    bz_vline(x + w - 1, y, h, level);
}

// Dots sit on even screen coordinates so separate guide lines stay in step.
void bz_hdots(int x, int y, int w, int level) {
    for (int i = x; i < x + w; ++i)
        if (!(i & 1)) bz_px(i, y, level);
}

void bz_vdots(int x, int y, int h, int level) {
    for (int i = y; i < y + h; ++i)
        if (!(i & 1)) bz_px(x, i, level);
}

void bz_dotted_rect(int x, int y, int w, int h, int level) {
    if (w <= 0 || h <= 0) return;
    bz_hdots(x, y, w, level);
    bz_hdots(x, y + h - 1, w, level);
    bz_vdots(x, y, h, level);
    bz_vdots(x + w - 1, y, h, level);
}

void bz_dither(int x, int y, int w, int h, int level, int phase) {
    for (int j = y; j < y + h; ++j)
        for (int i = x; i < x + w; ++i)
            if (((i + j + phase) & 1) == 0) bz_px(i, j, level);
}

void bz_invert(int x, int y, int w, int h) {
    int x1 = x + w, y1 = y + h;
    if (x < clip_x0) x = clip_x0;
    if (y < clip_y0) y = clip_y0;
    if (x1 > clip_x1) x1 = clip_x1;
    if (y1 > clip_y1) y1 = clip_y1;
    for (; y < y1; ++y)
        for (int i = x; i < x1; ++i) bz_fb[y * BZ_W + i] = (uint8_t)(255 - bz_fb[y * BZ_W + i]);
}

int bz_font_w(bz_font_t font) {
    return font == BZ_FONT_TINY ? 4 : font == BZ_FONT_SMALL ? 6 : 8;
}

int bz_font_h(bz_font_t font) {
    return font == BZ_FONT_TINY ? 6 : font == BZ_FONT_SMALL ? 8 : 16;
}

int bz_text_w(const char *text, bz_font_t font, int scale) {
    return (int)strlen(text) * bz_font_w(font) * (scale < 1 ? 1 : scale);
}

// Returns one glyph row with the left pixel in bit 7.
static uint8_t glyph_row(bz_font_t font, char c, int row) {
    unsigned code = (unsigned char)c;
    if (font == BZ_FONT_TINY) {
        if (code >= 'a' && code <= 'z') code -= 32;
        if (code < 0x20 || code > 0x5f || row >= 5) return 0;
        return (uint8_t)(((tiny_glyphs[code - 0x20] >> (3 * (4 - row))) & 7u) << 5);
    }
    if (code < 0x20 || code > 0x7e) code = '?';
    return font == BZ_FONT_SMALL ? bz_font_small_data[code - 0x20][row]
                                 : bz_font_large_data[code - 0x20][row];
}

int bz_text_ex(int x, int y, const char *text, bz_font_t font, int scale,
               bool dots, int level) {
    if (scale < 1) scale = 1;
    int cell_w = bz_font_w(font), cell_h = bz_font_h(font);
    int dot = dots && scale > 1 ? scale - 1 : scale;
    for (; *text; ++text, x += cell_w * scale) {
        if (x >= clip_x1 || x + cell_w * scale <= clip_x0) continue;
        for (int row = 0; row < cell_h; ++row) {
            uint8_t bits = glyph_row(font, *text, row);
            for (int col = 0; bits; ++col, bits = (uint8_t)(bits << 1)) {
                if (!(bits & 0x80)) continue;
                if (scale == 1) bz_px(x + col, y + row, level);
                else bz_fill(x + col * scale, y + row * scale, dot, dot, level);
            }
        }
    }
    return x;
}

int bz_text(int x, int y, const char *text, bz_font_t font, int level) {
    return bz_text_ex(x, y, text, font, 1, false, level);
}

void bz_text_right(int right, int y, const char *text, bz_font_t font, int level) {
    bz_text(right - bz_text_w(text, font, 1) + 1, y, text, font, level);
}

void bz_text_center(int cx, int y, const char *text, bz_font_t font, int level) {
    bz_text(cx - bz_text_w(text, font, 1) / 2, y, text, font, level);
}

void bz_bitmap(int x, int y, const uint8_t *bits, int w, int h, int level) {
    int stride = (w + 7) / 8;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
            if (bits[j * stride + i / 8] & (0x80 >> (i % 8))) bz_px(x + i, y + j, level);
}
