/* Unit tests for the protocol and formatting helpers. */
#include <errno.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "core/linebuf.h"
#include "core/store.h"
#include "util/fmt.h"
#include "util/nmea.h"
#include "util/strutil.h"
#include "util/template.h"
#include "util/timeutil.h"

ZTEST_SUITE(util, NULL, NULL, NULL, NULL, NULL);

ZTEST(util, test_nmea_checksum_matches_bsd_manual)
{
	/* Examples from the G3S Backseat Driver guide, chapter 4 */
	zassert_equal(nmea_checksum("SW,0:4.000000", 13), 0x38);
	zassert_equal(nmea_checksum("SW,0:0.12,1:508.0", 17), 0x3B);
}

ZTEST(util, test_nmea_build_and_parse)
{
	char out[64];
	struct nmea_frame f;

	zassert_equal(nmea_build(out, sizeof(out), "SW,0:4.000000"), 19);
	zassert_str_equal(out, "$SW,0:4.000000*38\r\n");

	zassert_ok(nmea_parse("$SD,4:1234567890.0,5:12.5*7B", &f));
	zassert_true(nmea_type_is(&f, "SD"));
	zassert_equal(f.fields_len, strlen("4:1234567890.0,5:12.5"));

	nmea_build(out, sizeof(out), "SD,4:1234567890.0,5:12.5");
	out[strlen(out) - 2] = '\0'; /* strip CRLF */
	zassert_ok(nmea_parse(out, &f));
	zassert_equal(f.cs, NMEA_CS_OK);

	out[1] = 'X'; /* corrupt one character */
	zassert_ok(nmea_parse(out, &f));
	zassert_equal(f.cs, NMEA_CS_BAD);

	zassert_ok(nmea_parse("$HI", &f));
	zassert_equal(f.cs, NMEA_CS_ABSENT);
	zassert_true(nmea_type_is(&f, "HI"));

	zassert_equal(nmea_parse("HI*00", &f), -EINVAL);
	zassert_equal(nmea_parse("$", &f), -EINVAL);
}

struct pairs {
	int idx[8];
	char val[8][32];
	int n;
};

static void collect(int idx, const char *value, void *user)
{
	struct pairs *p = user;

	p->idx[p->n] = idx;
	strcpy(p->val[p->n], value);
	p->n++;
}

ZTEST(util, test_nmea_pairs)
{
	struct pairs p = {0};
	const char *body = "4:1234.5,5:-3,bogus,7:,:9,6:1";
	int bad = nmea_for_each_pair(body, strlen(body), collect, &p);

	zassert_equal(p.n, 3);
	zassert_equal(bad, 3);
	zassert_equal(p.idx[0], 4);
	zassert_str_equal(p.val[0], "1234.5");
	zassert_equal(p.idx[1], 5);
	zassert_str_equal(p.val[1], "-3");
	zassert_equal(p.idx[2], 6);
}

ZTEST(util, test_time_conversion)
{
	struct civil_time ct;

	epoch_to_civil(1234567890, &ct);
	zassert_equal(ct.year, 2009);
	zassert_equal(ct.month, 2);
	zassert_equal(ct.day, 13);
	zassert_equal(ct.hour, 23);
	zassert_equal(ct.min, 31);
	zassert_equal(ct.sec, 30);
	zassert_equal(civil_to_epoch(&ct), 1234567890);

	zassert_true(civil_parse_compact("20240229", "235959", &ct));
	zassert_equal(civil_to_epoch(&ct), 1709251199);
	zassert_false(civil_parse_compact("2024022", "235959", &ct));
	zassert_false(civil_parse_compact("20241301", "000000", &ct));
}

ZTEST(util, test_template)
{
	char out[96];
	struct tmpl_ctx ctx = {.have_time = true, .epoch = 1234567890};

	zassert_equal(tmpl_expand("$start:ACQ_CSCS_002H,%Y%m%d,%H%M%S;%n", &ctx, out,
				  sizeof(out)),
		      38);
	zassert_str_equal(out, "$start:ACQ_CSCS_002H,20090213,233130;\n");

	ctx.have_time = false;
	zassert_equal(tmpl_expand("%Y", &ctx, out, sizeof(out)), -ENODATA);
	zassert_equal(tmpl_expand("%D", &ctx, out, sizeof(out)), -ENODATA);
	ctx.have_depth = true;
	ctx.depth = -1.0075;
	ctx.cast = 2;
	zassert_true(tmpl_expand("D=%D c=%c 100%%%r", &ctx, out, sizeof(out)) > 0);
	zassert_str_equal(out, "D=-1.01 c=2 100%\r");
	zassert_equal(tmpl_expand("%q", &ctx, out, sizeof(out)), -EINVAL);
	zassert_equal(tmpl_expand("abcdef", &ctx, out, 4), -ENOSPC);

	zassert_true(tmpl_needs_time("x%Hy"));
	zassert_false(tmpl_needs_time("%%H %D"));
}

ZTEST(util, test_fmt_fixed)
{
	char out[24];

	fmt_fixed(out, sizeof(out), 25.255, 2);
	zassert_str_equal(out, "25.26");
	fmt_fixed(out, sizeof(out), -0.004, 2);
	zassert_str_equal(out, "0.00");
	fmt_fixed(out, sizeof(out), -3.5, 0);
	zassert_str_equal(out, "-4");
	fmt_fixed(out, sizeof(out), 7, 3);
	zassert_str_equal(out, "7.000");
}

static int feed(struct linebuf *lb, const char *s, char lines[][32])
{
	int n = 0;

	for (; *s; s++) {
		if (linebuf_feed(lb, *s, 0)) {
			strcpy(lines[n++], lb->buf);
		}
	}
	return n;
}

ZTEST(util, test_linebuf)
{
	char mem[8];
	char lines[8][32];
	struct linebuf lb;

	linebuf_init(&lb, mem, sizeof(mem));
	zassert_equal(feed(&lb, "ab\r\ncd\n\n\re\r", lines), 3);
	zassert_str_equal(lines[0], "ab");
	zassert_str_equal(lines[1], "cd");
	zassert_str_equal(lines[2], "e");

	/* Overflow: the long line is dropped, the next one survives */
	zassert_equal(feed(&lb, "0123456789\nok\n", lines), 1);
	zassert_str_equal(lines[0], "ok");
	zassert_equal(lb.overflows, 1);

	/* logdev wake-up: a bare CR is a line, CRLF is one line not two */
	lb.emit_empty = true;
	zassert_equal(feed(&lb, "\r", lines), 1);
	zassert_str_equal(lines[0], "");
	zassert_equal(feed(&lb, "go\r\n", lines), 1);

	/* A partial line is taken after it goes idle */
	linebuf_feed(&lb, 'S', 100);
	linebuf_feed(&lb, '>', 100);
	zassert_false(linebuf_take_idle(&lb, 200, 300));
	zassert_true(linebuf_take_idle(&lb, 400, 300));
	zassert_str_equal(lb.buf, "S>");
}

static void append_out(const char *data, size_t len, void *user)
{
	strncat(user, data, len);
	strcat(user, "|");
}

ZTEST(util, test_store_partitions)
{
	char out[256] = "";

	store_init();
	store_append(1, "dive-a");
	store_append(2, "climb-a");
	store_append(1, "dive-b");
	store_dump(1, append_out, out);
	zassert_str_equal(out, "dive-a|dive-b|");

	store_clear(1);
	out[0] = '\0';
	store_dump(-1, append_out, out);
	zassert_str_equal(out, "climb-a|");
	zassert_equal(store_used(), strlen("2climb-a\n"));
}

ZTEST(util, test_keyword)
{
	const char *rest;

	zassert_true(str_keyword("START 20260101,000000,1", "START", &rest));
	zassert_str_equal(rest, "20260101,000000,1");
	zassert_true(str_keyword("stop", "STOP", NULL));
	zassert_false(str_keyword("STOPPED", "STOP", NULL));
	zassert_true(str_keyword("DEPTH:12.5", "DEPTH:", &rest));
	zassert_str_equal(rest, "12.5");
	zassert_true(str_keyword("$MIRROR*12", "$MIRROR", NULL));
}
