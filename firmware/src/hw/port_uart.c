/*
 * Interrupt-driven UART ports. The ISR moves bytes from the 32-byte RP2040
 * FIFO into a ring buffer, so nothing is lost while the main loop is busy.
 * TX uses polled output (messages are short and the RX side keeps running).
 */
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/ring_buffer.h>

#include "core/board_io.h"
#include "core/port.h"

LOG_MODULE_DECLARE(smart, CONFIG_SMART_LOG_LEVEL);

#define RX_RING_SIZE 2048

struct port {
	const struct device *dev;
	uint32_t baud;
	struct ring_buf rx;
	uint8_t rx_mem[RX_RING_SIZE];
	struct port_stats stats;
};

static struct port ports[PORT_NUM] = {
	[PORT_GLIDER] = {
		.dev = DEVICE_DT_GET(DT_CHOSEN(smart_glider_uart)),
		.baud = CONFIG_SMART_GLIDER_BAUD,
	},
	[PORT_SENSOR] = {
		.dev = DEVICE_DT_GET(DT_CHOSEN(smart_sensor_uart)),
		.baud = CONFIG_SMART_SENSOR_BAUD,
	},
};

static K_SEM_DEFINE(rx_sem, 0, 1);

static void uart_isr(const struct device *dev, void *user_data)
{
	struct port *p = user_data;
	uint8_t buf[32];

	if (!uart_irq_update(dev)) {
		return;
	}
	while (uart_irq_rx_ready(dev)) {
		int n = uart_fifo_read(dev, buf, sizeof(buf));

		if (n <= 0) {
			break;
		}
		uint32_t put = ring_buf_put(&p->rx, buf, (uint32_t)n);

		p->stats.rx_bytes += (uint32_t)n;
		p->stats.rx_dropped += (uint32_t)n - put;
	}
	if (uart_err_check(dev) > 0) {
		p->stats.rx_errors++;
	}
	k_sem_give(&rx_sem);
}

int port_init(void)
{
	for (int i = 0; i < PORT_NUM; i++) {
		struct port *p = &ports[i];
		struct uart_config cfg;
		int rc;

		if (!device_is_ready(p->dev)) {
			LOG_ERR("UART %s not ready", p->dev->name);
			return -ENODEV;
		}
		ring_buf_init(&p->rx, sizeof(p->rx_mem), p->rx_mem);

		rc = uart_config_get(p->dev, &cfg);
		if (rc == 0 && cfg.baudrate != p->baud) {
			cfg.baudrate = p->baud;
			rc = uart_configure(p->dev, &cfg);
		}
		if (rc != 0) {
			LOG_ERR("cannot set %s to %u baud (%d)", p->dev->name, p->baud, rc);
			return rc;
		}
		rc = uart_irq_callback_user_data_set(p->dev, uart_isr, p);
		if (rc != 0) {
			LOG_ERR("no IRQ support on %s (%d)", p->dev->name, rc);
			return rc;
		}
		uart_irq_rx_enable(p->dev);
		LOG_INF("%s port: %s @ %u", i == PORT_GLIDER ? "glider" : "sensor", p->dev->name,
			p->baud);
	}
	return 0;
}

void port_write(enum smart_port port, const char *data, size_t len)
{
	struct port *p = &ports[port];

	/* Long dumps at low baud rates would otherwise outlast the watchdog */
	board_watchdog_feed();
	for (size_t i = 0; i < len; i++) {
		uart_poll_out(p->dev, (unsigned char)data[i]);
		if ((i & 0xFF) == 0xFF) {
			board_watchdog_feed();
		}
	}
	p->stats.tx_bytes += len;
}

size_t port_read(enum smart_port port, uint8_t *buf, size_t cap)
{
	struct port *p = &ports[port];
	unsigned int key = irq_lock();
	uint32_t n = ring_buf_get(&p->rx, buf, (uint32_t)cap);

	irq_unlock(key);
	return n;
}

void port_discard_rx(enum smart_port port)
{
	struct port *p = &ports[port];
	unsigned int key = irq_lock();

	ring_buf_reset(&p->rx);
	irq_unlock(key);
}

void port_wait_ms(int32_t ms)
{
	(void)k_sem_take(&rx_sem, K_MSEC(MAX(ms, 0)));
}

const struct port_stats *port_get_stats(enum smart_port port)
{
	return &ports[port].stats;
}
