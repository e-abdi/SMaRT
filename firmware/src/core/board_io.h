/* Board-level outputs other than the serial ports. */
#pragma once

#include <stdbool.h>

int board_io_init(void);
void sensor_power_set(bool on);
void board_watchdog_feed(void);
