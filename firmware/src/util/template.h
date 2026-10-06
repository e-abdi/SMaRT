/*
 * Command templates, modelled on Seaglider logdev substitutions:
 *   %Y %m %d %H %M %S  UTC date/time from the glider clock (zero padded)
 *   %e                 UNIX epoch seconds
 *   %D                 last glider depth, metres, 2 decimals
 *   %c                 cast: 1 dive, 2 climb, 0 unknown
 *   %s                 segment number since power-up
 *   %r %n              CR, LF
 *   %%                 a literal '%'
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct tmpl_ctx {
	bool have_time;
	int64_t epoch;
	bool have_depth;
	double depth;
	int cast;
	uint32_t segment;
};

/*
 * Returns the expanded length, -ENODATA when the template needs time or
 * depth that is not available, -EINVAL for an unknown code, -ENOSPC when
 * out is too small.
 */
int tmpl_expand(const char *tmpl, const struct tmpl_ctx *ctx, char *out, size_t cap);

/* True if the template uses any of the time codes. */
bool tmpl_needs_time(const char *tmpl);
