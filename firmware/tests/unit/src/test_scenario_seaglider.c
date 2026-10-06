/*
 * End-to-end scenarios for the uvp6_seaglider profile: the board acting as a
 * logdev logger device (uvp6.cnf) in front of a UVP6.
 */
#ifdef CONFIG_SMART_GLIDER_SEAGLIDER

#include <stdio.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "core/app.h"
#include "core/controller.h"
#include "fake_hw.h"

#define PROMPT "UV>\r"

static void before(void *f)
{
	ARG_UNUSED(f);
	fake_reset();
	app_init(0);
}

ZTEST_SUITE(seaglider, NULL, NULL, before, NULL, NULL);

static int prompts(void)
{
	return fake_count(PORT_GLIDER, PROMPT);
}

static void lpm(int i)
{
	char line[200];

	snprintf(line, sizeof(line),
		 "LPM_DATA,%d.0,20260601,1200%02d,10,4.5,"
		 "1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,"
		 "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0;\n",
		 i, i);
	sensor_says(line);
}

ZTEST(seaglider, test_dive_and_download)
{
	sensor_says("HW_CONF,000001,UVP6\n");
	zassert_equal(prompts(), 1, "prompt for powerup-timeout");

	glider_says("\r");
	zassert_equal(prompts(), 2, "wakeup answered");

	glider_says("START 20260601,120000,1\r");
	zassert_equal(prompts(), 3, "start answered at once");
	zassert_not_null(strstr(fake_tx(PORT_SENSOR), "$start:ACQ_CSCS_022H,20260601,120000;\n"));
	sensor_says("$startack;\n");
	zassert_equal(controller_state(), ST_SAMPLING);

	glider_says("DEPTH:12.5\r");
	zassert_equal(prompts(), 3, "status= gets no reply");

	for (int i = 0; i <= 10; i++) {
		lpm(i);
	}

	glider_says("STOP\r");
	zassert_equal(controller_state(), ST_STOPPING);
	zassert_equal(prompts(), 3, "prompt only once stopped");
	sensor_says("$stopack;\n");
	zassert_equal(prompts(), 4);

	fake_tx_clear();
	glider_says("SEND_TXT_FILE 1\r");
	const char *out = fake_tx(PORT_GLIDER);

	zassert_not_null(strstr(out, "LPM,0.0,20260601,120000,4,8,12,16,10\r\n"), "got %s", out);
	zassert_not_null(strstr(out, "LPM,10.0,20260601,120010,4,8,12,16,10\r\n"));
	zassert_equal(fake_count(PORT_GLIDER, "LPM,"), 2, "decimated 1 in 10");
	zassert_not_null(strstr(out, "#SEG,1,dive,11,10.00,10.00,"));
	zassert_equal(strcmp(out + strlen(out) - strlen(PROMPT), PROMPT), 0, "ends with prompt");

	/* Climb cast: the dive data stays until a new dive cast starts. The
	 * sensor is restarted once the stop/start holdoff has passed.
	 */
	glider_says("START 20260601,130000,2\r");
	zassert_equal(controller_state(), ST_HOLDOFF);
	advance_ms(CONFIG_SMART_POLICY_RESTART_HOLDOFF_MS);
	zassert_equal(controller_state(), ST_STARTING);
	sensor_says("$startack;\n");
	lpm(1);
	glider_says("STOP\r");
	sensor_says("$stopack;\n");
	fake_tx_clear();
	glider_says("SEND_TXT_FILE 2\r");
	zassert_equal(fake_count(PORT_GLIDER, "LPM,"), 1);
	zassert_not_null(strstr(fake_tx(PORT_GLIDER), "#SEG,2,climb,1,"));
}

ZTEST(seaglider, test_stop_when_idle_answers_at_once)
{
	sensor_says("HW_CONF\n");
	glider_says("STOP\r");
	zassert_equal(prompts(), 2);
	zassert_equal(fake_count(PORT_SENSOR, "$stop;"), 0);
}

ZTEST(seaglider, test_unknown_command)
{
	sensor_says("HW_CONF\n");
	glider_says("FOO\r");
	zassert_not_null(strstr(fake_tx(PORT_GLIDER), "?\r\n" PROMPT));
}

ZTEST(seaglider, test_no_banner_still_answers)
{
	/* Sensor already powered or silent: prompt after the boot timeout */
	advance_ms(CONFIG_SMART_SENSOR_BOOT_TIMEOUT_MS + 10);
	zassert_equal(prompts(), 1);
	glider_says("START 20260601,120000,1\r");
	zassert_equal(controller_state(), ST_STARTING);
}

#endif /* CONFIG_SMART_GLIDER_SEAGLIDER */
