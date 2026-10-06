#include "util/timeutil.h"

#include <ctype.h>

/* Howard Hinnant's days_from_civil / civil_from_days algorithms. */

void epoch_to_civil(int64_t epoch, struct civil_time *ct)
{
	int64_t days = epoch / 86400;
	int64_t rem = epoch % 86400;

	if (rem < 0) {
		rem += 86400;
		days--;
	}
	ct->hour = (int)(rem / 3600);
	ct->min = (int)((rem % 3600) / 60);
	ct->sec = (int)(rem % 60);

	int64_t z = days + 719468;
	int64_t era = (z >= 0 ? z : z - 146096) / 146097;
	int64_t doe = z - era * 146097;
	int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	int64_t mp = (5 * doy + 2) / 153;
	int64_t d = doy - (153 * mp + 2) / 5 + 1;
	int64_t m = mp < 10 ? mp + 3 : mp - 9;

	ct->year = (int)(yoe + era * 400 + (m <= 2));
	ct->month = (int)m;
	ct->day = (int)d;
}

int64_t civil_to_epoch(const struct civil_time *ct)
{
	int64_t y = ct->year - (ct->month <= 2);
	int64_t era = (y >= 0 ? y : y - 399) / 400;
	int64_t yoe = y - era * 400;
	int64_t mp = ct->month > 2 ? ct->month - 3 : ct->month + 9;
	int64_t doy = (153 * mp + 2) / 5 + ct->day - 1;
	int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	int64_t days = era * 146097 + doe - 719468;

	return days * 86400 + ct->hour * 3600 + ct->min * 60 + ct->sec;
}

static bool digits(const char *s, int n, int *out)
{
	int v = 0;

	for (int i = 0; i < n; i++) {
		if (!isdigit((unsigned char)s[i])) {
			return false;
		}
		v = v * 10 + (s[i] - '0');
	}
	*out = v;
	return true;
}

bool civil_parse_compact(const char *date8, const char *time6, struct civil_time *ct)
{
	int ymd, hms;

	if (!digits(date8, 8, &ymd) || !digits(time6, 6, &hms)) {
		return false;
	}
	ct->year = ymd / 10000;
	ct->month = (ymd / 100) % 100;
	ct->day = ymd % 100;
	ct->hour = hms / 10000;
	ct->min = (hms / 100) % 100;
	ct->sec = hms % 100;

	return ct->year >= 2000 && ct->month >= 1 && ct->month <= 12 && ct->day >= 1 &&
	       ct->day <= 31 && ct->hour < 24 && ct->min < 60 && ct->sec < 61;
}
