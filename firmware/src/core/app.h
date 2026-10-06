/*
 * Hardware-independent application core: routes bytes from the two ports
 * into the glider adapter, the sensor and the controller. main.c feeds it
 * from the UARTs; the unit tests feed it scripted traffic.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "core/port.h"

void app_init(int64_t now_ms);
void app_feed(enum smart_port port, const uint8_t *data, size_t len, int64_t now_ms);
/* Handle timeouts; call regularly and at least by app_next_deadline(). */
void app_poll(int64_t now_ms);
int64_t app_next_deadline(void);
