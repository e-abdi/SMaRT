#include "core/linebuf.h"

void linebuf_init(struct linebuf *lb, char *storage, size_t cap)
{
	lb->buf = storage;
	lb->cap = cap;
	lb->overflows = 0;
	lb->emit_empty = false;
	linebuf_reset(lb);
}

void linebuf_reset(struct linebuf *lb)
{
	lb->len = 0;
	lb->ready = false;
	lb->discarding = false;
	lb->last_cr = false;
	lb->last_ms = 0;
	lb->buf[0] = '\0';
}

bool linebuf_feed(struct linebuf *lb, char c, int64_t now_ms)
{
	if (lb->ready) {
		lb->ready = false;
		lb->len = 0;
	}
	lb->last_ms = now_ms;

	bool crlf = (c == '\n' && lb->last_cr);

	lb->last_cr = (c == '\r');
	if (c == '\r' || c == '\n') {
		if (lb->discarding) {
			lb->discarding = false;
			lb->len = 0;
			return false;
		}
		if (lb->len == 0 && (crlf || !lb->emit_empty)) {
			return false;
		}
		lb->buf[lb->len] = '\0';
		lb->ready = true;
		return true;
	}
	if (c == '\0' || lb->discarding) {
		return false;
	}
	if (lb->len + 1 >= lb->cap) {
		lb->discarding = true;
		lb->overflows++;
		lb->len = 0;
		return false;
	}
	lb->buf[lb->len++] = c;
	return false;
}

bool linebuf_take_idle(struct linebuf *lb, int64_t now_ms, int32_t idle_ms)
{
	if (lb->ready || lb->discarding || lb->len == 0 || idle_ms <= 0) {
		return false;
	}
	if (now_ms - lb->last_ms < idle_ms) {
		return false;
	}
	lb->buf[lb->len] = '\0';
	lb->ready = true;
	return true;
}
