/*
 * Assembles bytes from a serial port into text lines. CR, LF and CRLF all
 * end a line. Empty lines are skipped unless emit_empty is set (a bare CR
 * is how Seaglider logdev wakes a device). A partial line that has been
 * idle for a while can also be taken, for devices that omit the line end.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct linebuf {
	char *buf;
	size_t cap;
	size_t len;
	bool ready;      /* buf holds a complete line until the next feed */
	bool discarding; /* overflowed: drop bytes until the next line end */
	bool emit_empty;
	bool last_cr;
	int64_t last_ms;
	uint32_t overflows;
};

void linebuf_init(struct linebuf *lb, char *storage, size_t cap);
void linebuf_reset(struct linebuf *lb);

/* Returns true when lb->buf holds a complete, NUL-terminated line. */
bool linebuf_feed(struct linebuf *lb, char c, int64_t now_ms);

/* Returns true (and terminates the line) if a partial line went idle. */
bool linebuf_take_idle(struct linebuf *lb, int64_t now_ms, int32_t idle_ms);
