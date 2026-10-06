#include "fake_hw.h"

#include <string.h>
#include <zephyr/sys/util.h>

#include "core/app.h"
#include "core/board_io.h"
#include "util/nmea.h"

#define TX_CAP 16384

static char tx[PORT_NUM][TX_CAP];
static size_t tx_len[PORT_NUM];
static struct port_stats stats[PORT_NUM];
static bool powered;
int64_t t_now;

void fake_reset(void)
{
	memset(tx_len, 0, sizeof(tx_len));
	memset(stats, 0, sizeof(stats));
	tx[PORT_GLIDER][0] = tx[PORT_SENSOR][0] = '\0';
	powered = false;
	t_now = 0;
}

int port_init(void)
{
	return 0;
}

void port_wait_ms(int32_t ms)
{
	ARG_UNUSED(ms);
}

void port_write(enum smart_port port, const char *data, size_t len)
{
	size_t room = TX_CAP - 1 - tx_len[port];
	size_t n = len < room ? len : room;

	memcpy(&tx[port][tx_len[port]], data, n);
	tx_len[port] += n;
	tx[port][tx_len[port]] = '\0';
}

size_t port_read(enum smart_port port, uint8_t *buf, size_t cap)
{
	ARG_UNUSED(port);
	ARG_UNUSED(buf);
	ARG_UNUSED(cap);
	return 0;
}

void port_discard_rx(enum smart_port port)
{
	ARG_UNUSED(port);
}

const struct port_stats *port_get_stats(enum smart_port port)
{
	return &stats[port];
}

int board_io_init(void)
{
	return 0;
}

void sensor_power_set(bool on)
{
	powered = on;
}

void board_watchdog_feed(void)
{
}

const char *fake_tx(enum smart_port port)
{
	return tx[port];
}

void fake_tx_clear(void)
{
	memset(tx_len, 0, sizeof(tx_len));
	tx[PORT_GLIDER][0] = tx[PORT_SENSOR][0] = '\0';
}

bool fake_sensor_powered(void)
{
	return powered;
}

int fake_count(enum smart_port port, const char *needle)
{
	int n = 0;
	size_t len = strlen(needle);

	for (const char *p = tx[port]; (p = strstr(p, needle)) != NULL; p += len) {
		n++;
	}
	return n;
}

void glider_says(const char *text)
{
	app_feed(PORT_GLIDER, (const uint8_t *)text, strlen(text), t_now);
}

void glider_nmea(const char *payload)
{
	glider_says(nmea_of(payload));
}

void sensor_says(const char *text)
{
	app_feed(PORT_SENSOR, (const uint8_t *)text, strlen(text), t_now);
}

void advance_ms(int ms)
{
	for (int i = 0; i < ms; i += 10) {
		t_now += 10;
		app_poll(t_now);
	}
}

const char *nmea_of(const char *payload)
{
	static char buf[4][256];
	static int slot;
	char *out = buf[slot++ % 4];

	nmea_build(out, sizeof(buf[0]), payload);
	return out;
}
