/*
 * The SMaRT controller: one hierarchical state machine (Zephyr SMF) that
 * decides when the sensor runs, based on events from the glider and sensor
 * adapters and on the sampling policy.
 *
 *   BOOT -> IDLE <-> PASSTHROUGH
 *            |
 *          ACTIVE [ ARMED -> STARTING -> SAMPLING -> STOPPING -> HOLDOFF -> ARMED ]
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

enum ctrl_state {
	ST_ROOT,
	ST_BOOT,
	ST_IDLE,
	ST_PASSTHROUGH,
	ST_ACTIVE,
	ST_ARMED,
	ST_STARTING,
	ST_SAMPLING,
	ST_STOPPING,
	ST_HOLDOFF,
	ST_COUNT,
};

void controller_init(int64_t now_ms);

/* Process queued events and expired deadlines. Call after feeding input. */
void controller_step(int64_t now_ms);

/* Uptime of the next deadline, or INT64_MAX if none is pending. */
int64_t controller_next_deadline(void);

enum ctrl_state controller_state(void);
const char *controller_state_name(enum ctrl_state st);
bool controller_in_passthrough(void);
void controller_passthrough_activity(int64_t now_ms);
uint32_t controller_health(void);

/*
 * Pilot overrides received from the glider (Slocum sci_generic inputs).
 * mode: 0 off, 1 dive, 2 climb, 3 hover, 4 dive+climb, 5 always.
 * A negative depth clears the override.
 */
void policy_set_remote_mode(int mode);
void policy_set_remote_depth_min(double m);
void policy_set_remote_depth_max(double m);
