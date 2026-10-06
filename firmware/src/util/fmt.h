/* Small formatting helpers that avoid floating point printf. */
#pragma once

#include <stddef.h>

/*
 * Format v with a fixed number of decimals (0-6), rounding half away from
 * zero. Returns the number of characters written, or a negative errno.
 */
int fmt_fixed(char *out, size_t cap, double v, int decimals);
