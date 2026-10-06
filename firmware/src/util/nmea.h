/*
 * NMEA-style framing used by the Slocum Backseat Driver:
 *   $TYPE,field,field*HH<CR><LF>
 * HH is the XOR of every character between '$' and '*'.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum nmea_cs {
	NMEA_CS_OK,
	NMEA_CS_ABSENT,
	NMEA_CS_BAD,
};

struct nmea_frame {
	const char *type;   /* e.g. "SD" (not NUL-terminated) */
	size_t type_len;
	const char *fields; /* text after "TYPE," up to '*' or end (may be empty) */
	size_t fields_len;
	enum nmea_cs cs;
};

uint8_t nmea_checksum(const char *s, size_t n);

/* Parse a line starting with '$'. Returns 0 or -EINVAL if not a frame. */
int nmea_parse(const char *line, struct nmea_frame *f);

bool nmea_type_is(const struct nmea_frame *f, const char *type);

/*
 * Build "$<payload>*HH\r\n" into out. Returns the length written or a
 * negative errno when it does not fit.
 */
int nmea_build(char *out, size_t cap, const char *payload);

/*
 * Walk "idx:value,idx:value" pairs (the SD/SW body). The callback gets the
 * index and a NUL-terminated copy of the value. Malformed pairs are skipped
 * and counted in the return value (number of bad pairs, >= 0).
 */
typedef void (*nmea_pair_cb)(int idx, const char *value, void *user);
int nmea_for_each_pair(const char *fields, size_t len, nmea_pair_cb cb, void *user);
