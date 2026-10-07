/*
 * SMaRT self-test firmware.
 *
 * Bring-up diagnostic for the board, driven one line at a time from either
 * UART at 115200 8N1. No glider or sensor protocol: every command below is
 * answered with something that could not come from a wire merely looped
 * back on itself, so a reply actually proves the MCU and that UART path are
 * both alive.
 *
 *   ID          -> "SMaRT-SELFTEST PORT=GLIDER\r\n" or "...PORT=SENSOR\r\n"
 *   PING        -> "PONG\r\n"
 *   RELAY ON    -> drives the sensor power relay on, replies "RELAY=ON\r\n"
 *   RELAY OFF   -> drives it off, replies "RELAY=OFF\r\n"
 *   (anything else) -> forwarded to the *other* UART as "FWD:<PORT>:<line>",
 *                      with "FWD:SENT\r\n" echoed back to the sender.
 *
 * See tools/assembly_check.py, which drives this over pyserial.
 */
#include <string.h>
#include <strings.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define GLIDER_NODE DT_CHOSEN(smart_glider_uart)
#define SENSOR_NODE DT_CHOSEN(smart_sensor_uart)
#define USER_NODE   DT_PATH(zephyr_user)

static const struct device *const glider_dev = DEVICE_DT_GET(GLIDER_NODE);
static const struct device *const sensor_dev = DEVICE_DT_GET(SENSOR_NODE);

#if DT_NODE_HAS_PROP(USER_NODE, sensor_power_gpios)
static const struct gpio_dt_spec relay = GPIO_DT_SPEC_GET(USER_NODE, sensor_power_gpios);
#define HAVE_RELAY 1
#endif

enum port_id { P_GLIDER, P_SENSOR, P_COUNT };

struct line_buf {
	char buf[128];
	size_t len;
};

static struct line_buf lb[P_COUNT];

static const char *port_name(enum port_id p)
{
	return p == P_GLIDER ? "GLIDER" : "SENSOR";
}

static const struct device *port_dev(enum port_id p)
{
	return p == P_GLIDER ? glider_dev : sensor_dev;
}

static void send_str(enum port_id p, const char *s)
{
	const struct device *dev = port_dev(p);

	for (const char *c = s; *c != '\0'; c++) {
		uart_poll_out(dev, (unsigned char)*c);
	}
}

static bool line_is(const char *line, const char *word)
{
	return strcasecmp(line, word) == 0;
}

static void handle_line(enum port_id p, char *line)
{
	char reply[160];

	/* strip trailing CR/LF */
	size_t n = strlen(line);

	while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == '\n')) {
		line[--n] = '\0';
	}
	if (n == 0) {
		return;
	}

	if (line_is(line, "PING")) {
		send_str(p, "PONG\r\n");
	} else if (line_is(line, "ID")) {
		snprintk(reply, sizeof(reply), "SMaRT-SELFTEST PORT=%s\r\n", port_name(p));
		send_str(p, reply);
	} else if (line_is(line, "RELAY ON") || line_is(line, "RELAY OFF")) {
#ifdef HAVE_RELAY
		bool on = line_is(line, "RELAY ON");

		gpio_pin_set_dt(&relay, on ? 1 : 0);
		snprintk(reply, sizeof(reply), "RELAY=%s\r\n", on ? "ON" : "OFF");
		send_str(p, reply);
#else
		send_str(p, "RELAY=UNAVAILABLE\r\n");
#endif
	} else {
		enum port_id other = (p == P_GLIDER) ? P_SENSOR : P_GLIDER;

		snprintk(reply, sizeof(reply), "FWD:%s:%s\r\n", port_name(p), line);
		send_str(other, reply);
		send_str(p, "FWD:SENT\r\n");
	}
}

static void feed(enum port_id p, uint8_t ch)
{
	struct line_buf *b = &lb[p];

	if (ch == '\n' || ch == '\r') {
		if (b->len > 0) {
			b->buf[b->len] = '\0';
			handle_line(p, b->buf);
			b->len = 0;
		}
		return;
	}
	if (b->len < sizeof(b->buf) - 1) {
		b->buf[b->len++] = (char)ch;
	}
}

int main(void)
{
	bool g_ready = device_is_ready(glider_dev);
	bool s_ready = device_is_ready(sensor_dev);

	printk("SMaRT self-test: glider uart ready=%d sensor uart ready=%d\n", g_ready, s_ready);

#ifdef HAVE_RELAY
	if (!gpio_is_ready_dt(&relay) ||
	    gpio_pin_configure_dt(&relay, GPIO_OUTPUT_INACTIVE) != 0) {
		printk("SMaRT self-test: relay GPIO not available\n");
	}
#endif

	if (g_ready) {
		send_str(P_GLIDER, "SMaRT-SELFTEST READY PORT=GLIDER\r\n");
	}
	if (s_ready) {
		send_str(P_SENSOR, "SMaRT-SELFTEST READY PORT=SENSOR\r\n");
	}

	for (;;) {
		unsigned char c;

		if (g_ready) {
			while (uart_poll_in(glider_dev, &c) == 0) {
				feed(P_GLIDER, c);
			}
		}
		if (s_ready) {
			while (uart_poll_in(sensor_dev, &c) == 0) {
				feed(P_SENSOR, c);
			}
		}
		k_sleep(K_MSEC(2));
	}
	return 0;
}
