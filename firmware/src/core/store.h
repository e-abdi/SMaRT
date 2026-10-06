/*
 * RAM store for processed data waiting to be downloaded by the glider.
 * Each line is tagged with a partition (0 = any, 1 = dive, 2 = climb) so a
 * Seaglider can download dive and climb data separately.
 *
 * Contents are lost when the board loses power.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STORE_PARTS 3

typedef void (*store_write_fn)(const char *data, size_t len, void *user);

void store_init(void);
/* Append one line (a '\n' is added). Returns 0 or -ENOSPC (line dropped). */
int store_append(int part, const char *line);
void store_clear(int part);
size_t store_used(void);
uint32_t store_dropped(void);

/* Write every line of a partition (or all of them for part < 0). */
void store_dump(int part, store_write_fn write, void *user);

/* Track which partitions were downloaded; cleared lazily on the next start. */
void store_mark_downloaded(int part);
bool store_downloaded(int part);
