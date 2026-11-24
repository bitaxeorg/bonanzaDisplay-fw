#include "bz_model.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void bz_data_reset(bz_data_t *data) {
    memset(data, 0, sizeof(*data));
    data->link = BZ_LINK_WAITING;
    data->hashrate_ghs = data->chip_temp_c = data->vr_temp_c = data->power_mw =
        data->voltage_mv = data->current_ma = data->fan_rpm = data->frequency_mhz =
        data->shares_accepted = data->shares_rejected = data->block_height = BZ_UNKNOWN;
    data->rssi_dbm = BZ_RSSI_UNKNOWN;
    memset(data->asics, 0xff, sizeof(data->asics)); // Every field BZ_UNKNOWN
}

// ---- 256-bit targets and hashes ---------------------------------------------

int bz_u256_digit(const bz_u256_t *value, int index) {
    if (index < 0 || index > 63) return 0;
    uint8_t byte = value->be[index / 2];
    return index % 2 ? byte & 15 : byte >> 4;
}

int bz_u256_zeros(const bz_u256_t *value) {
    int zeros = 0;
    while (zeros < 64 && !bz_u256_digit(value, zeros)) ++zeros;
    return zeros;
}

// As a double: only the top 53 bits survive, which is plenty to compare by.
static double u256_approx(const bz_u256_t *value) {
    double sum = 0;
    for (int i = 0; i < 32; ++i) sum = sum * 256.0 + value->be[i];
    return sum;
}

double bz_u256_difficulty(const bz_u256_t *value) {
    double v = value->digits ? u256_approx(value) : 0;
    return v > 0 ? ldexp(65535.0, 208) / v : 0; // Difficulty 1 is 0xFFFF << 208
}

double bz_u256_ratio(const bz_u256_t *value, const bz_u256_t *target) {
    double t = target->digits ? u256_approx(target) : 0;
    return value->digits && t > 0 ? u256_approx(value) / t : 0;
}

void bz_u256_from_difficulty(double difficulty, int significant, bz_u256_t *out) {
    memset(out, 0, sizeof(*out));
    if (!(difficulty > 0) || !isfinite(difficulty)) return;
    // value = mantissa * 2^low, with a 53-bit mantissa.
    int exponent;
    double fraction = frexp(65535.0 / difficulty, &exponent);
    uint64_t mantissa = (uint64_t)ldexp(fraction, 53);
    int low = exponent + 208 - 53;
    if (low + 53 > 256) {
        memset(out->be, 0xff, sizeof(out->be)); // Easier than any 256-bit hash
    } else {
        for (int bit = 0; bit < 53; ++bit) {
            int position = low + bit;
            if ((mantissa >> bit & 1u) && position >= 0)
                out->be[31 - position / 8] |= (uint8_t)(1u << (position % 8));
        }
    }
    int digits = bz_u256_zeros(out) + (significant < 1 ? 1 : significant);
    out->digits = (uint8_t)(digits > 64 ? 64 : digits);
}

bool bz_u256_from_text(const char *text, bz_u256_t *out) {
    memset(out, 0, sizeof(*out));
    if (*text < '0' || *text > '9') return false;
    char *end;
    double value = strtod(text, &end);
    static const char suffixes[] = "kMGTPE";
    const char *suffix = *end ? strchr(suffixes, *end == 'K' ? 'k' : *end) : NULL;
    if (*end && (!suffix || end[1])) return false;
    for (int i = 0; suffix && i <= suffix - suffixes; ++i) value *= 1000.0;
    // Each decimal digit the host printed is worth a little under one hex digit.
    int decimal = 0;
    for (const char *c = text; c != end; ++c) decimal += *c >= '0' && *c <= '9';
    bz_u256_from_difficulty(value, decimal * 5 / 6, out);
    return out->digits != 0;
}

void bz_format_scaled(double value, char *out, size_t size) {
    static const char suffixes[] = " kMGTPE";
    int scale = 0;
    if (!(value >= 0) || !isfinite(value)) value = 0;
    while (value >= 1000.0 && scale < 6) {
        value /= 1000.0;
        ++scale;
    }
    if (value > 99999.0) value = 99999.0;
    unsigned long hundredths = (unsigned long)(value * 100.0 + 0.5);
    if (scale == 0) snprintf(out, size, "%lu", (hundredths + 50) / 100);
    else snprintf(out, size, "%lu.%02lu%c", hundredths / 100, hundredths % 100, suffixes[scale]);
}

// ---- Metric catalogue -------------------------------------------------------

static const struct {
    const char *name;
    const char *label;
} metric_names[BZ_M_COUNT] = {
    [BZ_M_NONE] = {"(EMPTY)", ""},
    [BZ_M_HASHRATE] = {"HASHRATE", "HASH"},
    [BZ_M_CHIP_TEMP] = {"CHIP TEMP", "TEMP"},
    [BZ_M_VR_TEMP] = {"VR TEMP", "VREG"},
    [BZ_M_POWER] = {"POWER", "PWR"},
    [BZ_M_EFFICIENCY] = {"EFFICIENCY", "EFF"},
    [BZ_M_VOLTAGE] = {"VOLTAGE", "VIN"},
    [BZ_M_CURRENT] = {"CURRENT", "AMPS"},
    [BZ_M_FAN] = {"FAN SPEED", "FAN"},
    [BZ_M_BEST_DIFF] = {"BEST DIFF", "BEST"},
    [BZ_M_SHARES] = {"SHARES", "SHRS"},
    [BZ_M_REJECTED] = {"REJECTED", "REJ"},
    [BZ_M_BLOCK_HEIGHT] = {"BLOCK HEIGHT", "BLK"},
    [BZ_M_WIFI_RSSI] = {"WIFI SIGNAL", "WIFI"},
    [BZ_M_IP] = {"IP ADDRESS", "IP"},
    [BZ_M_POOL] = {"POOL", "POOL"},
    [BZ_M_DEVICE] = {"ASIC MODEL", "ASIC"},
    [BZ_M_FREQUENCY] = {"FREQUENCY", "FREQ"},
    [BZ_M_NET_DIFF] = {"NETWORK DIFF", "NET"},
};

const char *bz_metric_name(bz_metric_t metric) {
    return metric < BZ_M_COUNT ? metric_names[metric].name : "?";
}

const char *bz_metric_short(bz_metric_t metric) {
    return metric < BZ_M_COUNT ? metric_names[metric].label : "?";
}

static void set_text(bz_reading_t *out, const char *text, const char *unit) {
    out->valid = text && *text;
    snprintf(out->value, sizeof(out->value), "%s", out->valid ? text : "--");
    snprintf(out->unit, sizeof(out->unit), "%s", unit);
}

static void set_count(bz_reading_t *out, uint32_t value, const char *unit) {
    out->valid = value != BZ_UNKNOWN;
    if (out->valid) snprintf(out->value, sizeof(out->value), "%lu", (unsigned long)value);
    else snprintf(out->value, sizeof(out->value), "--");
    snprintf(out->unit, sizeof(out->unit), "%s", unit);
}

// Integer value with a gauge, e.g. degrees or RPM.
static void set_gauge(bz_reading_t *out, uint32_t value, const char *unit, float fixed_max) {
    set_count(out, value, unit);
    out->gauge = true;
    out->fixed_max = fixed_max;
    out->number = out->valid ? (float)value : 0.0f;
}

// Thousandths shown as a decimal: 5020 with 2 places -> "5.02".
static void set_milli(bz_reading_t *out, uint32_t milli, int places, const char *unit) {
    out->gauge = true;
    out->valid = milli != BZ_UNKNOWN;
    snprintf(out->unit, sizeof(out->unit), "%s", unit);
    if (!out->valid) {
        snprintf(out->value, sizeof(out->value), "--");
        return;
    }
    out->number = (float)milli / 1000.0f;
    uint32_t step = places == 2 ? 10u : places == 1 ? 100u : 1000u;
    uint32_t rounded = (milli + step / 2u) / step;
    uint32_t per_unit = 1000u / step;
    if (places == 0) snprintf(out->value, sizeof(out->value), "%lu", (unsigned long)rounded);
    else snprintf(out->value, sizeof(out->value), "%lu.%0*lu", (unsigned long)(rounded / per_unit),
                  places, (unsigned long)(rounded % per_unit));
}

void bz_metric_read(bz_metric_t metric, const bz_data_t *d, bz_reading_t *out) {
    memset(out, 0, sizeof(*out));
    out->unit_div = 1.0f;
    switch (metric) {
    case BZ_M_HASHRATE:
        // The host reports GH/s. Show TH/s once that reads better.
        if (d->hashrate_ghs != BZ_UNKNOWN && d->hashrate_ghs >= 1000u) {
            set_milli(out, d->hashrate_ghs, d->hashrate_ghs >= 100000u ? 1 : 2, "TH/s");
            out->number = (float)d->hashrate_ghs;
            out->unit_div = 1000.0f;
        } else {
            set_gauge(out, d->hashrate_ghs, "GH/s", 0);
        }
        break;
    case BZ_M_CHIP_TEMP: set_gauge(out, d->chip_temp_c, "C", 100); break;
    case BZ_M_VR_TEMP: set_gauge(out, d->vr_temp_c, "C", 100); break;
    case BZ_M_POWER:
        set_milli(out, d->power_mw, d->power_mw != BZ_UNKNOWN && d->power_mw >= 100000u ? 0 : 1, "W");
        break;
    case BZ_M_EFFICIENCY:
        if (d->power_mw != BZ_UNKNOWN && d->hashrate_ghs != BZ_UNKNOWN && d->hashrate_ghs > 0 &&
            d->power_mw < 4000000u) {
            // W per TH/s is J/TH; power is in mW and hashrate in GH/s.
            uint64_t milli = ((uint64_t)d->power_mw * 1000u + d->hashrate_ghs / 2u) / d->hashrate_ghs;
            set_milli(out, milli > 9999000u ? 9999000u : (uint32_t)milli, 1, "J/TH");
        } else {
            set_milli(out, BZ_UNKNOWN, 1, "J/TH");
        }
        break;
    case BZ_M_VOLTAGE: set_milli(out, d->voltage_mv, 2, "V"); break;
    case BZ_M_CURRENT: set_milli(out, d->current_ma, 2, "A"); break;
    case BZ_M_FAN: set_gauge(out, d->fan_rpm, "RPM", 0); break;
    case BZ_M_BEST_DIFF: set_text(out, d->best_diff, ""); break;
    case BZ_M_SHARES: set_count(out, d->shares_accepted, ""); break;
    case BZ_M_REJECTED: set_count(out, d->shares_rejected, ""); break;
    case BZ_M_BLOCK_HEIGHT: set_count(out, d->block_height, ""); break;
    case BZ_M_WIFI_RSSI:
        out->valid = d->rssi_dbm != BZ_RSSI_UNKNOWN;
        if (out->valid) snprintf(out->value, sizeof(out->value), "%ld", (long)d->rssi_dbm);
        else snprintf(out->value, sizeof(out->value), "--");
        snprintf(out->unit, sizeof(out->unit), "dBm");
        break;
    case BZ_M_IP: set_text(out, d->ip, ""); break;
    case BZ_M_POOL: set_text(out, d->pool, ""); break;
    case BZ_M_DEVICE: set_text(out, *d->asic ? d->asic : d->family, ""); break;
    case BZ_M_FREQUENCY: set_gauge(out, d->frequency_mhz, "MHz", 0); break;
    case BZ_M_NET_DIFF: set_text(out, d->net_diff, ""); break;
    default: break; // BZ_M_NONE: an empty slot
    }
}

// ---- Settings -----------------------------------------------------------------

// The blob has room for four rows of slots, one per page. The set of pages
// has changed twice, so the version says which pages its numbers mean:
//   1: HERO, GRID, DUO, STUDIO    2: HERO, GRID, DUO, TARGET
//   3: HERO, TARGET, ASIC
// Older blobs still load: HERO keeps its slots, and a page that is gone
// lands on HERO.
#define BLOB_VERSION 3u
#define BLOB_PAGE_ROWS 4u
#define BLOB_PAYLOAD (3u + BLOB_PAGE_ROWS * BZ_MAX_SLOTS)

_Static_assert(BZ_PAGE_COUNT <= BLOB_PAGE_ROWS, "settings blob has no room for another page");
_Static_assert(BZ_PAGE_HERO == 0, "HERO must stay the first row of the settings blob");

int bz_page_slot_count(bz_page_t page) {
    return page == BZ_PAGE_HERO ? 5 : 0;
}

const char *bz_page_name(bz_page_t page) {
    static const char *const names[BZ_PAGE_COUNT] = {"HERO", "TARGET", "ASIC"};
    return page < BZ_PAGE_COUNT ? names[page] : "?";
}

const char *bz_page_slot_name(bz_page_t page, int slot) {
    static const char *const hero[] = {"MAIN", "TOP LEFT", "TOP RIGHT", "LOW LEFT", "LOW RIGHT"};
    if (slot < 0 || slot >= bz_page_slot_count(page)) return "?";
    return hero[slot];
}

void bz_settings_defaults(bz_settings_t *s) {
    static const uint8_t hero[BZ_MAX_SLOTS] = {BZ_M_HASHRATE, BZ_M_CHIP_TEMP, BZ_M_POWER, BZ_M_FAN,
                                               BZ_M_BEST_DIFF};
    memset(s, 0, sizeof(*s));
    s->page = BZ_PAGE_HERO;
    s->brightness = BZ_BRIGHTNESS_LEVELS - 1;
    memcpy(s->slots[BZ_PAGE_HERO], hero, sizeof(hero));
}

static uint32_t crc32(const uint8_t *data, size_t size) {
    uint32_t crc = 0xffffffffu;
    while (size--) {
        crc ^= *data++;
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static uint32_t stored_crc(const uint8_t *blob) {
    const uint8_t *p = blob + BZ_SETTINGS_BLOB_SIZE - 4;
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

void bz_settings_encode(const bz_settings_t *s, uint8_t blob[BZ_SETTINGS_BLOB_SIZE]) {
    memset(blob, 0, BZ_SETTINGS_BLOB_SIZE);
    memcpy(blob, "BZUI", 4);
    blob[4] = BLOB_VERSION;
    blob[5] = BLOB_PAYLOAD;
    blob[6] = s->page;
    blob[7] = 0; // Was the big-digit style, when there was a choice of two
    blob[8] = s->brightness;
    memcpy(&blob[9], s->slots, sizeof(s->slots));
    uint32_t crc = crc32(blob, BZ_SETTINGS_BLOB_SIZE - 4);
    for (int i = 0; i < 4; ++i) blob[BZ_SETTINGS_BLOB_SIZE - 4 + i] = (uint8_t)(crc >> (8 * i));
}

bool bz_settings_decode(const uint8_t blob[BZ_SETTINGS_BLOB_SIZE], bz_settings_t *out) {
    if (memcmp(blob, "BZUI", 4) || blob[4] < 1u || blob[4] > BLOB_VERSION || blob[5] != BLOB_PAYLOAD ||
        crc32(blob, BZ_SETTINGS_BLOB_SIZE - 4) != stored_crc(blob)) return false;
    bz_settings_t s;
    memset(&s, 0, sizeof(s));
    s.page = blob[6];
    s.brightness = blob[8];
    if (blob[4] == BLOB_VERSION) {
        memcpy(s.slots, &blob[9], sizeof(s.slots));
    } else {
        // An earlier set of pages: only HERO and TARGET are still here.
        if (s.page >= BLOB_PAGE_ROWS) return false;
        s.page = blob[4] == 2u && s.page == 3u ? BZ_PAGE_TARGET : BZ_PAGE_HERO;
        memcpy(s.slots[BZ_PAGE_HERO], &blob[9], BZ_MAX_SLOTS);
    }
    if (s.page >= BZ_PAGE_COUNT || s.brightness >= BZ_BRIGHTNESS_LEVELS) return false;
    for (int page = 0; page < BZ_PAGE_COUNT; ++page)
        for (int slot = 0; slot < BZ_MAX_SLOTS; ++slot)
            if (s.slots[page][slot] >= BZ_M_COUNT) return false;
    *out = s;
    return true;
}
