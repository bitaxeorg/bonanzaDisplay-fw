// WiFi setup screen: what to do when the Bitaxe cannot get onto WiFi. It
// says so in large type, lists the three steps to fix it (join the Bitaxe's
// own setup network, open its page, enter the WiFi details), and draws a QR
// code a phone can scan to do the first step.

#include "bz_internal.h"

#include <stdio.h>
#include <string.h>

#include "bz_qr.h"

// ESP-Miner's setup access point is open, and always at this address.
#define AP_ADDRESS "192.168.4.1"
#define AP_NAME_PATTERN "Bitaxe_XXXX" // Shown until the host says its real name

#define QR_FIELD 62           // The lit square the code sits in
#define QR_X (BZ_W - QR_FIELD)
#define QR_Y 1
#define QR_MODULE 2           // Pixels per module
#define TEXT_W (QR_X - 5)     // Everything else stays left of this
#define LABEL_X 13
#define VALUE_X 54
#define BLINK_MS 500u

static struct {
    char text[BZ_QR_MAX_TEXT + 1]; // What the code below says
    bz_qr_t qr;
} w;

// "WIFI:S:<name>;;" joins an open network, the same wording ESP-Miner's own
// screen uses. Characters that mean something in it are escaped.
static bool join_text(const char *ssid, char *out, size_t size) {
    size_t n = (size_t)snprintf(out, size, "WIFI:S:");
    for (; *ssid; ++ssid) {
        if (strchr("\\;,:\"", *ssid)) {
            if (n + 1 >= size) return false;
            out[n++] = '\\';
        }
        if (n + 1 >= size) return false;
        out[n++] = *ssid;
    }
    if (n + 2 >= size) return false;
    memcpy(out + n, ";;", 3);
    return true;
}

// Encodes only when the text changes: the code is the same frame after frame.
static bool prepare(const char *text) {
    if (w.qr.size && !strcmp(text, w.text)) return true;
    bz_qr_t qr;
    if (strlen(text) >= sizeof(w.text) || !bz_qr_encode(text, &qr)) return false;
    snprintf(w.text, sizeof(w.text), "%s", text);
    w.qr = qr;
    return true;
}

// Dark modules on a lit field, the way phones expect a code to look.
static void draw_code(void) {
    bz_fill(QR_X, QR_Y, QR_FIELD, QR_FIELD, BZ_WHITE);
    int origin = (QR_FIELD - w.qr.size * QR_MODULE) / 2;
    for (int y = 0; y < w.qr.size; ++y)
        for (int x = 0; x < w.qr.size; ++x)
            if (bz_qr_dark(&w.qr, x, y))
                bz_fill(QR_X + origin + x * QR_MODULE, QR_Y + origin + y * QR_MODULE, QR_MODULE,
                        QR_MODULE, BZ_BLACK);
}

static void draw_number(int number, int y) {
    char digit[2] = {(char)('0' + number), '\0'};
    bz_fill(0, y - 1, 9, 10, BZ_WHITE);
    bz_text(2, y, digit, BZ_FONT_SMALL, BZ_BLACK);
}

// One step: its number, what to do, and the name or address to do it with.
// The step the code takes care of gets a tag pointing at the code.
static void draw_step(int number, int y, const char *label, const char *value, bool scan) {
    draw_number(number, y);
    bz_text(LABEL_X, y + 2, label, BZ_FONT_TINY, BZ_MID);
    int right = TEXT_W;
    if (scan) {
        const int tag_w = 27;
        right -= tag_w;
        bz_fill(right, y - 1, tag_w, 10, BZ_WHITE);
        bz_text(right + 2, y + 2, "SCAN", BZ_FONT_TINY, BZ_BLACK);
        for (int i = 0; i < 3; ++i) bz_vline(right + 20 + i, y + 2 + i, 5 - 2 * i, BZ_BLACK);
        right -= 4;
    }
    bz_clip(VALUE_X, y, right - VALUE_X, 8); // A long network name is cut short
    bz_text(VALUE_X, y, value, BZ_FONT_SMALL, BZ_WHITE);
    bz_clip_reset();
}

void bz_wifi_draw(void) {
    // With the access point's name the code joins it. Without, the best a
    // code can do is open the setup page once the phone has joined by hand.
    const char *ssid = bz.data.ap_ssid;
    char text[BZ_QR_MAX_TEXT + 1];
    bool joins = *ssid && join_text(ssid, text, sizeof(text)) && prepare(text);
    if (!joins) prepare("http://" AP_ADDRESS);

    bool lit = (bz.now / BLINK_MS) % 2u == 0;
    if (lit) bz_fill(0, 0, 12, 16, BZ_WHITE);
    bz_text(2, 0, "!", BZ_FONT_LARGE, lit ? BZ_BLACK : BZ_WHITE);
    bz_text(16, 0, "CAN'T CONNECT TO WIFI", BZ_FONT_LARGE, BZ_WHITE);
    bz_hdots(0, 18, TEXT_W, BZ_MID);

    draw_step(1, 23, "JOIN WIFI", joins ? ssid : AP_NAME_PATTERN, joins);
    draw_step(2, 36, "THEN OPEN", AP_ADDRESS, !joins);
    draw_number(3, 49);
    bz_text(LABEL_X, 51, "ENTER YOUR WIFI NAME AND PASSWORD", BZ_FONT_TINY, BZ_MID);

    draw_code();
}
