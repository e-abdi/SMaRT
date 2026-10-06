/*
 * Generic serial sensor, described entirely by the CONFIG_SMART_SENSOR_*
 * options: power-up banner, start/stop command templates, ack strings and
 * the data record format. Most sensors need no code, only a profile.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "util/template.h"

void sensor_init(void);

/* Handle one line received from the sensor port; posts EV_SENSOR_* events. */
void sensor_on_line(const char *line, int64_t now_ms);

/*
 * Send the start command. Uses the time-less template when ctx has no time.
 * Returns 0, -ENODATA if no usable template, or another negative errno.
 */
int sensor_send_start(const struct tmpl_ctx *ctx, bool *with_time);
int sensor_send_stop(const struct tmpl_ctx *ctx);

/* Whether a start command can be sent with or without glider time. */
bool sensor_can_start(bool have_time);
bool sensor_expects_start_ack(void);
bool sensor_expects_stop_ack(void);
