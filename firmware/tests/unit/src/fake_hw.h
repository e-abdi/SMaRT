/* Test doubles for the serial ports and board I/O, plus scenario helpers. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/port.h"

extern int64_t t_now;

void fake_reset(void);
const char *fake_tx(enum smart_port port);
void fake_tx_clear(void);
bool fake_sensor_powered(void);
int fake_count(enum smart_port port, const char *needle);

/* Scenario helpers: send text into a port, advance time in 10 ms steps */
void glider_says(const char *text);
void glider_nmea(const char *payload);
void sensor_says(const char *text);
void advance_ms(int ms);
/* The exact NMEA frame for payload, e.g. "SW,0:3" -> "$SW,0:3*0F\r\n" */
const char *nmea_of(const char *payload);
