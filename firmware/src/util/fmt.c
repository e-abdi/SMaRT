#include "util/fmt.h"

#include <errno.h>
#include <stdint.h>
#include <zephyr/sys/printk.h>

int fmt_fixed(char *out, size_t cap, double v, int decimals)
{
	static const int64_t scale[] = {1, 10, 100, 1000, 10000, 100000, 1000000};

	if (decimals < 0 || decimals > 6) {
		return -EINVAL;
	}
	/* Reject NaN and values that would overflow the integer path */
	if (!(v > -1e12 && v < 1e12)) {
		return -ERANGE;
	}

	int64_t s = scale[decimals];
	int64_t q = (int64_t)(v * (double)s + (v < 0 ? -0.5 : 0.5));
	const char *sign = q < 0 ? "-" : "";
	uint64_t a = q < 0 ? (uint64_t)-q : (uint64_t)q;
	int n;

	if (decimals == 0) {
		n = snprintk(out, cap, "%s%llu", sign, (unsigned long long)a);
	} else {
		n = snprintk(out, cap, "%s%llu.%0*llu", sign, (unsigned long long)(a / s), decimals,
			     (unsigned long long)(a % s));
	}
	if (n < 0 || (size_t)n >= cap) {
		return -ENOSPC;
	}
	return n;
}
