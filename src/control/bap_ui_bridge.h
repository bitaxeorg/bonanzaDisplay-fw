#ifndef BONANZA_BAP_UI_BRIDGE_H
#define BONANZA_BAP_UI_BRIDGE_H

// Copies a BAP metrics snapshot into the UI's own data model, so neither the
// protocol client nor the UI needs to know about the other. Shared by the
// firmware main loop and the desktop simulator.

#include <stdio.h>

#include "app/bz_model.h"
#include "bap_client.h"

static inline void bonanza_ui_data_from_metrics(const bonanza_metrics_t *m, bz_data_t *d) {
    bz_data_reset(d);
    d->link = m->link == BONANZA_LINK_CONNECTED ? BZ_LINK_CONNECTED
              : m->link == BONANZA_LINK_AP_MODE ? BZ_LINK_AP_MODE
              : m->link == BONANZA_LINK_RESTARTING ? BZ_LINK_RESTARTING : BZ_LINK_WAITING;
    snprintf(d->family, sizeof(d->family), "%s", m->device_family);
    snprintf(d->asic, sizeof(d->asic), "%s", m->device_model);
    // The client uses "--" for text it has not received; the UI uses "".
    if (m->ip_address[0] != '-') snprintf(d->ip, sizeof(d->ip), "%s", m->ip_address);
    if (m->best_share[0] != '-') snprintf(d->best_diff, sizeof(d->best_diff), "%s", m->best_share);
    snprintf(d->pool, sizeof(d->pool), "%s", m->pool);
    snprintf(d->net_diff, sizeof(d->net_diff), "%s", m->network_difficulty);
    // Both sides mark unknown numbers with UINT32_MAX / INT32_MIN.
    d->hashrate_ghs = m->hashrate_ghs;
    d->chip_temp_c = m->temperature_c;
    d->vr_temp_c = m->vr_temp_c;
    d->power_mw = m->power_mw;
    d->voltage_mv = m->voltage_mv;
    d->current_ma = m->current_ma;
    d->fan_rpm = m->fan_rpm;
    d->frequency_mhz = m->frequency_mhz;
    d->shares_accepted = m->shares_accepted;
    d->shares_rejected = m->shares_rejected;
    d->block_height = m->block_height;
    d->rssi_dbm = m->wifi_rssi_dbm;
}

#endif
