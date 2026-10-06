#include "sensor/sensor.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/logging/log.h>

#include "core/events.h"
#include "core/port.h"
#include "proc/proc.h"

LOG_MODULE_DECLARE(smart, CONFIG_SMART_LOG_LEVEL);

static const char ready_token[] = CONFIG_SMART_SENSOR_READY_TOKEN;
static const char start_cmd[] = CONFIG_SMART_SENSOR_START_CMD;
static const char start_cmd_notime[] = CONFIG_SMART_SENSOR_START_CMD_NOTIME;
static const char stop_cmd[] = CONFIG_SMART_SENSOR_STOP_CMD;
static const char start_ack[] = CONFIG_SMART_SENSOR_START_ACK;
static const char stop_ack[] = CONFIG_SMART_SENSOR_STOP_ACK;
static const char data_prefix[] = CONFIG_SMART_SENSOR_DATA_PREFIX;

void sensor_init(void)
{
	if (start_cmd[0] == '\0' && start_cmd_notime[0] == '\0') {
		LOG_WRN("no sensor start command configured");
	}
}

static bool parse_depth(const char *line, double *depth)
{
	int want = CONFIG_SMART_SENSOR_DATA_DEPTH_FIELD;
	const char *p = line;

	if (want < 0) {
		return false;
	}
	for (int i = 0; i < want; i++) {
		p = strchr(p, ',');
		if (p == NULL) {
			return false;
		}
		p++;
	}

	char *end;
	double d = strtod(p, &end);

	if (end == p || (*end != ',' && *end != ';' && *end != '\0' && *end != ' ')) {
		return false;
	}
	*depth = d;
	return true;
}

void sensor_on_line(const char *line, int64_t now_ms)
{
	ARG_UNUSED(now_ms);

	if (ready_token[0] && strstr(line, ready_token)) {
		LOG_INF("%s ready: %s", CONFIG_SMART_SENSOR_NAME, line);
		ev_post(EV_SENSOR_READY, 0);
	}
	if (start_ack[0] && strstr(line, start_ack)) {
		ev_post(EV_SENSOR_START_ACK, 0);
	}
	if (stop_ack[0] && strstr(line, stop_ack)) {
		ev_post(EV_SENSOR_STOP_ACK, 0);
	}
	if (data_prefix[0] && strncmp(line, data_prefix, sizeof(data_prefix) - 1) == 0) {
		struct proc_record rec = {.line = line};

		rec.has_depth = parse_depth(line, &rec.depth);
		proc_on_record(&rec);
		ev_post(EV_SENSOR_DATA, 0);
	}
}

bool sensor_can_start(bool have_time)
{
	if (have_time) {
		return start_cmd[0] || start_cmd_notime[0];
	}
	return start_cmd_notime[0] || (start_cmd[0] && !tmpl_needs_time(start_cmd));
}

static int send_template(const char *tmpl, const struct tmpl_ctx *ctx)
{
	char out[CONFIG_SMART_LINE_MAX];
	int n = tmpl_expand(tmpl, ctx, out, sizeof(out));

	if (n < 0) {
		LOG_ERR("cannot expand command template (%d)", n);
		return n;
	}
	port_write(PORT_SENSOR, out, (size_t)n);
	return 0;
}

int sensor_send_start(const struct tmpl_ctx *ctx, bool *with_time)
{
	const char *tmpl;

	if (ctx->have_time && start_cmd[0]) {
		tmpl = start_cmd;
	} else if (start_cmd_notime[0]) {
		tmpl = start_cmd_notime;
	} else if (start_cmd[0] && !tmpl_needs_time(start_cmd)) {
		tmpl = start_cmd;
	} else {
		return -ENODATA;
	}
	*with_time = tmpl_needs_time(tmpl);
	LOG_INF("-> sensor start (%s time)", *with_time ? "with" : "without");
	return send_template(tmpl, ctx);
}

int sensor_send_stop(const struct tmpl_ctx *ctx)
{
	if (stop_cmd[0] == '\0') {
		return -ENODATA;
	}
	LOG_INF("-> sensor stop");
	return send_template(stop_cmd, ctx);
}

bool sensor_expects_start_ack(void)
{
	return start_ack[0] != '\0';
}

bool sensor_expects_stop_ack(void)
{
	return stop_ack[0] != '\0';
}
