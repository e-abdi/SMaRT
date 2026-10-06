/*
 * Slocum G3 Backseat Driver adapter (extctl proglet on the science bay).
 *
 * Glider -> board:  $HI  $BY  $SD,idx:value,...  $ER,code
 * Board -> glider:  $SW,idx:value,...
 * Indices are positions in extctl.ini and are set by the profile.
 */
#include <stdlib.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include "core/controller.h"
#include "core/events.h"
#include "core/nav.h"
#include "core/port.h"
#include "glider/glider.h"
#include "util/fmt.h"
#include "util/nmea.h"
#include "util/strutil.h"

LOG_MODULE_DECLARE(smart, CONFIG_SMART_LOG_LEVEL);

/* Times before 2000-01-01 are treated as "not set" */
#define MIN_VALID_EPOCH 946684800

static bool active;
static bool link_seen;
static uint32_t cs_errors;
static uint32_t bad_values;

void glider_init(void)
{
	active = false;
	link_seen = false;
	cs_errors = 0;
	bad_values = 0;
}

bool glider_commands_sampling(void)
{
	return false;
}

uint32_t glider_health_bits(void)
{
	return (link_seen ? HEALTH_GLIDER_LINK : 0) | (cs_errors ? HEALTH_GLIDER_ERRS : 0);
}

/* ---- $SW builder ---- */

struct sw_msg {
	char payload[160];
	size_t len;
	int pairs;
};

static void sw_init(struct sw_msg *m)
{
	m->len = (size_t)snprintk(m->payload, sizeof(m->payload), "SW");
	m->pairs = 0;
}

static void sw_add(struct sw_msg *m, int idx, double value, int decimals)
{
	char num[24];

	if (idx < 0 || fmt_fixed(num, sizeof(num), value, decimals) < 0) {
		return;
	}
	int n = snprintk(m->payload + m->len, sizeof(m->payload) - m->len, ",%d:%s", idx, num);

	if (n > 0 && m->len + (size_t)n < sizeof(m->payload)) {
		m->len += (size_t)n;
		m->pairs++;
	}
}

static void sw_send(struct sw_msg *m)
{
	char out[sizeof(m->payload) + 8];
	int n;

	if (m->pairs == 0) {
		return;
	}
	n = nmea_build(out, sizeof(out), m->payload);
	if (n > 0) {
		LOG_INF("-> glider %s", m->payload);
		port_write(PORT_GLIDER, out, (size_t)n);
	}
}

/* ---- $SD parsing ---- */

struct sd_update {
	int64_t now;
	bool nav;
};

static enum glider_phase map_phase(int v)
{
	switch (v) {
	case 0:
		return PHASE_SURFACE;
	case 1:
		return PHASE_DIVE;
	case 2:
		return PHASE_CLIMB;
	case 3:
		return PHASE_HOVER;
	default:
		return PHASE_UNKNOWN;
	}
}

static void on_sd_pair(int idx, const char *value, void *user)
{
	struct sd_update *u = user;
	char *end;
	double v = strtod(value, &end);

	if (end == value) {
		bad_values++;
		return;
	}
	if (idx == CONFIG_SMART_SLOCUM_IDX_TIME) {
		if (v >= MIN_VALID_EPOCH) {
			nav_set_time((int64_t)v, u->now);
			u->nav = true;
		}
	} else if (idx == CONFIG_SMART_SLOCUM_IDX_DEPTH) {
		nav_set_depth(v, u->now);
		u->nav = true;
	} else if (idx == CONFIG_SMART_SLOCUM_IDX_PHASE) {
		nav_set_phase(map_phase((int)v));
		u->nav = true;
	} else if (idx == CONFIG_SMART_SLOCUM_IDX_IN_MODE) {
		policy_set_remote_mode((int)v);
		u->nav = true;
	} else if (idx == CONFIG_SMART_SLOCUM_IDX_IN_DEPTH_MIN) {
		policy_set_remote_depth_min(v);
		u->nav = true;
	} else if (idx == CONFIG_SMART_SLOCUM_IDX_IN_DEPTH_MAX) {
		policy_set_remote_depth_max(v);
		u->nav = true;
	}
}

static void set_active(bool on)
{
	if (on != active) {
		active = on;
		ev_post(on ? EV_GLIDER_ACTIVE : EV_GLIDER_INACTIVE, 0);
	}
}

void glider_on_line(const char *line, int64_t now_ms)
{
	struct nmea_frame f;

#ifdef CONFIG_SMART_PASSTHROUGH
	if (str_keyword(line, CONFIG_SMART_PASSTHROUGH_ENTER, NULL)) {
		ev_post(EV_PASSTHROUGH_ENTER, 0);
		return;
	}
#endif
	if (nmea_parse(line, &f) != 0) {
		LOG_DBG("ignored glider line: %s", line);
		return;
	}
	if (f.cs == NMEA_CS_BAD ||
	    (f.cs == NMEA_CS_ABSENT && IS_ENABLED(CONFIG_SMART_SLOCUM_REQUIRE_CHECKSUM))) {
		cs_errors++;
		LOG_WRN("glider checksum %s: %s", f.cs == NMEA_CS_BAD ? "bad" : "missing", line);
		return;
	}
	link_seen = true;

	if (nmea_type_is(&f, "HI")) {
		LOG_INF("glider $HI");
		set_active(true);
	} else if (nmea_type_is(&f, "BY")) {
		LOG_INF("glider $BY");
		set_active(false);
	} else if (nmea_type_is(&f, "SD")) {
		struct sd_update u = {.now = now_ms};
		int bad;

		if (IS_ENABLED(CONFIG_SMART_SLOCUM_SD_ACTIVATES)) {
			set_active(true);
		}
		bad = nmea_for_each_pair(f.fields, f.fields_len, on_sd_pair, &u);
		if (bad) {
			bad_values += (uint32_t)bad;
			LOG_WRN("%d malformed SD fields: %s", bad, line);
		}
		if (u.nav) {
			ev_post(EV_NAV, 0);
		}
	} else if (nmea_type_is(&f, "ER")) {
		LOG_WRN("glider reports error: %s", line);
	} else {
		LOG_DBG("unhandled glider message: %s", line);
	}
}

/* ---- controller notifications ---- */

static void send_health(uint32_t health)
{
	struct sw_msg m;

	sw_init(&m);
	sw_add(&m, CONFIG_SMART_SLOCUM_IDX_OUT_HEALTH, health, 0);
	sw_send(&m);
}

void glider_on_boot_done(uint32_t health)
{
	/* Nothing is sent before $HI: the proglet is not listening yet */
	ARG_UNUSED(health);
}

void glider_on_session_start(uint32_t health)
{
	send_health(health);
}

void glider_on_cmd_done(enum glider_cmd cmd, bool ok)
{
	ARG_UNUSED(cmd);
	ARG_UNUSED(ok);
}

void glider_on_sensor_started(const struct segment_summary *seg)
{
	struct sw_msg m;

	sw_init(&m);
	sw_add(&m, CONFIG_SMART_SLOCUM_IDX_OUT_TIME_VALID, seg->started_with_time ? 1 : -1, 0);
	sw_send(&m);
}

void glider_on_segment_end(const struct segment_summary *seg, uint32_t health)
{
	struct sw_msg m;

	sw_init(&m);
	sw_add(&m, CONFIG_SMART_SLOCUM_IDX_OUT_HEALTH, health, 0);
	sw_add(&m, CONFIG_SMART_SLOCUM_IDX_OUT_COUNT, seg->records, 0);
	if (seg->depth_valid) {
		sw_add(&m, CONFIG_SMART_SLOCUM_IDX_OUT_MAX_DEPTH, seg->max_depth, 2);
		sw_add(&m, CONFIG_SMART_SLOCUM_IDX_OUT_LAST_DEPTH, seg->last_depth, 2);
	}
	sw_send(&m);
}

void glider_on_start_failed(uint32_t health)
{
	send_health(health);
}

void glider_on_passthrough(bool entered)
{
	port_puts(PORT_GLIDER, entered ? "PASSTHROUGH_ACTIVE\r\n" : "PASSTHROUGH_END\r\n");
}
