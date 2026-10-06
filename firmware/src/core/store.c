#include "core/store.h"

#include <errno.h>
#include <string.h>

/*
 * Layout: a sequence of records, each "<part byte><text>\n". Lines never
 * contain '\n', so the buffer can be walked without a separate index.
 */
static char buf[CONFIG_SMART_STORE_SIZE];
static size_t used;
static uint32_t dropped;
static bool downloaded[STORE_PARTS];

void store_init(void)
{
	used = 0;
	dropped = 0;
	memset(downloaded, 0, sizeof(downloaded));
}

int store_append(int part, const char *line)
{
	size_t n = strcspn(line, "\n");

	if (part < 0 || part >= STORE_PARTS) {
		part = 0;
	}
	if (used + n + 2 > sizeof(buf)) {
		dropped++;
		return -ENOSPC;
	}
	buf[used++] = (char)('0' + part);
	memcpy(&buf[used], line, n);
	used += n;
	buf[used++] = '\n';
	return 0;
}

void store_clear(int part)
{
	size_t rd = 0, wr = 0;

	while (rd < used) {
		size_t len = (size_t)((char *)memchr(&buf[rd], '\n', used - rd) - &buf[rd]) + 1;

		if (part >= 0 && buf[rd] - '0' != part) {
			memmove(&buf[wr], &buf[rd], len);
			wr += len;
		}
		rd += len;
	}
	used = wr;
	if (part < 0) {
		memset(downloaded, 0, sizeof(downloaded));
	} else if (part < STORE_PARTS) {
		downloaded[part] = false;
	}
}

size_t store_used(void)
{
	return used;
}

uint32_t store_dropped(void)
{
	return dropped;
}

void store_dump(int part, store_write_fn write, void *user)
{
	size_t rd = 0;

	while (rd < used) {
		size_t len = (size_t)((char *)memchr(&buf[rd], '\n', used - rd) - &buf[rd]) + 1;

		/* Skip the partition tag and the '\n' */
		if (part < 0 || buf[rd] - '0' == part) {
			write(&buf[rd + 1], len - 2, user);
		}
		rd += len;
	}
}

void store_mark_downloaded(int part)
{
	if (part >= 0 && part < STORE_PARTS) {
		downloaded[part] = true;
	}
}

bool store_downloaded(int part)
{
	return part >= 0 && part < STORE_PARTS && downloaded[part];
}
