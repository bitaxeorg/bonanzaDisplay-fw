#ifndef BZ_MODEL_H
#define BZ_MODEL_H

// Data shared by the retro UI screens: the live host snapshot, the metric
// catalogue, and the user's saved display settings.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BZ_UNKNOWN UINT32_MAX
#define BZ_RSSI_UNKNOWN INT32_MIN

typedef enum {
    BZ_LINK_WAITING,    // No valid host response yet, or the host went quiet
    BZ_LINK_CONNECTED,
    BZ_LINK_AP_MODE,    // Host is in WiFi setup (access point) mode
    BZ_LINK_RESTARTING,
} bz_link_t;

// Snapshot of everything the host has told us. Numeric fields use BZ_UNKNOWN
// (or BZ_RSSI_UNKNOWN) until a value arrives; strings are empty.
typedef struct {
    bz_link_t link;
    char family[32];
    char asic[16];
    char ip[16];
    char best_diff[32];
    char pool[48];      // Stratum host name
    char net_diff[16];  // Network difficulty as the host words it: "129.44T"
    uint32_t hashrate_ghs;
    uint32_t chip_temp_c;
    uint32_t vr_temp_c;
    uint32_t power_mw;
    uint32_t voltage_mv;
    uint32_t current_ma;
    uint32_t fan_rpm;
    uint32_t frequency_mhz;
    uint32_t shares_accepted;
    uint32_t shares_rejected;
    uint32_t block_height;
    int32_t rssi_dbm;
} bz_data_t;

void bz_data_reset(bz_data_t *data);

// ---- Metric catalogue -----------------------------------------------------

// Values are stored in flash; append new metrics, never renumber.
typedef enum {
    BZ_M_NONE = 0,
    BZ_M_HASHRATE,
    BZ_M_CHIP_TEMP,
    BZ_M_VR_TEMP,
    BZ_M_POWER,
    BZ_M_EFFICIENCY,
    BZ_M_VOLTAGE,
    BZ_M_CURRENT,
    BZ_M_FAN,
    BZ_M_BEST_DIFF,
    BZ_M_SHARES,
    BZ_M_REJECTED,
    BZ_M_BLOCK_HEIGHT,
    BZ_M_WIFI_RSSI,
    BZ_M_IP,
    BZ_M_POOL,
    BZ_M_DEVICE,
    BZ_M_FREQUENCY,
    BZ_M_NET_DIFF,
    BZ_M_COUNT
} bz_metric_t;

typedef struct {
    char value[24]; // "1.21", "58", "192.168.1.234" or "--"
    char unit[6];   // "TH/s", "C", "" ...
    bool valid;     // Host has supplied the value
    bool gauge;     // Has a number that can drive a graph or bar meter
    float number;   // Gauge value in the metric's base unit (GH/s for hashrate)
    float unit_div; // Base units per shown unit: 1000 when GH/s reads as TH/s
    float fixed_max; // Meter full scale in base units, or 0 to auto-range
} bz_reading_t;

const char *bz_metric_name(bz_metric_t metric);  // Menu name, <= 12 chars
const char *bz_metric_short(bz_metric_t metric); // Slot label, <= 4 chars
void bz_metric_read(bz_metric_t metric, const bz_data_t *data, bz_reading_t *out);

// ---- Settings ---------------------------------------------------------------

typedef enum {
    BZ_PAGE_HERO,   // One big metric with a history graph, four icon metrics,
                    // and the stratum host and network difficulty
    BZ_PAGE_GRID,   // Six equal metrics
    BZ_PAGE_DUO,    // Two big metrics
    BZ_PAGE_COUNT
} bz_page_t;

#define BZ_MAX_SLOTS 6
#define BZ_BRIGHTNESS_LEVELS 4

typedef enum { BZ_DIGITS_SOLID, BZ_DIGITS_DOT_MATRIX, BZ_DIGITS_COUNT } bz_digits_t;

typedef struct {
    uint8_t page;                              // bz_page_t shown while mining
    uint8_t slots[BZ_PAGE_COUNT][BZ_MAX_SLOTS]; // bz_metric_t per page slot
    uint8_t digits;                            // bz_digits_t for big numbers
    uint8_t brightness;                        // 0..BZ_BRIGHTNESS_LEVELS-1
} bz_settings_t;

#define BZ_SETTINGS_BLOB_SIZE 64u

int bz_page_slot_count(bz_page_t page);
const char *bz_page_name(bz_page_t page);
const char *bz_page_slot_name(bz_page_t page, int slot);
void bz_settings_defaults(bz_settings_t *settings);
// Fixed-size blob with magic, version and CRC. Decode returns false (and
// leaves *settings untouched) for anything it does not recognise.
void bz_settings_encode(const bz_settings_t *settings, uint8_t blob[BZ_SETTINGS_BLOB_SIZE]);
bool bz_settings_decode(const uint8_t blob[BZ_SETTINGS_BLOB_SIZE], bz_settings_t *settings);

#endif
