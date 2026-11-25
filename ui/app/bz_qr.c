// QR code encoder for versions 1 to 3, byte mode, following ISO/IEC 18004.
// Those versions keep all their data in one error correction block at the
// levels used here, so there is no interleaving and no version information
// to draw.

#include "bz_qr.h"

#include <stdlib.h>
#include <string.h>

#define MAX_CODEWORDS 70
#define MAX_EC 28

enum { EC_L, EC_M, EC_Q, EC_H, EC_LEVELS };

static const uint8_t total_codewords[3] = {26, 44, 70};
// Error correction codewords per version and level. 0 where the standard
// splits the data into two blocks, which this encoder leaves out.
static const uint8_t ec_codewords[3][EC_LEVELS] = {{7, 10, 13, 17}, {10, 16, 22, 28}, {15, 26, 0, 0}};
static const uint8_t level_bits[EC_LEVELS] = {1, 0, 3, 2}; // As written in the format information

typedef struct {
    int size;
    uint32_t dark[BZ_QR_MAX_SIZE];
    uint32_t fixed[BZ_QR_MAX_SIZE]; // Finder, timing, alignment and format modules
} grid_t;

// ---- Reed-Solomon over GF(256), polynomial x^8 + x^4 + x^3 + x^2 + 1 ----------

static uint8_t gf_mul(uint8_t a, uint8_t b) {
    unsigned product = 0;
    for (int bit = 7; bit >= 0; --bit) {
        product = (product << 1) ^ ((product >> 7) * 0x11du);
        if (b >> bit & 1) product ^= a;
    }
    return (uint8_t)product;
}

// Remainder of the data divided by the generator with `count` roots.
static void rs_remainder(const uint8_t *data, int length, int count, uint8_t *out) {
    uint8_t generator[MAX_EC] = {0};
    uint8_t root = 1;
    generator[count - 1] = 1;
    for (int i = 0; i < count; ++i) {
        for (int j = 0; j < count; ++j) {
            generator[j] = gf_mul(generator[j], root);
            if (j + 1 < count) generator[j] ^= generator[j + 1];
        }
        root = gf_mul(root, 2);
    }
    memset(out, 0, (size_t)count);
    for (int i = 0; i < length; ++i) {
        uint8_t factor = data[i] ^ out[0];
        memmove(out, out + 1, (size_t)count - 1);
        out[count - 1] = 0;
        for (int j = 0; j < count; ++j) out[j] ^= gf_mul(generator[j], factor);
    }
}

// ---- The grid -----------------------------------------------------------------

static bool get(const grid_t *g, int x, int y) {
    return g->dark[y] >> x & 1u;
}

// Draws a module of a fixed pattern; off-grid positions are ignored.
static void set_fixed(grid_t *g, int x, int y, bool dark) {
    if (x < 0 || y < 0 || x >= g->size || y >= g->size) return;
    if (dark) g->dark[y] |= 1u << x;
    else g->dark[y] &= ~(1u << x);
    g->fixed[y] |= 1u << x;
}

static void draw_format(grid_t *g, int level, int mask) {
    // Five data bits, ten check bits (BCH), then a fixed pattern on top so
    // the result is never all zeros.
    unsigned data = (unsigned)level_bits[level] << 3 | (unsigned)mask;
    unsigned check = data;
    for (int i = 0; i < 10; ++i) check = (check << 1) ^ ((check >> 9) * 0x537u);
    unsigned bits = (data << 10 | check) ^ 0x5412u;
#define BIT(i) ((bits >> (i) & 1u) != 0)
    // One copy around the top-left finder...
    for (int i = 0; i <= 5; ++i) set_fixed(g, 8, i, BIT(i));
    set_fixed(g, 8, 7, BIT(6));
    set_fixed(g, 8, 8, BIT(7));
    set_fixed(g, 7, 8, BIT(8));
    for (int i = 9; i < 15; ++i) set_fixed(g, 14 - i, 8, BIT(i));
    // ...and one split between the other two.
    for (int i = 0; i < 8; ++i) set_fixed(g, g->size - 1 - i, 8, BIT(i));
    for (int i = 8; i < 15; ++i) set_fixed(g, 8, g->size - 15 + i, BIT(i));
#undef BIT
    set_fixed(g, 8, g->size - 8, true); // Always dark
}

static void draw_fixed(grid_t *g) {
    int size = g->size;
    for (int i = 0; i < size; ++i) { // Timing lines
        set_fixed(g, 6, i, i % 2 == 0);
        set_fixed(g, i, 6, i % 2 == 0);
    }
    // Finders in three corners, each with a light border.
    static const int8_t corners[3][2] = {{0, 0}, {1, 0}, {0, 1}};
    for (int c = 0; c < 3; ++c) {
        int cx = corners[c][0] ? size - 4 : 3, cy = corners[c][1] ? size - 4 : 3;
        for (int dy = -4; dy <= 4; ++dy)
            for (int dx = -4; dx <= 4; ++dx) {
                int ring = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
                set_fixed(g, cx + dx, cy + dy, ring != 2 && ring != 4);
            }
    }
    if (size > 21) { // One alignment pattern from version 2 up
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx)
                set_fixed(g, size - 7 + dx, size - 7 + dy, (abs(dx) > abs(dy) ? abs(dx) : abs(dy)) != 1);
    }
    draw_format(g, EC_L, 0); // Reserves the format modules; redrawn later
}

// Codewords run in a two-module-wide column that snakes up and down from the
// bottom right corner, stepping round the fixed patterns.
static void draw_codewords(grid_t *g, const uint8_t *codewords, int count) {
    int bit = 0;
    for (int right = g->size - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5; // The vertical timing line
        for (int step = 0; step < g->size; ++step) {
            for (int j = 0; j < 2; ++j) {
                int x = right - j;
                bool upward = ((right + 1) & 2) == 0;
                int y = upward ? g->size - 1 - step : step;
                if ((g->fixed[y] >> x & 1u) || bit >= count * 8) continue;
                if (codewords[bit >> 3] >> (7 - (bit & 7)) & 1) g->dark[y] |= 1u << x;
                ++bit;
            }
        }
    }
}

// Flips the data modules a mask pattern selects. Applying it twice undoes it.
static void apply_mask(grid_t *g, int mask) {
    for (int y = 0; y < g->size; ++y)
        for (int x = 0; x < g->size; ++x) {
            bool flip;
            switch (mask) {
            case 0: flip = (x + y) % 2 == 0; break;
            case 1: flip = y % 2 == 0; break;
            case 2: flip = x % 3 == 0; break;
            case 3: flip = (x + y) % 3 == 0; break;
            case 4: flip = (x / 3 + y / 2) % 2 == 0; break;
            case 5: flip = x * y % 2 + x * y % 3 == 0; break;
            case 6: flip = (x * y % 2 + x * y % 3) % 2 == 0; break;
            default: flip = ((x + y) % 2 + x * y % 3) % 2 == 0; break;
            }
            if (flip && !(g->fixed[y] >> x & 1u)) g->dark[y] ^= 1u << x;
        }
}

// How hard the pattern is to read, by the standard's four rules. Lower is
// better.
static int penalty(const grid_t *g) {
    int size = g->size, score = 0, dark = 0;
    for (int axis = 0; axis < 2; ++axis)
        for (int a = 0; a < size; ++a) {
            int run = 0;
            bool colour = false;
            unsigned window = 0;
            for (int b = 0; b < size; ++b) {
                bool module = axis ? get(g, a, b) : get(g, b, a);
                // Runs of five or more alike.
                if (b && module == colour) {
                    if (++run == 5) score += 3;
                    else if (run > 5) ++score;
                } else {
                    colour = module;
                    run = 1;
                }
                // Anything that looks like a finder: 1011101 beside four light.
                window = (window << 1 | module) & 0x7ffu;
                if (b >= 10 && (window == 0x5d0u || window == 0x05du)) score += 40;
            }
        }
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            bool module = get(g, x, y);
            dark += module;
            // Two by two blocks of one colour.
            if (x + 1 < size && y + 1 < size && module == get(g, x + 1, y) &&
                module == get(g, x, y + 1) && module == get(g, x + 1, y + 1)) score += 3;
        }
    // Dark and light out of balance, in steps of 5%.
    int total = size * size;
    int steps = (abs(dark * 20 - total * 10) + total - 1) / total - 1;
    return score + steps * 10;
}

// ---- Encoding -----------------------------------------------------------------

bool bz_qr_encode(const char *text, bz_qr_t *out) {
    size_t length = strlen(text);
    if (!length || length > BZ_QR_MAX_TEXT) return false;

    // The data is a 4 bit mode, an 8 bit length, the text and a 4 bit
    // terminator: two codewords more than the text.
    int version = 1;
    while (length + 2 > (size_t)(total_codewords[version - 1] - ec_codewords[version - 1][EC_L]))
        ++version;
    int level = EC_L;
    for (int l = EC_H; l > EC_L; --l) {
        int ec = ec_codewords[version - 1][l];
        if (ec && length + 2 <= (size_t)(total_codewords[version - 1] - ec)) {
            level = l;
            break;
        }
    }
    int total = total_codewords[version - 1], ec = ec_codewords[version - 1][level];
    int data = total - ec;

    uint8_t codewords[MAX_CODEWORDS] = {0};
    codewords[0] = (uint8_t)(0x40u | length >> 4); // Mode 0100: bytes
    codewords[1] = (uint8_t)(length << 4);
    for (size_t i = 0; i < length; ++i) {
        uint8_t byte = (uint8_t)text[i];
        codewords[i + 1] |= byte >> 4;
        codewords[i + 2] = (uint8_t)(byte << 4);
    }
    for (int i = (int)length + 2, pad = 0; i < data; ++i, ++pad) codewords[i] = pad % 2 ? 0x11 : 0xec;
    rs_remainder(codewords, data, ec, codewords + data);

    grid_t grid = {.size = 17 + 4 * version};
    draw_fixed(&grid);
    draw_codewords(&grid, codewords, total);

    int best_mask = 0, best_score = 0;
    for (int mask = 0; mask < 8; ++mask) {
        apply_mask(&grid, mask);
        draw_format(&grid, level, mask);
        int score = penalty(&grid);
        if (!mask || score < best_score) {
            best_mask = mask;
            best_score = score;
        }
        apply_mask(&grid, mask);
    }
    apply_mask(&grid, best_mask);
    draw_format(&grid, level, best_mask);

    memset(out, 0, sizeof(*out));
    out->size = (uint8_t)grid.size;
    memcpy(out->rows, grid.dark, sizeof(grid.dark));
    return true;
}
