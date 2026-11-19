#ifndef BONANZA_BAP_CONTROL_H
#define BONANZA_BAP_CONTROL_H

#include "bap_client.h"

#define BONANZA_BAP_BAUD_RATE 115200u

void bonanza_bap_control_init(void);
// Drain buffered UART input and service requests/subscriptions in the main loop.
void bonanza_bap_control_poll(void);
bool bonanza_bap_control_take_update(bonanza_metrics_t *metrics);

#endif
