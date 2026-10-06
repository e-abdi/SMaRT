#include "core/events.h"

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_DECLARE(smart, CONFIG_SMART_LOG_LEVEL);

#define EV_QUEUE_LEN 32

static struct smart_ev queue[EV_QUEUE_LEN];
static uint32_t head, tail, dropped;

void ev_reset(void)
{
	head = tail = dropped = 0;
}

void ev_post(enum smart_ev_type type, int32_t arg)
{
	if (head - tail >= EV_QUEUE_LEN) {
		dropped++;
		LOG_ERR("event queue full, dropped %s", ev_name(type));
		return;
	}
	queue[head % EV_QUEUE_LEN] = (struct smart_ev){.type = type, .arg = arg};
	head++;
}

bool ev_get(struct smart_ev *ev)
{
	if (head == tail) {
		return false;
	}
	*ev = queue[tail % EV_QUEUE_LEN];
	tail++;
	return true;
}

uint32_t ev_dropped(void)
{
	return dropped;
}

const char *ev_name(enum smart_ev_type type)
{
	static const char *const names[] = {
		[EV_GLIDER_ACTIVE] = "GLIDER_ACTIVE",
		[EV_GLIDER_INACTIVE] = "GLIDER_INACTIVE",
		[EV_NAV] = "NAV",
		[EV_GLIDER_START] = "GLIDER_START",
		[EV_GLIDER_STOP] = "GLIDER_STOP",
		[EV_PASSTHROUGH_ENTER] = "PASSTHROUGH_ENTER",
		[EV_PASSTHROUGH_EXIT] = "PASSTHROUGH_EXIT",
		[EV_SENSOR_READY] = "SENSOR_READY",
		[EV_SENSOR_START_ACK] = "SENSOR_START_ACK",
		[EV_SENSOR_STOP_ACK] = "SENSOR_STOP_ACK",
		[EV_SENSOR_DATA] = "SENSOR_DATA",
		[EV_TIMEOUT] = "TIMEOUT",
	};

	return (type < ARRAY_SIZE(names) && names[type]) ? names[type] : "?";
}
