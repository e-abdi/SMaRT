// main.c — minimal Zephyr app:
// Continuously send 'A' on UART0 and 'B' on UART1. Nothing else.
//
// If you still see other text on the serial lines at boot, that's from
// Zephyr's console/logging config, not this file.

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>

#define TX_DELAY_MS 10  // Adjust if you want faster/slower output (0 = as fast as possible)

#define UART0_NODE DT_NODELABEL(uart0)
#define UART1_NODE DT_NODELABEL(uart1)

#if !DT_NODE_EXISTS(UART0_NODE)
#error "Device tree node 'uart0' not found"
#endif

#if !DT_NODE_EXISTS(UART1_NODE)
#error "Device tree node 'uart1' not found"
#endif

static const struct device *const uart0_dev = DEVICE_DT_GET(UART0_NODE);
static const struct device *const uart1_dev = DEVICE_DT_GET(UART1_NODE);

void main(void)
{
    // If either UART isn't ready, just idle quietly.
    if (!device_is_ready(uart0_dev) || !device_is_ready(uart1_dev)) {
        while (1) {
            k_msleep(1000);
        }
    }

    // Forever: 'A' -> UART0, 'B' -> UART1
    while (1) {
        uart_poll_out(uart0_dev, 'A');
        uart_poll_out(uart1_dev, 'B');
        k_msleep(TX_DELAY_MS);
    }
}

