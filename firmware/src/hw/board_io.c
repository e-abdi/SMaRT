#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/logging/log.h>

#include "core/board_io.h"

LOG_MODULE_DECLARE(smart, CONFIG_SMART_LOG_LEVEL);

#define USER_NODE DT_PATH(zephyr_user)

#if DT_NODE_HAS_PROP(USER_NODE, sensor_power_gpios)
static const struct gpio_dt_spec sensor_power = GPIO_DT_SPEC_GET(USER_NODE, sensor_power_gpios);
#define HAVE_SENSOR_POWER 1
#endif

#if CONFIG_SMART_WATCHDOG_TIMEOUT_MS > 0 && DT_NODE_EXISTS(DT_ALIAS(watchdog0))
static const struct device *const wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));
static int wdt_channel = -1;
#endif

int board_io_init(void)
{
#ifdef HAVE_SENSOR_POWER
	if (!gpio_is_ready_dt(&sensor_power) ||
	    gpio_pin_configure_dt(&sensor_power, GPIO_OUTPUT_INACTIVE) != 0) {
		LOG_ERR("sensor power GPIO not available");
	}
#endif

#if CONFIG_SMART_WATCHDOG_TIMEOUT_MS > 0 && DT_NODE_EXISTS(DT_ALIAS(watchdog0))
	struct wdt_timeout_cfg cfg = {
		.window.max = CONFIG_SMART_WATCHDOG_TIMEOUT_MS,
		.flags = WDT_FLAG_RESET_SOC,
	};

	if (!device_is_ready(wdt)) {
		LOG_ERR("watchdog not ready");
		return -ENODEV;
	}
	wdt_channel = wdt_install_timeout(wdt, &cfg);
	if (wdt_channel < 0 || wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG) != 0) {
		LOG_ERR("watchdog setup failed (%d)", wdt_channel);
		wdt_channel = -1;
		return -EIO;
	}
#endif
	return 0;
}

void sensor_power_set(bool on)
{
#ifdef HAVE_SENSOR_POWER
	/* Without power control the relay is simply kept on (JP1 may be on A-C) */
	if (!IS_ENABLED(CONFIG_SMART_SENSOR_POWER_CONTROL)) {
		on = true;
	}
	gpio_pin_set_dt(&sensor_power, on ? 1 : 0);
	LOG_INF("sensor power %s", on ? "on" : "off");
#else
	ARG_UNUSED(on);
#endif
}

void board_watchdog_feed(void)
{
#if CONFIG_SMART_WATCHDOG_TIMEOUT_MS > 0 && DT_NODE_EXISTS(DT_ALIAS(watchdog0))
	if (wdt_channel >= 0) {
		wdt_feed(wdt, wdt_channel);
	}
#endif
}
