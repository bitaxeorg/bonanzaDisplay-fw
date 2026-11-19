#include "bap_control.h"

#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/uart.h"
#include "pico/time.h"
#include "pin_config.h"

#define CONTROL_UART uart1
#define CONTROL_UART_IRQ UART1_IRQ
#define RX_BUFFER_SIZE 2048u
#define RX_BUFFER_MASK (RX_BUFFER_SIZE - 1u)

static uint8_t rx_buffer[RX_BUFFER_SIZE];
static volatile uint32_t rx_head, rx_tail;
static volatile bool rx_error;
static bonanza_bap_client_t client;

static void __not_in_flash_func(bap_uart_irq)(void) {
    while (uart_is_readable(CONTROL_UART)) {
        uint32_t data = uart_get_hw(CONTROL_UART)->dr;
        uint32_t next = (rx_head + 1u) & RX_BUFFER_MASK;
        if ((data & (UART_UARTDR_OE_BITS | UART_UARTDR_BE_BITS |
                     UART_UARTDR_PE_BITS | UART_UARTDR_FE_BITS)) || next == rx_tail) {
            rx_error = true;
        } else if (!rx_error) {
            rx_buffer[rx_head] = (uint8_t)data;
            rx_head = next;
        }
    }
    uart_get_hw(CONTROL_UART)->rsr = 0; // Clear sticky receive error flags.
}

void bonanza_bap_control_init(void) {
    rx_head = rx_tail = 0;
    rx_error = false;
    bonanza_bap_client_init(&client, to_ms_since_boot(get_absolute_time()));
    uart_init(CONTROL_UART, BONANZA_BAP_BAUD_RATE);
    gpio_set_function(PIN_CONTROL_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_CONTROL_RX, GPIO_FUNC_UART);
    gpio_pull_up(PIN_CONTROL_RX);
    uart_set_hw_flow(CONTROL_UART, false, false);
    uart_set_format(CONTROL_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(CONTROL_UART, true);
    irq_set_exclusive_handler(CONTROL_UART_IRQ, bap_uart_irq);
    irq_set_enabled(CONTROL_UART_IRQ, true);
    uart_set_irqs_enabled(CONTROL_UART, true, false);
}

void bonanza_bap_control_poll(void) {
    uint8_t chunk[128];
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    for (unsigned batch = 0; batch < RX_BUFFER_SIZE / sizeof(chunk); ++batch) {
        size_t size = 0;
        uint32_t irq_state = save_and_disable_interrupts();
        if (rx_error) {
            // A dropped/corrupt byte invalidates the partial sentence. Discard
            // the backlog and resynchronize at the next '$', never apply it.
            rx_tail = rx_head;
            rx_error = false;
            bonanza_bap_client_reset_stream(&client);
        }
        while (size < sizeof(chunk) && rx_tail != rx_head) {
            chunk[size++] = rx_buffer[rx_tail];
            rx_tail = (rx_tail + 1u) & RX_BUFFER_MASK;
        }
        restore_interrupts(irq_state);
        if (!size) break;
        bonanza_bap_client_receive(&client, chunk, size, now_ms);
    }
    char message[BONANZA_BAP_MAX_MESSAGE];
    size_t size = bonanza_bap_client_next_message(&client, now_ms, message, sizeof(message));
    if (size) uart_write_blocking(CONTROL_UART, (const uint8_t *)message, size);
}

bool bonanza_bap_control_take_update(bonanza_metrics_t *metrics) {
    return bonanza_bap_client_take_update(&client, metrics);
}
