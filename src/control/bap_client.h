#ifndef BONANZA_BAP_CLIENT_H
#define BONANZA_BAP_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BONANZA_BAP_MAX_MESSAGE 256u
#define BONANZA_METRIC_UNKNOWN UINT32_MAX
#define BONANZA_RSSI_UNKNOWN INT32_MIN

typedef enum {
    BONANZA_LINK_WAITING,    // No recognized response yet, or the host timed out
    BONANZA_LINK_CONNECTED,
    BONANZA_LINK_AP_MODE,    // Host reported WiFi setup (access point) mode
    BONANZA_LINK_RESTARTING, // Host announced a restart
} bonanza_link_t;

typedef struct {
    char device_family[32];
    char device_model[16]; // ASIC model: the current host does not send board revision.
    char device_name[32]; // Connection status: the current host does not send hostname.
    char ip_address[16];
    char best_share[32];
    uint32_t hashrate_ghs;
    uint32_t temperature_c;
    uint32_t power_w;
    uint32_t frequency_mhz;
    uint32_t fan_rpm;
    // Fields below feed the selectable mining-screen metrics.
    bonanza_link_t link;
    char pool[48];
    char network_difficulty[16]; // Empty until a host that reports it does
    uint32_t power_mw;        // Same reading as power_w, kept to 1 mW
    uint32_t vr_temp_c;
    uint32_t voltage_mv;      // Input voltage; the host reports millivolts
    uint32_t current_ma;      // Input current; the host reports milliamps
    uint32_t shares_accepted;
    uint32_t shares_rejected;
    uint32_t block_height;
    int32_t wifi_rssi_dbm;    // BONANZA_RSSI_UNKNOWN until reported
} bonanza_metrics_t;

// Platform-independent streaming client. All calls run in the main loop.
typedef struct {
    bonanza_metrics_t metrics;
    char sentence[BONANZA_BAP_MAX_MESSAGE];
    size_t sentence_length;
    uint32_t last_response_ms;
    uint32_t last_cycle_ms;
    uint32_t last_probe_ms;
    uint32_t next_send_ms;
    uint8_t command_index;
    bool cycle_active;
    bool connected;
    bool update_pending;
} bonanza_bap_client_t;

void bonanza_bap_client_init(bonanza_bap_client_t *client, uint32_t now_ms);
void bonanza_bap_client_reset_stream(bonanza_bap_client_t *client);
void bonanza_bap_client_receive(bonanza_bap_client_t *client,
                                const uint8_t *data, size_t size, uint32_t now_ms);
// Returns one framed command, or zero when no command is due. No blocking waits.
size_t bonanza_bap_client_next_message(bonanza_bap_client_t *client,
                                       uint32_t now_ms, char *out, size_t capacity);
bool bonanza_bap_client_take_update(bonanza_bap_client_t *client,
                                     bonanza_metrics_t *metrics);
size_t bonanza_bap_format(char *out, size_t capacity, const char *command,
                          const char *parameter, const char *value);

#endif
