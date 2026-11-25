#ifndef BZ_QR_H
#define BZ_QR_H

// A small QR code encoder: just enough for a WiFi name or a short link on a
// 64 pixel high panel. Byte mode, versions 1 to 3 (21, 25 or 29 modules a
// side), which at two pixels a module is the most the panel has room for.

#include <stdbool.h>
#include <stdint.h>

#define BZ_QR_MAX_SIZE 29 // Version 3
#define BZ_QR_MAX_TEXT 53 // Longest text that fits, in bytes

typedef struct {
    uint8_t size;                  // Modules per side
    uint32_t rows[BZ_QR_MAX_SIZE]; // Bit x of rows[y] is set for a dark module
} bz_qr_t;

// Picks the smallest version the text fits, then the strongest error
// correction that still fits it. False if the text is empty or too long.
bool bz_qr_encode(const char *text, bz_qr_t *out);

static inline bool bz_qr_dark(const bz_qr_t *qr, int x, int y) {
    return x >= 0 && y >= 0 && x < qr->size && y < qr->size && (qr->rows[y] >> x & 1u);
}

#endif
