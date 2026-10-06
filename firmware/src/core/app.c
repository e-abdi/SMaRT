#include "core/app.h"

#include <zephyr/logging/log.h>

#include "core/controller.h"
#include "core/events.h"
#include "core/linebuf.h"
#include "core/nav.h"
#include "core/store.h"
#include "glider/glider.h"
#include "proc/proc.h"
#include "sensor/sensor.h"
#include "util/strutil.h"

LOG_MODULE_REGISTER(smart, CONFIG_SMART_LOG_LEVEL);

/* Deliver a sensor line without line ending after this much silence */
#define SENSOR_LINE_IDLE_MS 300

static char glider_mem[CONFIG_SMART_LINE_MAX];
static char sensor_mem[CONFIG_SMART_LINE_MAX];
static struct linebuf glider_lb;
static struct linebuf sensor_lb;

void app_init(int64_t now_ms)
{
	ev_reset();
	nav_reset();
	store_init();
	proc_init();
	linebuf_init(&glider_lb, glider_mem, sizeof(glider_mem));
	linebuf_init(&sensor_lb, sensor_mem, sizeof(sensor_mem));
	/* logdev wakes the device with a bare CR */
	glider_lb.emit_empty = IS_ENABLED(CONFIG_SMART_GLIDER_SEAGLIDER);
	glider_init();
	sensor_init();
	controller_init(now_ms);
	controller_step(now_ms);
}

static void glider_line(const char *line, int64_t now_ms)
{
	if (controller_in_passthrough()) {
#ifdef CONFIG_SMART_PASSTHROUGH
		if (str_keyword(line, CONFIG_SMART_PASSTHROUGH_EXIT, NULL)) {
			ev_post(EV_PASSTHROUGH_EXIT, 0);
		}
#endif
	} else {
		LOG_DBG("glider: %s", line);
		glider_on_line(line, now_ms);
	}
	controller_step(now_ms);
}

static void sensor_line(const char *line, int64_t now_ms)
{
	if (!controller_in_passthrough()) {
		LOG_DBG("sensor: %s", line);
		sensor_on_line(line, now_ms);
	}
	controller_step(now_ms);
}

void app_feed(enum smart_port port, const uint8_t *data, size_t len, int64_t now_ms)
{
	for (size_t i = 0; i < len; i++) {
		char ch = (char)data[i];

		if (controller_in_passthrough()) {
			port_write(port == PORT_GLIDER ? PORT_SENSOR : PORT_GLIDER, &ch, 1);
			controller_passthrough_activity(now_ms);
		}
		if (port == PORT_GLIDER) {
			if (linebuf_feed(&glider_lb, ch, now_ms)) {
				glider_line(glider_lb.buf, now_ms);
			}
		} else if (linebuf_feed(&sensor_lb, ch, now_ms)) {
			sensor_line(sensor_lb.buf, now_ms);
		}
	}
}

void app_poll(int64_t now_ms)
{
	if (linebuf_take_idle(&sensor_lb, now_ms, SENSOR_LINE_IDLE_MS)) {
		sensor_line(sensor_lb.buf, now_ms);
	}
	controller_step(now_ms);
}

int64_t app_next_deadline(void)
{
	return controller_next_deadline();
}
