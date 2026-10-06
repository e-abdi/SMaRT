#include "util/nmea.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static int hex_val(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	return -1;
}

uint8_t nmea_checksum(const char *s, size_t n)
{
	uint8_t cs = 0;

	for (size_t i = 0; i < n; i++) {
		cs ^= (uint8_t)s[i];
	}
	return cs;
}

int nmea_parse(const char *line, struct nmea_frame *f)
{
	if (line == NULL || line[0] != '$') {
		return -EINVAL;
	}

	const char *body = line + 1;
	const char *star = strchr(body, '*');
	size_t body_len = star ? (size_t)(star - body) : strlen(body);

	/* Trim trailing whitespace on frames without a checksum */
	while (!star && body_len > 0 &&
	       (body[body_len - 1] == ' ' || body[body_len - 1] == '\t')) {
		body_len--;
	}

	if (body_len == 0) {
		return -EINVAL;
	}

	const char *comma = memchr(body, ',', body_len);

	f->type = body;
	f->type_len = comma ? (size_t)(comma - body) : body_len;
	f->fields = comma ? comma + 1 : body + body_len;
	f->fields_len = comma ? body_len - f->type_len - 1 : 0;

	if (!star) {
		f->cs = NMEA_CS_ABSENT;
		return 0;
	}

	int hi = hex_val(star[1]);
	int lo = (hi >= 0) ? hex_val(star[2]) : -1;

	if (hi < 0 || lo < 0) {
		f->cs = NMEA_CS_BAD;
		return 0;
	}
	f->cs = (nmea_checksum(body, body_len) == (uint8_t)((hi << 4) | lo)) ? NMEA_CS_OK
									      : NMEA_CS_BAD;
	return 0;
}

bool nmea_type_is(const struct nmea_frame *f, const char *type)
{
	size_t n = strlen(type);

	return f->type_len == n && memcmp(f->type, type, n) == 0;
}

int nmea_build(char *out, size_t cap, const char *payload)
{
	static const char hex[] = "0123456789ABCDEF";
	size_t n = strlen(payload);

	/* '$' + payload + '*' + 2 hex + CR LF + NUL */
	if (n + 7 > cap) {
		return -ENOSPC;
	}

	uint8_t cs = nmea_checksum(payload, n);

	out[0] = '$';
	memcpy(&out[1], payload, n);
	out[n + 1] = '*';
	out[n + 2] = hex[cs >> 4];
	out[n + 3] = hex[cs & 0x0F];
	out[n + 4] = '\r';
	out[n + 5] = '\n';
	out[n + 6] = '\0';
	return (int)n + 6;
}

int nmea_for_each_pair(const char *fields, size_t len, nmea_pair_cb cb, void *user)
{
	int bad = 0;
	size_t pos = 0;

	while (pos < len) {
		const char *tok = fields + pos;
		const char *end = memchr(tok, ',', len - pos);
		size_t tok_len = end ? (size_t)(end - tok) : len - pos;
		const char *colon = memchr(tok, ':', tok_len);

		pos += tok_len + 1;

		if (tok_len == 0) {
			continue;
		}
		if (colon == NULL || colon == tok || colon == tok + tok_len - 1) {
			bad++;
			continue;
		}

		char num[8];
		size_t num_len = (size_t)(colon - tok);
		char value[32];
		size_t val_len = tok_len - num_len - 1;

		if (num_len >= sizeof(num) || val_len >= sizeof(value)) {
			bad++;
			continue;
		}
		memcpy(num, tok, num_len);
		num[num_len] = '\0';
		memcpy(value, colon + 1, val_len);
		value[val_len] = '\0';

		char *num_end;
		long idx = strtol(num, &num_end, 10);

		if (*num_end != '\0' || idx < 0 || idx > 255) {
			bad++;
			continue;
		}
		cb((int)idx, value, user);
	}
	return bad;
}
