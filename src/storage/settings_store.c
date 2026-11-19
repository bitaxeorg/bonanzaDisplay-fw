#include "settings_store.h"

#include <string.h>

#include "hardware/flash.h"
#include "hardware/sync.h"

// The RP2354A has 2 MiB of internal flash. This is deliberately not
// PICO_FLASH_SIZE_BYTES: the pico2 board profile used for the build describes
// a 4 MiB part, and the store must stay inside the real device.
#define STORE_FLASH_BYTES (2u * 1024u * 1024u)
#define STORE_OFFSET (STORE_FLASH_BYTES - FLASH_SECTOR_SIZE)

static const uint8_t *stored(void) {
    return (const uint8_t *)(XIP_BASE + STORE_OFFSET);
}

bool settings_store_load(uint8_t *blob, size_t size) {
    if (size > FLASH_PAGE_SIZE) return false;
    memcpy(blob, stored(), size);
    return true;
}

bool settings_store_save(const uint8_t *blob, size_t size) {
    if (size > FLASH_PAGE_SIZE) return false;
    if (!memcmp(stored(), blob, size)) return true; // Unchanged: no flash wear

    uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xff, sizeof(page));
    memcpy(page, blob, size);
    // Flash is unreadable while it is being written, so nothing that lives
    // in flash (including interrupt handlers) may run until this is done.
    uint32_t irq_state = save_and_disable_interrupts();
    flash_range_erase(STORE_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(STORE_OFFSET, page, FLASH_PAGE_SIZE);
    restore_interrupts(irq_state);
    return !memcmp(stored(), blob, size);
}
