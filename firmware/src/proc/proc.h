/*
 * Data processing on sensor records. A "segment" is one start..stop run of
 * the sensor. Built-in statistics are always kept; optional plug-ins (see
 * proc.c) can derive their own products and write them to the store.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/nav.h"

struct segment_summary {
	uint32_t segment;
	enum glider_phase phase;
	bool started_with_time;
	int64_t start_epoch; /* 0 if the glider time was unknown */
	uint32_t records;
	bool depth_valid;    /* at least one record carried a depth */
	double max_depth;
	double last_depth;
};

struct proc_record {
	const char *line; /* full sensor line */
	bool has_depth;
	double depth;
};

/*
 * A processing plug-in. All callbacks are optional. part is the store
 * partition for this segment (1 dive, 2 climb, 0 otherwise).
 */
struct proc_plugin {
	const char *name;
	void (*begin)(const struct segment_summary *seg, int part);
	void (*record)(const struct proc_record *rec, int part);
	void (*end)(const struct segment_summary *seg, int part);
};

void proc_init(void);
void proc_segment_begin(uint32_t segment, enum glider_phase phase, bool with_time,
			int64_t start_epoch);
void proc_on_record(const struct proc_record *rec);
/* Closes the open segment; returns false if none was open. */
bool proc_segment_end(struct segment_summary *out);
bool proc_segment_open(void);
int proc_part_for_phase(enum glider_phase phase);
