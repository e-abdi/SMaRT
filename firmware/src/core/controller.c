#include "core/controller.h"

#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/smf.h>
#include <zephyr/sys/util.h>

#include "core/board_io.h"
#include "core/events.h"
#include "core/nav.h"
#include "core/port.h"
#include "glider/glider.h"
#include "proc/proc.h"
#include "sensor/sensor.h"

LOG_MODULE_DECLARE(smart, CONFIG_SMART_LOG_LEVEL);

#define NO_DEADLINE INT64_MAX
/* Internal event: re-evaluate the new state after a transition */
#define EV_EVAL     EV_COUNT

static const struct smf_state states[ST_COUNT];

static struct {
	struct smf_ctx smf; /* must be first */
	struct smart_ev ev;
	int64_t now;
	int64_t deadline;
	enum ctrl_state cur;
	bool changed;

	bool boot_done;
	bool powered;
	bool glider_active;
	int64_t session_ms;  /* uptime when the glider session opened */
	bool session_reported;
	bool cmd_start;      /* glider-commanded sampling is on */
	bool cmd_stop_reply; /* glider waits for the stop to complete */
	bool sensor_ready;
	bool start_failed;
	bool stop_failed;
	bool start_error;    /* start command could not be sent */
	bool hold_fail;      /* HOLDOFF after a failed start */
	bool orphan_stop_sent;
	int attempts;
	uint32_t segment;
	bool seg_with_time;
	enum glider_phase seg_phase;
} c;

static struct {
	bool mode_set;
	int mode;
	bool dmin_set;
	double dmin;
	bool dmax_set;
	double dmax;
} remote;

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static void go(enum ctrl_state st)
{
	LOG_INF("state %s -> %s", controller_state_name(c.cur), controller_state_name(st));
	c.changed = true;
	smf_set_state(SMF_CTX(&c), &states[st]);
}

static void set_deadline(int32_t ms)
{
	c.deadline = c.now + ms;
}

static void clear_deadline(void)
{
	c.deadline = NO_DEADLINE;
}

static void make_ctx(struct tmpl_ctx *t)
{
	const struct nav_state *n = nav_get();

	t->have_time = nav_time_now(c.now, &t->epoch);
	if (!t->have_time) {
		t->epoch = 0;
	}
	t->have_depth = n->depth_valid;
	t->depth = n->depth;
	t->cast = n->phase == PHASE_DIVE ? 1 : n->phase == PHASE_CLIMB ? 2 : 0;
	t->segment = c.segment;
}

static bool phase_allowed(enum glider_phase p)
{
	if (remote.mode_set) {
		switch (remote.mode) {
		case 1:
			return p == PHASE_DIVE;
		case 2:
			return p == PHASE_CLIMB;
		case 3:
			return p == PHASE_HOVER;
		case 4:
			return p == PHASE_DIVE || p == PHASE_CLIMB;
		case 5:
			return true;
		default:
			return false;
		}
	}
	switch (p) {
	case PHASE_DIVE:
		return IS_ENABLED(CONFIG_SMART_POLICY_PHASE_DIVE);
	case PHASE_CLIMB:
		return IS_ENABLED(CONFIG_SMART_POLICY_PHASE_CLIMB);
	case PHASE_HOVER:
		return IS_ENABLED(CONFIG_SMART_POLICY_PHASE_HOVER);
	case PHASE_SURFACE:
		return IS_ENABLED(CONFIG_SMART_POLICY_PHASE_SURFACE);
	default:
		return IS_ENABLED(CONFIG_SMART_POLICY_PHASE_UNKNOWN);
	}
}

static bool depth_ok(bool sampling)
{
	const struct nav_state *n = nav_get();
	double dmin = remote.dmin_set ? remote.dmin : CONFIG_SMART_POLICY_DEPTH_MIN_M;
	double dmax = remote.dmax_set ? remote.dmax : CONFIG_SMART_POLICY_DEPTH_MAX_M;
	/* While sampling, widen the window so noise does not cause flapping */
	double hyst = sampling ? CONFIG_SMART_POLICY_DEPTH_HYST_M : 0;

	if (dmin <= 0 && dmax <= 0) {
		return true;
	}
	if (!n->depth_valid) {
		return false;
	}
	if (dmin > 0 && n->depth < dmin - hyst) {
		return false;
	}
	if (dmax > 0 && n->depth > dmax + hyst) {
		return false;
	}
	return true;
}

/* The sampling policy: should the sensor be running right now? */
static bool want_sampling(bool sampling)
{
	const struct nav_state *n = nav_get();

	if (!c.boot_done || !c.glider_active) {
		return false;
	}
	if (glider_commands_sampling() && !c.cmd_start) {
		return false;
	}
	if (!phase_allowed(n->phase) || !depth_ok(sampling)) {
		return false;
	}
	if (sampling) {
		return true;
	}
	if (!n->time_valid) {
		if (IS_ENABLED(CONFIG_SMART_POLICY_REQUIRE_TIME) ||
		    c.now < c.session_ms + CONFIG_SMART_POLICY_TIME_WAIT_MS) {
			return false;
		}
	}
	return sensor_can_start(n->time_valid);
}

static struct segment_summary current_segment(void)
{
	return (struct segment_summary){
		.segment = c.segment,
		.phase = c.seg_phase,
		.started_with_time = c.seg_with_time,
	};
}

/* ------------------------------------------------------------------ */
/* States                                                               */
/* ------------------------------------------------------------------ */

static enum smf_state_result root_run(void *o)
{
	ARG_UNUSED(o);

	/*
	 * Data while we believe the sensor is idle means it is running
	 * unattended (e.g. the board rebooted mid-dive). Stop it once; the
	 * policy restarts it when wanted.
	 */
	if (c.ev.type == EV_SENSOR_DATA && !c.orphan_stop_sent &&
	    (c.cur == ST_IDLE || c.cur == ST_ARMED || c.cur == ST_HOLDOFF)) {
		struct tmpl_ctx t;

		LOG_WRN("sensor data while idle, sending stop");
		make_ctx(&t);
		(void)sensor_send_stop(&t);
		c.orphan_stop_sent = true;
	}
	return SMF_EVENT_HANDLED;
}

static void boot_finish(void)
{
	if (!c.sensor_ready && CONFIG_SMART_SENSOR_READY_TOKEN[0] != '\0') {
		LOG_WRN("no power-up banner from %s", CONFIG_SMART_SENSOR_NAME);
	}
	c.boot_done = true;
	glider_on_boot_done(controller_health());
	go(ST_IDLE);
}

static void boot_entry(void *o)
{
	ARG_UNUSED(o);
	c.cur = ST_BOOT;
	c.boot_done = false;
	sensor_power_set(true);
	c.powered = true;
	set_deadline(CONFIG_SMART_SENSOR_BOOT_TIMEOUT_MS);
}

static enum smf_state_result boot_run(void *o)
{
	ARG_UNUSED(o);

	if (c.ev.type == EV_SENSOR_READY || c.ev.type == EV_TIMEOUT) {
		boot_finish();
	}
	return SMF_EVENT_HANDLED;
}

static void idle_entry(void *o)
{
	ARG_UNUSED(o);
	c.cur = ST_IDLE;
	c.orphan_stop_sent = false;
	clear_deadline();
	if (IS_ENABLED(CONFIG_SMART_SENSOR_POWER_OFF_WHEN_IDLE) && !c.glider_active) {
		sensor_power_set(false);
		c.powered = false;
		c.sensor_ready = false;
	}
}

static enum smf_state_result idle_run(void *o)
{
	ARG_UNUSED(o);

	if (c.ev.type == EV_PASSTHROUGH_ENTER) {
		go(ST_PASSTHROUGH);
	} else if (c.glider_active) {
		go(c.powered ? ST_ACTIVE : ST_BOOT);
	} else {
		return SMF_EVENT_PROPAGATE;
	}
	return SMF_EVENT_HANDLED;
}

static void passthrough_entry(void *o)
{
	ARG_UNUSED(o);
	c.cur = ST_PASSTHROUGH;
	set_deadline(CONFIG_SMART_PASSTHROUGH_TIMEOUT_S * 1000);
	glider_on_passthrough(true);
}

static enum smf_state_result passthrough_run(void *o)
{
	ARG_UNUSED(o);

	if (c.ev.type == EV_PASSTHROUGH_EXIT || c.ev.type == EV_TIMEOUT) {
		go(ST_IDLE);
	}
	return SMF_EVENT_HANDLED;
}

static void passthrough_exit(void *o)
{
	ARG_UNUSED(o);
	glider_on_passthrough(false);
}

static void active_entry(void *o)
{
	ARG_UNUSED(o);
	if (!c.session_reported) {
		c.session_reported = true;
		glider_on_session_start(controller_health());
	}
}

static enum smf_state_result active_run(void *o)
{
	ARG_UNUSED(o);

	if (!c.glider_active) {
		go(ST_IDLE);
		return SMF_EVENT_HANDLED;
	}
	return SMF_EVENT_PROPAGATE;
}

static void armed_entry(void *o)
{
	ARG_UNUSED(o);
	c.cur = ST_ARMED;
	clear_deadline();
	/* Re-evaluate when the wait for the glider time runs out */
	if (!nav_get()->time_valid && c.now < c.session_ms + CONFIG_SMART_POLICY_TIME_WAIT_MS) {
		c.deadline = c.session_ms + CONFIG_SMART_POLICY_TIME_WAIT_MS;
	}
}

static enum smf_state_result armed_run(void *o)
{
	ARG_UNUSED(o);

	if (c.ev.type == EV_PASSTHROUGH_ENTER) {
		go(ST_PASSTHROUGH);
	} else if (want_sampling(false)) {
		go(ST_STARTING);
	} else {
		return SMF_EVENT_PROPAGATE;
	}
	return SMF_EVENT_HANDLED;
}

static void send_start(void)
{
	struct tmpl_ctx t;
	bool with_time = false;

	make_ctx(&t);
	c.start_error = sensor_send_start(&t, &with_time) != 0;
	if (c.attempts == 0) {
		c.seg_with_time = with_time;
		proc_segment_begin(c.segment, c.seg_phase, with_time, t.have_time ? t.epoch : 0);
	}
	if (c.start_error || !sensor_expects_start_ack()) {
		set_deadline(0);
	} else {
		set_deadline(CONFIG_SMART_SENSOR_ACK_TIMEOUT_MS);
	}
}

static void starting_entry(void *o)
{
	ARG_UNUSED(o);
	c.cur = ST_STARTING;
	c.segment++;
	c.attempts = 0;
	c.orphan_stop_sent = false;
	c.seg_phase = nav_get()->phase;
	send_start();
}

static enum smf_state_result starting_run(void *o)
{
	ARG_UNUSED(o);

	switch (c.ev.type) {
	case EV_SENSOR_START_ACK:
	case EV_SENSOR_DATA:
		go(ST_SAMPLING);
		return SMF_EVENT_HANDLED;
	case EV_SENSOR_READY:
		/* Sensor (re)booted after our start: it was lost, send again */
		LOG_WRN("sensor rebooted while starting");
		send_start();
		return SMF_EVENT_HANDLED;
	case EV_TIMEOUT:
		if (!c.start_error && !sensor_expects_start_ack()) {
			go(ST_SAMPLING);
		} else if (!c.start_error && c.attempts < CONFIG_SMART_SENSOR_START_RETRIES) {
			c.attempts++;
			LOG_WRN("no start ack, retry %d", c.attempts);
			send_start();
		} else {
			struct segment_summary sum;
			struct tmpl_ctx t;

			LOG_ERR("%s did not start", CONFIG_SMART_SENSOR_NAME);
			c.start_failed = true;
			/* In case it did start and only the ack was lost */
			make_ctx(&t);
			(void)sensor_send_stop(&t);
			(void)proc_segment_end(&sum);
			glider_on_start_failed(controller_health());
			c.hold_fail = true;
			go(ST_HOLDOFF);
		}
		return SMF_EVENT_HANDLED;
	default:
		break;
	}
	if (!want_sampling(true)) {
		go(ST_STOPPING);
	}
	return SMF_EVENT_HANDLED;
}

static void sampling_entry(void *o)
{
	ARG_UNUSED(o);
	struct segment_summary seg = current_segment();

	c.cur = ST_SAMPLING;
	c.start_failed = false;
	clear_deadline();
	LOG_INF("sampling, segment %u (%s)", c.segment, phase_name(c.seg_phase));
	glider_on_sensor_started(&seg);
}

static enum smf_state_result sampling_run(void *o)
{
	ARG_UNUSED(o);
	enum glider_phase phase = nav_get()->phase;

	if (!want_sampling(true)) {
		go(ST_STOPPING);
	} else if (c.ev.type == EV_SENSOR_READY &&
		   IS_ENABLED(CONFIG_SMART_SENSOR_RESTART_ON_READY)) {
		LOG_WRN("sensor rebooted while sampling, restarting");
		go(ST_STOPPING);
	} else if (c.ev.type == EV_NAV && phase != c.seg_phase && phase != PHASE_UNKNOWN) {
		if (c.seg_phase == PHASE_UNKNOWN) {
			c.seg_phase = phase;
		} else if (IS_ENABLED(CONFIG_SMART_POLICY_RESTART_ON_PHASE_CHANGE)) {
			LOG_INF("phase %s -> %s, new segment", phase_name(c.seg_phase),
				phase_name(phase));
			go(ST_STOPPING);
		}
	}
	return SMF_EVENT_HANDLED;
}

static void send_stop(void)
{
	struct tmpl_ctx t;
	int rc;

	make_ctx(&t);
	rc = sensor_send_stop(&t);
	if (rc != 0 || !sensor_expects_stop_ack()) {
		set_deadline(0);
	} else {
		set_deadline(CONFIG_SMART_SENSOR_ACK_TIMEOUT_MS);
	}
}

static void stop_finish(bool ok)
{
	struct segment_summary sum;

	c.stop_failed = !ok;
	if (!ok) {
		LOG_ERR("%s did not acknowledge stop", CONFIG_SMART_SENSOR_NAME);
	}
	if (proc_segment_end(&sum)) {
		LOG_INF("segment %u: %u records", sum.segment, sum.records);
		glider_on_segment_end(&sum, controller_health());
	}
	if (c.cmd_stop_reply) {
		c.cmd_stop_reply = false;
		glider_on_cmd_done(GLIDER_CMD_STOP, ok);
	}
	c.hold_fail = false;
	go(c.glider_active ? ST_HOLDOFF : ST_IDLE);
}

static void stopping_entry(void *o)
{
	ARG_UNUSED(o);
	c.cur = ST_STOPPING;
	c.attempts = 0;
	send_stop();
}

static enum smf_state_result stopping_run(void *o)
{
	ARG_UNUSED(o);

	if (c.ev.type == EV_SENSOR_STOP_ACK) {
		stop_finish(true);
	} else if (c.ev.type == EV_TIMEOUT) {
		if (!sensor_expects_stop_ack()) {
			stop_finish(true);
		} else if (c.attempts < CONFIG_SMART_SENSOR_STOP_RETRIES) {
			c.attempts++;
			LOG_WRN("no stop ack, retry %d", c.attempts);
			send_stop();
		} else {
			stop_finish(false);
		}
	}
	return SMF_EVENT_HANDLED;
}

static void holdoff_entry(void *o)
{
	ARG_UNUSED(o);
	c.cur = ST_HOLDOFF;
	c.orphan_stop_sent = false;
	set_deadline(c.hold_fail ? CONFIG_SMART_POLICY_FAIL_BACKOFF_MS
				 : CONFIG_SMART_POLICY_RESTART_HOLDOFF_MS);
}

static enum smf_state_result holdoff_run(void *o)
{
	ARG_UNUSED(o);

	if (c.ev.type == EV_TIMEOUT) {
		go(ST_ARMED);
	} else if (c.ev.type == EV_PASSTHROUGH_ENTER) {
		go(ST_PASSTHROUGH);
	} else {
		return SMF_EVENT_PROPAGATE;
	}
	return SMF_EVENT_HANDLED;
}

/* clang-format off */
static const struct smf_state states[ST_COUNT] = {
	[ST_ROOT]        = SMF_CREATE_STATE(NULL, root_run, NULL, NULL, NULL),
	[ST_BOOT]        = SMF_CREATE_STATE(boot_entry, boot_run, NULL, &states[ST_ROOT], NULL),
	[ST_IDLE]        = SMF_CREATE_STATE(idle_entry, idle_run, NULL, &states[ST_ROOT], NULL),
	[ST_PASSTHROUGH] = SMF_CREATE_STATE(passthrough_entry, passthrough_run, passthrough_exit,
					    &states[ST_ROOT], NULL),
	[ST_ACTIVE]      = SMF_CREATE_STATE(active_entry, active_run, NULL, &states[ST_ROOT],
					    &states[ST_ARMED]),
	[ST_ARMED]       = SMF_CREATE_STATE(armed_entry, armed_run, NULL, &states[ST_ACTIVE], NULL),
	[ST_STARTING]    = SMF_CREATE_STATE(starting_entry, starting_run, NULL, &states[ST_ACTIVE],
					    NULL),
	[ST_SAMPLING]    = SMF_CREATE_STATE(sampling_entry, sampling_run, NULL, &states[ST_ACTIVE],
					    NULL),
	[ST_STOPPING]    = SMF_CREATE_STATE(stopping_entry, stopping_run, NULL, &states[ST_ACTIVE],
					    NULL),
	[ST_HOLDOFF]     = SMF_CREATE_STATE(holdoff_entry, holdoff_run, NULL, &states[ST_ACTIVE],
					    NULL),
};
/* clang-format on */

/* ------------------------------------------------------------------ */
/* Event dispatch                                                       */
/* ------------------------------------------------------------------ */

/* Update session flags before the state machine sees the event. */
static void track(const struct smart_ev *ev)
{
	switch (ev->type) {
	case EV_GLIDER_ACTIVE:
		if (!c.glider_active) {
			c.session_ms = c.now;
		}
		c.glider_active = true;
		break;
	case EV_GLIDER_INACTIVE:
		c.glider_active = false;
		c.cmd_start = false;
		c.session_reported = false;
		break;
	case EV_GLIDER_START:
		if (!c.glider_active) {
			c.session_ms = c.now;
		}
		c.glider_active = true;
		c.cmd_start = true;
		break;
	case EV_GLIDER_STOP:
		c.cmd_start = false;
		if (c.cur == ST_STARTING || c.cur == ST_SAMPLING || c.cur == ST_STOPPING) {
			c.cmd_stop_reply = true;
		} else {
			glider_on_cmd_done(GLIDER_CMD_STOP, true);
		}
		break;
	case EV_SENSOR_READY:
		c.sensor_ready = true;
		break;
	default:
		break;
	}
}

static void dispatch(enum smart_ev_type type, int32_t arg)
{
	c.ev = (struct smart_ev){.type = type, .arg = arg};
	if (type < EV_COUNT) {
		LOG_DBG("event %s in %s", ev_name(type), controller_state_name(c.cur));
		track(&c.ev);
	}
	c.changed = false;
	smf_run_state(SMF_CTX(&c));

	/* Let each newly entered state react to the current situation */
	for (int i = 0; i < 8 && c.changed; i++) {
		c.changed = false;
		c.ev = (struct smart_ev){.type = (enum smart_ev_type)EV_EVAL};
		smf_run_state(SMF_CTX(&c));
	}
}

void controller_init(int64_t now_ms)
{
	memset(&c, 0, sizeof(c));
	memset(&remote, 0, sizeof(remote));
	c.now = now_ms;
	c.cur = ST_ROOT;
	c.deadline = NO_DEADLINE;
	smf_set_initial(SMF_CTX(&c), &states[ST_BOOT]);
}

void controller_step(int64_t now_ms)
{
	struct smart_ev ev;

	c.now = now_ms;
	while (ev_get(&ev)) {
		dispatch(ev.type, ev.arg);
	}
	/* A handler may set an immediate deadline; loop until none expired */
	for (int i = 0; i < 8 && c.deadline <= c.now; i++) {
		clear_deadline();
		dispatch(EV_TIMEOUT, 0);
		while (ev_get(&ev)) {
			dispatch(ev.type, ev.arg);
		}
	}
}

int64_t controller_next_deadline(void)
{
	return c.deadline;
}

enum ctrl_state controller_state(void)
{
	return c.cur;
}

const char *controller_state_name(enum ctrl_state st)
{
	static const char *const names[] = {
		[ST_ROOT] = "ROOT",         [ST_BOOT] = "BOOT",
		[ST_IDLE] = "IDLE",         [ST_PASSTHROUGH] = "PASSTHROUGH",
		[ST_ACTIVE] = "ACTIVE",     [ST_ARMED] = "ARMED",
		[ST_STARTING] = "STARTING", [ST_SAMPLING] = "SAMPLING",
		[ST_STOPPING] = "STOPPING", [ST_HOLDOFF] = "HOLDOFF",
	};

	return st < ARRAY_SIZE(names) ? names[st] : "?";
}

bool controller_in_passthrough(void)
{
	return c.cur == ST_PASSTHROUGH;
}

void controller_passthrough_activity(int64_t now_ms)
{
	if (c.cur == ST_PASSTHROUGH) {
		c.deadline = now_ms + CONFIG_SMART_PASSTHROUGH_TIMEOUT_S * 1000;
	}
}

uint32_t controller_health(void)
{
	uint32_t h = glider_health_bits();

	if (c.sensor_ready) {
		h |= HEALTH_SENSOR_READY;
	}
	if (c.cur == ST_SAMPLING) {
		h |= HEALTH_SAMPLING;
	}
	if (nav_get()->time_valid) {
		h |= HEALTH_TIME_VALID;
	}
	if (c.start_failed) {
		h |= HEALTH_START_FAILED;
	}
	if (c.stop_failed) {
		h |= HEALTH_STOP_FAILED;
	}
	for (int p = 0; p < PORT_NUM; p++) {
		const struct port_stats *s = port_get_stats((enum smart_port)p);

		if (s->rx_dropped || s->rx_errors) {
			h |= HEALTH_LINK_ERRS;
		}
	}
	return h;
}

void policy_set_remote_mode(int mode)
{
	if (!remote.mode_set || remote.mode != mode) {
		LOG_INF("pilot sampling mode %d", mode);
	}
	remote.mode_set = true;
	remote.mode = mode;
}

void policy_set_remote_depth_min(double m)
{
	remote.dmin_set = m >= 0;
	remote.dmin = m;
}

void policy_set_remote_depth_max(double m)
{
	remote.dmax_set = m >= 0;
	remote.dmax = m;
}
