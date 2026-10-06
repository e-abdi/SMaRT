/* UTC calendar conversions without libc time zone support. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

struct civil_time {
	int year;  /* e.g. 2026 */
	int month; /* 1-12 */
	int day;   /* 1-31 */
	int hour;
	int min;
	int sec;
};

void epoch_to_civil(int64_t epoch, struct civil_time *ct);
int64_t civil_to_epoch(const struct civil_time *ct);

/* Parse "YYYYMMDD" and "HHMMSS" digit strings. Returns false if malformed. */
bool civil_parse_compact(const char *date8, const char *time6, struct civil_time *ct);
