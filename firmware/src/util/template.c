#include "util/template.h"

#include <errno.h>
#include <string.h>
#include <zephyr/sys/printk.h>

#include "util/fmt.h"
#include "util/timeutil.h"

static bool is_time_code(char c)
{
	return c != '\0' && strchr("YmdHMSe", c) != NULL;
}

bool tmpl_needs_time(const char *tmpl)
{
	for (const char *p = tmpl; *p; p++) {
		if (*p == '%') {
			if (is_time_code(p[1])) {
				return true;
			}
			if (p[1] != '\0') {
				p++;
			}
		}
	}
	return false;
}

int tmpl_expand(const char *tmpl, const struct tmpl_ctx *ctx, char *out, size_t cap)
{
	struct civil_time ct = {0};
	size_t n = 0;

	if (cap == 0) {
		return -ENOSPC;
	}
	if (ctx->have_time) {
		epoch_to_civil(ctx->epoch, &ct);
	}

	for (const char *p = tmpl; *p; p++) {
		char piece[24];
		int len;

		if (*p != '%') {
			piece[0] = *p;
			len = 1;
		} else {
			char code = *++p;

			if (code == '\0') {
				return -EINVAL;
			}
			if (is_time_code(code) && !ctx->have_time) {
				return -ENODATA;
			}
			switch (code) {
			case 'Y':
				len = snprintk(piece, sizeof(piece), "%04d", ct.year);
				break;
			case 'm':
				len = snprintk(piece, sizeof(piece), "%02d", ct.month);
				break;
			case 'd':
				len = snprintk(piece, sizeof(piece), "%02d", ct.day);
				break;
			case 'H':
				len = snprintk(piece, sizeof(piece), "%02d", ct.hour);
				break;
			case 'M':
				len = snprintk(piece, sizeof(piece), "%02d", ct.min);
				break;
			case 'S':
				len = snprintk(piece, sizeof(piece), "%02d", ct.sec);
				break;
			case 'e':
				len = snprintk(piece, sizeof(piece), "%lld", (long long)ctx->epoch);
				break;
			case 'D':
				if (!ctx->have_depth) {
					return -ENODATA;
				}
				len = fmt_fixed(piece, sizeof(piece), ctx->depth, 2);
				break;
			case 'c':
				len = snprintk(piece, sizeof(piece), "%d", ctx->cast);
				break;
			case 's':
				len = snprintk(piece, sizeof(piece), "%u", ctx->segment);
				break;
			case 'r':
				piece[0] = '\r';
				len = 1;
				break;
			case 'n':
				piece[0] = '\n';
				len = 1;
				break;
			case '%':
				piece[0] = '%';
				len = 1;
				break;
			default:
				return -EINVAL;
			}
			if (len < 0) {
				return len;
			}
		}
		if (n + (size_t)len >= cap) {
			return -ENOSPC;
		}
		memcpy(out + n, piece, (size_t)len);
		n += (size_t)len;
	}
	out[n] = '\0';
	return (int)n;
}
