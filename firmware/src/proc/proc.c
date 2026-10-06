#include "proc/proc.h"

#include <string.h>
#include <zephyr/sys/util.h>

#ifdef CONFIG_SMART_PROC_UVP6_LPM
extern const struct proc_plugin proc_uvp6_lpm;
#endif

/* Register additional plug-ins here. */
static const struct proc_plugin *const plugins[] = {
#ifdef CONFIG_SMART_PROC_UVP6_LPM
	&proc_uvp6_lpm,
#endif
	NULL, /* keeps the array non-empty */
};

static struct segment_summary seg;
static bool open;
static int part;

void proc_init(void)
{
	memset(&seg, 0, sizeof(seg));
	open = false;
}

int proc_part_for_phase(enum glider_phase phase)
{
	return phase == PHASE_DIVE ? 1 : phase == PHASE_CLIMB ? 2 : 0;
}

void proc_segment_begin(uint32_t segment, enum glider_phase phase, bool with_time,
			int64_t start_epoch)
{
	memset(&seg, 0, sizeof(seg));
	seg.segment = segment;
	seg.phase = phase;
	seg.started_with_time = with_time;
	seg.start_epoch = start_epoch;
	part = proc_part_for_phase(phase);
	open = true;

	for (size_t i = 0; i < ARRAY_SIZE(plugins); i++) {
		if (plugins[i] && plugins[i]->begin) {
			plugins[i]->begin(&seg, part);
		}
	}
}

void proc_on_record(const struct proc_record *rec)
{
	if (!open) {
		return;
	}
	seg.records++;
	if (rec->has_depth) {
		if (!seg.depth_valid || rec->depth > seg.max_depth) {
			seg.max_depth = rec->depth;
		}
		seg.last_depth = rec->depth;
		seg.depth_valid = true;
	}
	for (size_t i = 0; i < ARRAY_SIZE(plugins); i++) {
		if (plugins[i] && plugins[i]->record) {
			plugins[i]->record(rec, part);
		}
	}
}

bool proc_segment_end(struct segment_summary *out)
{
	if (!open) {
		return false;
	}
	open = false;
	for (size_t i = 0; i < ARRAY_SIZE(plugins); i++) {
		if (plugins[i] && plugins[i]->end) {
			plugins[i]->end(&seg, part);
		}
	}
	*out = seg;
	return true;
}

bool proc_segment_open(void)
{
	return open;
}
