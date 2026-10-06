/*
 * Events flowing from the glider and sensor adapters to the controller.
 * Everything runs in the main thread, so the queue needs no locking.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

enum smart_ev_type {
	EV_GLIDER_ACTIVE,   /* glider opened a session ($HI, logdev command) */
	EV_GLIDER_INACTIVE, /* glider closed the session ($BY) */
	EV_NAV,             /* time, depth or phase updated (see nav.h) */
	EV_GLIDER_START,    /* glider commands sampling to start (arg: cast) */
	EV_GLIDER_STOP,     /* glider commands sampling to stop */
	EV_PASSTHROUGH_ENTER,
	EV_PASSTHROUGH_EXIT,
	EV_SENSOR_READY,    /* sensor printed its power-up banner */
	EV_SENSOR_START_ACK,
	EV_SENSOR_STOP_ACK,
	EV_SENSOR_DATA,     /* sensor produced a data record */
	EV_TIMEOUT,         /* controller deadline expired (internal) */
	EV_COUNT,
};

struct smart_ev {
	enum smart_ev_type type;
	int32_t arg;
};

void ev_reset(void);
void ev_post(enum smart_ev_type type, int32_t arg);
bool ev_get(struct smart_ev *ev);
uint32_t ev_dropped(void);
const char *ev_name(enum smart_ev_type type);
