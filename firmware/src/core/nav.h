/* What the board knows about the glider: clock, depth and flight phase. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

enum glider_phase {
	PHASE_UNKNOWN,
	PHASE_SURFACE,
	PHASE_DIVE,
	PHASE_CLIMB,
	PHASE_HOVER,
};

struct nav_state {
	bool time_valid;
	int64_t epoch;    /* glider time at epoch_ms */
	int64_t epoch_ms; /* uptime when epoch was received */
	bool depth_valid;
	double depth;
	int64_t depth_ms;
	enum glider_phase phase;
};

void nav_reset(void);
const struct nav_state *nav_get(void);

void nav_set_time(int64_t epoch, int64_t now_ms);
void nav_set_depth(double depth, int64_t now_ms);
void nav_set_phase(enum glider_phase phase);

/* Current glider time extrapolated with the board uptime. */
bool nav_time_now(int64_t now_ms, int64_t *epoch);

const char *phase_name(enum glider_phase phase);
