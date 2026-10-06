/*
 * Glider adapter interface. Exactly one adapter is built, selected by
 * CONFIG_SMART_GLIDER_*. The adapter translates the glider protocol into
 * events (core/events.h) and nav updates (core/nav.h), and turns controller
 * notifications into glider messages.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/sys/util.h>

#include "proc/proc.h"

enum glider_cmd {
	GLIDER_CMD_START,
	GLIDER_CMD_STOP,
};

/* Health bit field reported to the glider. Bits 0-1 match legacy SW,0. */
#define HEALTH_GLIDER_LINK  BIT(0) /* valid glider traffic received */
#define HEALTH_SENSOR_READY BIT(1) /* sensor power-up banner seen */
#define HEALTH_SAMPLING     BIT(2)
#define HEALTH_TIME_VALID   BIT(3) /* glider time known */
#define HEALTH_START_FAILED BIT(4) /* last start got no ack */
#define HEALTH_STOP_FAILED  BIT(5) /* last stop got no ack */
#define HEALTH_GLIDER_ERRS  BIT(6) /* glider checksum / framing errors seen */
#define HEALTH_LINK_ERRS    BIT(7) /* UART overruns or dropped bytes */

void glider_init(void);
void glider_on_line(const char *line, int64_t now_ms);

/* True when the glider decides when sampling starts and stops (logdev). */
bool glider_commands_sampling(void);

/* Bits the adapter contributes to the health field. */
uint32_t glider_health_bits(void);

/* Controller notifications */
void glider_on_boot_done(uint32_t health);
void glider_on_session_start(uint32_t health);
void glider_on_cmd_done(enum glider_cmd cmd, bool ok);
void glider_on_sensor_started(const struct segment_summary *seg);
void glider_on_segment_end(const struct segment_summary *seg, uint32_t health);
void glider_on_start_failed(uint32_t health);
void glider_on_passthrough(bool entered);
