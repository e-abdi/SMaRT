#include "core/nav.h"

#include <string.h>

static struct nav_state nav;

void nav_reset(void)
{
	memset(&nav, 0, sizeof(nav));
	nav.phase = PHASE_UNKNOWN;
}

const struct nav_state *nav_get(void)
{
	return &nav;
}

void nav_set_time(int64_t epoch, int64_t now_ms)
{
	nav.time_valid = true;
	nav.epoch = epoch;
	nav.epoch_ms = now_ms;
}

void nav_set_depth(double depth, int64_t now_ms)
{
	nav.depth_valid = true;
	nav.depth = depth;
	nav.depth_ms = now_ms;
}

void nav_set_phase(enum glider_phase phase)
{
	nav.phase = phase;
}

bool nav_time_now(int64_t now_ms, int64_t *epoch)
{
	if (!nav.time_valid) {
		return false;
	}
	*epoch = nav.epoch + (now_ms - nav.epoch_ms) / 1000;
	return true;
}

const char *phase_name(enum glider_phase phase)
{
	switch (phase) {
	case PHASE_SURFACE:
		return "surface";
	case PHASE_DIVE:
		return "dive";
	case PHASE_CLIMB:
		return "climb";
	case PHASE_HOVER:
		return "hover";
	default:
		return "unknown";
	}
}
