/*
 * End-to-end scenarios for the uvp6_slocum profile: scripted Backseat Driver
 * traffic in, exact bytes to the glider and to the UVP6 checked.
 */
#ifdef CONFIG_SMART_GLIDER_SLOCUM

#include <string.h>
#include <zephyr/ztest.h>

#include "core/app.h"
#include "core/controller.h"
#include "core/nav.h"
#include "fake_hw.h"

#define START_T0   "$start:ACQ_CSCS_002H,20090213,233130;\n"
#define START_NOT  "$start:ACQ_CSCS_002H;\n"
#define STOP       "$stop;\n"
#define LPM(depth) "LPM_DATA," depth ",20090213,233131,10,4.5,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5;\n"

static void before(void *f)
{
	ARG_UNUSED(f);
	fake_reset();
	app_init(0);
}

ZTEST_SUITE(slocum, NULL, NULL, before, NULL, NULL);

static bool glider_got(const char *payload)
{
	return strstr(fake_tx(PORT_GLIDER), nmea_of(payload)) != NULL;
}

static void boot(void)
{
	sensor_says("HW_CONF,000001,UVP6\n");
	zassert_equal(controller_state(), ST_IDLE);
}

static void start_sampling(void)
{
	boot();
	glider_nmea("HI");
	glider_nmea("SD,4:1234567890,5:1.50,6:1");
	zassert_equal(controller_state(), ST_STARTING);
	sensor_says("$startack;\n");
	zassert_equal(controller_state(), ST_SAMPLING);
}

ZTEST(slocum, test_full_dive_cycle)
{
	boot();
	zassert_true(fake_sensor_powered());

	glider_nmea("HI");
	zassert_true(glider_got("SW,0:3"), "health on session start");
	zassert_equal(controller_state(), ST_ARMED, "waits for glider time");

	glider_nmea("SD,4:1234567890,5:1.50,6:1");
	zassert_equal(controller_state(), ST_STARTING);
	zassert_not_null(strstr(fake_tx(PORT_SENSOR), START_T0));

	sensor_says("$startack;\n");
	zassert_equal(controller_state(), ST_SAMPLING);
	zassert_true(glider_got("SW,1:1"), "started with time");

	sensor_says(LPM("10.50"));
	sensor_says(LPM("25.25"));
	sensor_says(LPM("20.00"));

	/* Dive -> climb: stop, report the leg, restart after the holdoff */
	fake_tx_clear();
	glider_nmea("SD,5:30.0,6:2");
	zassert_equal(controller_state(), ST_STOPPING);
	zassert_str_equal(fake_tx(PORT_SENSOR), STOP);
	sensor_says("$stopack;\n");
	zassert_equal(controller_state(), ST_HOLDOFF);
	zassert_true(glider_got("SW,0:11,2:3,3:25.25"), "got: %s", fake_tx(PORT_GLIDER));

	fake_tx_clear();
	advance_ms(2000);
	zassert_equal(controller_state(), ST_STARTING);
	/* glider time extrapolated by 2 s */
	zassert_not_null(strstr(fake_tx(PORT_SENSOR), "$start:ACQ_CSCS_002H,20090213,233132;\n"));
	sensor_says("$startack;\n");
	zassert_equal(controller_state(), ST_SAMPLING);

	/* End of the extctl sample state */
	glider_nmea("BY");
	zassert_equal(controller_state(), ST_STOPPING);
	sensor_says("$stopack;\n");
	zassert_equal(controller_state(), ST_IDLE);
}

ZTEST(slocum, test_starts_without_time_after_wait)
{
	boot();
	glider_nmea("HI");
	advance_ms(4990);
	zassert_equal(controller_state(), ST_ARMED);
	advance_ms(20);
	zassert_equal(controller_state(), ST_STARTING);
	zassert_str_equal(fake_tx(PORT_SENSOR), START_NOT);
	sensor_says("$startack;\n");
	zassert_true(glider_got("SW,1:-1"));
}

ZTEST(slocum, test_start_retries_then_backs_off)
{
	boot();
	glider_nmea("HI");
	glider_nmea("SD,4:1234567890,5:1.50,6:1");
	advance_ms(3000);
	zassert_equal(fake_count(PORT_SENSOR, "$start:"), 3, "1 try + 2 retries");
	zassert_equal(fake_count(PORT_SENSOR, STOP), 1, "safety stop");
	zassert_equal(controller_state(), ST_HOLDOFF);
	zassert_true(glider_got("SW,0:27"), "start-failed bit; got %s", fake_tx(PORT_GLIDER));

	fake_tx_clear();
	advance_ms(10000);
	zassert_equal(controller_state(), ST_STARTING, "retries after the backoff");
	sensor_says("LPM_DATA,5.0,x;\n"); /* data counts as a start ack */
	zassert_equal(controller_state(), ST_SAMPLING);
}

ZTEST(slocum, test_rejects_bad_checksums)
{
	boot();
	glider_says("$HI\r\n"); /* no checksum */
	zassert_equal(controller_state(), ST_IDLE);
	glider_says("$SD,4:1234567890,5:1.5,6:1*00\r\n");
	zassert_false(nav_get()->time_valid);
	zassert_true(controller_health() & BIT(6));
	zassert_equal(controller_state(), ST_IDLE);
}

ZTEST(slocum, test_sd_recovers_session_after_board_reset)
{
	boot();
	/* $HI was sent before this board rebooted; only $SD arrives */
	glider_nmea("SD,4:1234567890,5:40.0,6:1");
	zassert_equal(controller_state(), ST_STARTING);
}

ZTEST(slocum, test_stops_unattended_sensor)
{
	boot();
	sensor_says(LPM("10.0"));
	sensor_says(LPM("11.0"));
	zassert_equal(fake_count(PORT_SENSOR, STOP), 1);
}

ZTEST(slocum, test_restarts_after_sensor_reboot)
{
	start_sampling();
	fake_tx_clear();
	sensor_says("HW_CONF,000001,UVP6\n");
	zassert_equal(controller_state(), ST_STOPPING);
	sensor_says("$stopack;\n");
	advance_ms(2000);
	zassert_equal(controller_state(), ST_STARTING);
	zassert_equal(fake_count(PORT_SENSOR, "$start:"), 1);
}

ZTEST(slocum, test_stop_without_ack_gives_up)
{
	start_sampling();
	fake_tx_clear();
	glider_nmea("BY");
	advance_ms(4000);
	zassert_equal(fake_count(PORT_SENSOR, STOP), 4, "1 try + 3 retries");
	zassert_equal(controller_state(), ST_IDLE);
}

ZTEST(slocum, test_passthrough)
{
	boot();
	glider_says("$MIRROR\r\n");
	zassert_equal(controller_state(), ST_PASSTHROUGH);
	fake_tx_clear();
	glider_says("$hello;\n");
	sensor_says("$hi back;\n");
	zassert_str_equal(fake_tx(PORT_SENSOR), "$hello;\n");
	zassert_str_equal(fake_tx(PORT_GLIDER), "$hi back;\n");
	glider_says("$QUIT\r\n");
	zassert_equal(controller_state(), ST_IDLE);
}

ZTEST(slocum, test_passthrough_times_out)
{
	boot();
	glider_says("$MIRROR\r\n");
	advance_ms(CONFIG_SMART_PASSTHROUGH_TIMEOUT_S * 1000 + 10);
	zassert_equal(controller_state(), ST_IDLE);
}

#endif /* CONFIG_SMART_GLIDER_SLOCUM */
