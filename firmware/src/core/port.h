/*
 * Serial ports as seen by the application. Implemented by hw/port_uart.c on
 * the board and by a capture fake in the unit tests.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum smart_port {
	PORT_GLIDER,
	PORT_SENSOR,
	PORT_NUM,
};

struct port_stats {
	uint32_t rx_bytes;
	uint32_t tx_bytes;
	uint32_t rx_dropped; /* RX ring full: bytes lost */
	uint32_t rx_errors;  /* UART overrun / framing errors */
};

int port_init(void);
/* Block until either port receives data or ms elapse. */
void port_wait_ms(int32_t ms);
void port_write(enum smart_port port, const char *data, size_t len);
size_t port_read(enum smart_port port, uint8_t *buf, size_t cap);
void port_discard_rx(enum smart_port port);
const struct port_stats *port_get_stats(enum smart_port port);

static inline void port_puts(enum smart_port port, const char *s)
{
	port_write(port, s, strlen(s));
}
