#ifndef BONANZA_SETTINGS_STORE_H
#define BONANZA_SETTINGS_STORE_H

// One small settings blob in the last sector of the RP2354A's internal flash.
// Firmware updates do not touch that sector, so display settings survive them.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Copies the stored bytes out. The caller validates them (magic, CRC).
bool settings_store_load(uint8_t *blob, size_t size);
// Erases and rewrites the sector if the contents differ. Interrupts are off
// for the duration (tens of milliseconds); call from the main loop only.
bool settings_store_save(const uint8_t *blob, size_t size);

#endif
