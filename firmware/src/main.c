/*
 * SMaRT - Sensor Management and Relay Tool
 *
 * A single event loop: wait for UART data or the next controller deadline,
 * feed received bytes to the application core, feed the watchdog.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "core/app.h"
#include "core/board_io.h"
#include "core/controller.h"
#include "core/port.h"

LOG_MODULE_DECLARE(smart, CONFIG_SMART_LOG_LEVEL);

#define MAX_WAIT_MS  100
#define STATUS_EVERY 60000

static void log_status(void)
{
	const struct port_stats *g = port_get_stats(PORT_GLIDER);
	const struct port_stats *s = port_get_stats(PORT_SENSOR);

	LOG_INF("state=%s health=0x%02x glider rx=%u drop=%u err=%u, sensor rx=%u drop=%u err=%u",
		controller_state_name(controller_state()), controller_health(), g->rx_bytes,
		g->rx_dropped, g->rx_errors, s->rx_bytes, s->rx_dropped, s->rx_errors);
}

int main(void)
{
	uint8_t buf[64];
	int64_t next_status = STATUS_EVERY;

	LOG_INF("SMaRT starting: %s glider, sensor '%s'",
		IS_ENABLED(CONFIG_SMART_GLIDER_SLOCUM) ? "Slocum" : "Seaglider",
		CONFIG_SMART_SENSOR_NAME);

	if (board_io_init() != 0) {
		LOG_ERR("board I/O init failed, continuing");
	}
	if (port_init() != 0) {
		/* Without UARTs nothing can work; let the watchdog retry */
		LOG_ERR("serial ports unavailable");
		for (;;) {
			k_sleep(K_SECONDS(1));
		}
	}

	app_init(k_uptime_get());

	for (;;) {
		int64_t now = k_uptime_get();
		int64_t wait = MIN(app_next_deadline() - now, MAX_WAIT_MS);

		board_watchdog_feed();
		port_wait_ms((int32_t)MAX(wait, 0));

		for (int p = 0; p < PORT_NUM; p++) {
			size_t n;

			while ((n = port_read((enum smart_port)p, buf, sizeof(buf))) > 0) {
				app_feed((enum smart_port)p, buf, n, k_uptime_get());
			}
		}
		now = k_uptime_get();
		app_poll(now);

		if (now >= next_status) {
			next_status = now + STATUS_EVERY;
			log_status();
		}
	}
	return 0;
}
