/*
 * UVP6 LPM summary, after the Smart-Cable Seaglider integration: keep one of
 * every N LPM_DATA records and reduce the 18 size classes to 5 sums so the
 * result is small enough to send home over Iridium.
 *
 * LPM_DATA,<depth>,<YYYYMMDD>,<HHMMSS>,<images>,<temp>,<18 classes>,<16 grey>;
 *
 * Output line: LPM,<depth>,<date>,<time>,<c0-3>,<c4-7>,<c8-11>,<c12-15>,<c16-17>
 */
#include <stdlib.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include "core/store.h"
#include "proc/proc.h"

LOG_MODULE_DECLARE(smart, CONFIG_SMART_LOG_LEVEL);

#define LPM_CLASSES       18
#define LPM_FIRST_CLASS   6 /* field index of the first size class */

static uint32_t seen;

static void lpm_begin(const struct segment_summary *seg, int part)
{
	ARG_UNUSED(seg);
	ARG_UNUSED(part);
	seen = 0;
}

static void lpm_record(const struct proc_record *rec, int part)
{
	if (strncmp(rec->line, "LPM_DATA,", 9) != 0) {
		return;
	}
	if (seen++ % CONFIG_SMART_PROC_UVP6_LPM_DECIMATE != 0) {
		return;
	}

	char copy[CONFIG_SMART_LINE_MAX];
	const char *field[LPM_FIRST_CLASS + LPM_CLASSES];
	size_t nf = 0;

	strncpy(copy, rec->line, sizeof(copy) - 1);
	copy[sizeof(copy) - 1] = '\0';
	/* Split on ',' and stop at the ';' terminator */
	for (char *p = copy; nf < ARRAY_SIZE(field);) {
		field[nf++] = p;
		p += strcspn(p, ",;");
		if (*p != ',') {
			*p = '\0';
			break;
		}
		*p++ = '\0';
	}
	if (nf < ARRAY_SIZE(field)) {
		LOG_WRN("LPM_DATA with %u fields ignored", (unsigned)nf);
		return;
	}

	long sum[5] = {0};

	for (int i = 0; i < LPM_CLASSES; i++) {
		sum[MIN(i / 4, 4)] += strtol(field[LPM_FIRST_CLASS + i], NULL, 10);
	}

	char out[96];

	snprintk(out, sizeof(out), "LPM,%s,%s,%s,%ld,%ld,%ld,%ld,%ld", field[1], field[2],
		 field[3], sum[0], sum[1], sum[2], sum[3], sum[4]);
	if (store_append(part, out) != 0) {
		LOG_WRN("store full, LPM summary dropped");
	}
}

const struct proc_plugin proc_uvp6_lpm = {
	.name = "uvp6_lpm",
	.begin = lpm_begin,
	.record = lpm_record,
};
