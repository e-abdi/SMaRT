/*
 * Seaglider logdev adapter: the board presents itself to the glider as a
 * logger device described by a .cnf file (see the .cnf file in the profile folder).
 *
 * Glider -> board (keywords configurable):
 *   <CR>                              wake-up, answered with the prompt
 *   START [YYYYMMDD,HHMMSS][,cast]    start sampling (cast: 1 dive, 2 climb)
 *   STOP                              stop sampling, prompt when done
 *   DEPTH:<m>                         depth update from status= (no reply)
 *   SEND_TXT_FILE [cast]              dump stored data, then prompt
 *   INFO                              board status, then prompt
 */
#include <stdlib.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include "core/controller.h"
#include "core/events.h"
#include "core/nav.h"
#include "core/port.h"
#include "core/store.h"
#include "glider/glider.h"
#include "util/fmt.h"
#include "util/strutil.h"
#include "util/timeutil.h"

LOG_MODULE_DECLARE(smart, CONFIG_SMART_LOG_LEVEL);

static bool active;
static bool link_seen;
static uint32_t bad_cmds;

void glider_init(void)
{
	active = false;
	link_seen = false;
	bad_cmds = 0;
}

bool glider_commands_sampling(void)
{
	return true;
}

uint32_t glider_health_bits(void)
{
	return (link_seen ? HEALTH_GLIDER_LINK : 0) | (bad_cmds ? HEALTH_GLIDER_ERRS : 0);
}

static void prompt(void)
{
	port_puts(PORT_GLIDER, CONFIG_SMART_LOGDEV_PROMPT "\r");
}

static void reply(const char *text)
{
	port_puts(PORT_GLIDER, text);
	port_puts(PORT_GLIDER, "\r\n");
}

static void dump_line(const char *data, size_t len, void *user)
{
	ARG_UNUSED(user);
	port_write(PORT_GLIDER, data, len);
	port_puts(PORT_GLIDER, "\r\n");
}

/* Parse "[YYYYMMDD,HHMMSS][,cast]" */
static int parse_start_args(const char *args, int64_t now_ms)
{
	char date[9] = {0}, time[7] = {0};
	int cast = 0;
	const char *p = args;
	struct civil_time ct;

	if (strlen(p) >= 15 && p[8] == ',') {
		memcpy(date, p, 8);
		memcpy(time, p + 9, 6);
		if (civil_parse_compact(date, time, &ct)) {
			nav_set_time(civil_to_epoch(&ct), now_ms);
		} else {
			LOG_WRN("bad START time: %s", args);
		}
		p += 15;
		while (*p == ',' || *p == ' ') {
			p++;
		}
	}
	if (*p >= '0' && *p <= '2') {
		cast = *p - '0';
	}
	return cast;
}

static void cmd_info(int64_t now_ms)
{
	char line[96];
	int64_t epoch;

	snprintk(line, sizeof(line), "SMaRT %s state=%s health=0x%02x", CONFIG_SMART_SENSOR_NAME,
		 controller_state_name(controller_state()), controller_health());
	reply(line);
	if (nav_time_now(now_ms, &epoch)) {
		snprintk(line, sizeof(line), "time=%lld", (long long)epoch);
		reply(line);
	}
	snprintk(line, sizeof(line), "store=%u bytes dropped=%u", (unsigned)store_used(),
		 store_dropped());
	reply(line);
}

void glider_on_line(const char *line, int64_t now_ms)
{
	const char *args;

	while (*line == ' ') {
		line++;
	}
	link_seen = true;
	if (!active) {
		active = true;
		ev_post(EV_GLIDER_ACTIVE, 0);
	}

	if (*line == '\0') {
		prompt();
	} else if (str_keyword(line, CONFIG_SMART_LOGDEV_CMD_START, &args)) {
		int cast = parse_start_args(args, now_ms);

		if (cast == 1 || cast == 2) {
			nav_set_phase(cast == 1 ? PHASE_DIVE : PHASE_CLIMB);
		}
		/* Data already downloaded for this cast is no longer needed */
		if (store_downloaded(cast)) {
			store_clear(cast);
		}
		LOG_INF("glider START cast %d", cast);
		ev_post(EV_NAV, 0);
		ev_post(EV_GLIDER_START, cast);
		prompt();
	} else if (str_keyword(line, CONFIG_SMART_LOGDEV_CMD_STOP, NULL)) {
		LOG_INF("glider STOP");
		ev_post(EV_GLIDER_STOP, 0); /* prompt sent by glider_on_cmd_done() */
	} else if (str_keyword(line, CONFIG_SMART_LOGDEV_CMD_DEPTH, &args)) {
		char *end;
		double d = strtod(args, &end);

		if (end != args) {
			nav_set_depth(d, now_ms);
			ev_post(EV_NAV, 0);
		}
	} else if (str_keyword(line, CONFIG_SMART_LOGDEV_CMD_DOWNLOAD, &args)) {
		int part = (*args >= '0' && *args <= '2') ? *args - '0' : -1;

		LOG_INF("glider download part %d (%u bytes in store)", part,
			(unsigned)store_used());
		store_dump(part, dump_line, NULL);
		if (part < 0) {
			for (int i = 0; i < STORE_PARTS; i++) {
				store_mark_downloaded(i);
			}
		} else {
			store_mark_downloaded(part);
		}
		prompt();
	} else if (str_keyword(line, CONFIG_SMART_LOGDEV_CMD_INFO, NULL)) {
		cmd_info(now_ms);
		prompt();
#ifdef CONFIG_SMART_PASSTHROUGH
	} else if (str_keyword(line, CONFIG_SMART_PASSTHROUGH_ENTER, NULL)) {
		ev_post(EV_PASSTHROUGH_ENTER, 0);
#endif
	} else {
		bad_cmds++;
		LOG_WRN("unknown glider command: %s", line);
		reply("?");
		prompt();
	}
}

void glider_on_boot_done(uint32_t health)
{
	ARG_UNUSED(health);
	/* logdev's powerup-timeout waits for this prompt */
	prompt();
}

void glider_on_session_start(uint32_t health)
{
	ARG_UNUSED(health);
}

void glider_on_cmd_done(enum glider_cmd cmd, bool ok)
{
	ARG_UNUSED(ok);
	if (cmd == GLIDER_CMD_STOP) {
		prompt();
	}
}

void glider_on_sensor_started(const struct segment_summary *seg)
{
	ARG_UNUSED(seg);
}

void glider_on_segment_end(const struct segment_summary *seg, uint32_t health)
{
	char line[96];
	char maxd[16] = "", lastd[16] = "";

	if (seg->depth_valid) {
		fmt_fixed(maxd, sizeof(maxd), seg->max_depth, 2);
		fmt_fixed(lastd, sizeof(lastd), seg->last_depth, 2);
	}
	snprintk(line, sizeof(line), "#SEG,%u,%s,%u,%s,%s,0x%02x", seg->segment,
		 phase_name(seg->phase), seg->records, maxd, lastd, health);
	(void)store_append(proc_part_for_phase(seg->phase), line);
}

void glider_on_start_failed(uint32_t health)
{
	char line[48];

	snprintk(line, sizeof(line), "#ERR,start failed,0x%02x", health);
	(void)store_append(proc_part_for_phase(nav_get()->phase), line);
}

void glider_on_passthrough(bool entered)
{
	reply(entered ? "PASSTHROUGH_ACTIVE" : "PASSTHROUGH_END");
	if (!entered) {
		prompt();
	}
}
