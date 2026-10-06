#pragma once

#include <ctype.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>

/*
 * True if line starts with keyword (case-insensitive) followed by the end of
 * the line, whitespace, ',', ':' or '*'. On success *rest points past the
 * keyword and any following separators.
 */
static inline bool str_keyword(const char *line, const char *keyword, const char **rest)
{
	size_t n = strlen(keyword);

	if (n == 0 || strncasecmp(line, keyword, n) != 0) {
		return false;
	}

	const char *p = line + n;
	char last = keyword[n - 1];

	/* Keywords ending in a separator (e.g. "DEPTH:") need no extra check */
	if (*p != '\0' && last != ':' && last != ',' && last != '=' && !isspace((unsigned char)*p) &&
	    *p != ',' && *p != ':' && *p != '*') {
		return false;
	}
	while (*p == ' ' || *p == '\t' || *p == ',') {
		p++;
	}
	if (rest) {
		*rest = p;
	}
	return true;
}
