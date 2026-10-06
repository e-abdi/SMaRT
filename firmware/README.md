# SMaRT firmware

Zephyr firmware for the SMaRT board (RP2040 / Raspberry Pi Pico). It sits between a glider and a
sensor, and decides when the sensor runs. It works with both glider types:

| Glider | Glider-side interface | What the board does |
|---|---|---|
| Slocum G3 | Backseat Driver, `extctl` proglet | Reads `$SD` (time, depth, depth state), decides itself when to start and stop the sensor, reports back with `$SW` |
| Seaglider | `logdev` logger device (`.cnf` file) | Acts as a logger device: follows logdev `START`/`STOP`, keeps processed data, hands it over on download |

Each integration (a glider type plus a sensor) is a **profile**: one folder under `profiles/` holding
the board settings (`smart.conf`) next to the glider-side file (`extctl.ini` or `.cnf`). Most sensors
need no code at all, only a profile.

## Build, flash, test

```bash
source ~/zephyrproject/.venv/bin/activate
cd SMaRT

# Board image for a profile
west build -p -b rpi_pico firmware -- -DSMART_PROFILE=uvp6_slocum
# flash build/zephyr/zephyr.uf2 (BOOTSEL drag-and-drop) or: west flash

# Host tests, run against the real profile
west build -p -b native_sim/native/64 firmware/tests/unit -d build-test -- -DSMART_PROFILE=uvp6_slocum
build-test/zephyr/zephyr.exe
```

Debug output goes to SEGGER RTT through the SWD header (J8). It never touches the glider or sensor
UART. The board logs every state change, every command sent, and a status line once a minute.

### Bench test without a glider

`tools/glider_sim.py` plays a dive over a USB-serial adapter wired to the glider connector:

```bash
python3 firmware/tools/glider_sim.py slocum /dev/ttyUSB0 --baud 38400 --depth 30
python3 firmware/tools/glider_sim.py seaglider /dev/ttyUSB0 --baud 115200 --depth 30
```

## Profiles shipped

| Profile | Glider | Sensor | Behaviour |
|---|---|---|---|
| `uvp6_slocum` | Slocum | UVP6 | Port of `legacy/uvp6_slocum_v1`: one UVP6 acquisition per dive/climb/hover leg, LPM count and max depth reported per leg |
| `uvp6_seaglider` | Seaglider | UVP6 | Port of the Smart-Cable sg644 integration: logdev START/STOP, LPM records reduced to 5 size classes (1 in 10) and downloaded after the dive |

## How it works

```
 glider UART ──► line framer ──► glider adapter ──┐                ┌──► sensor commands
                                (slocum_bsd.c or  │   events       │    (templates)
                                 seaglider_logdev)├──► controller ─┤
 sensor UART ──► line framer ──► sensor.c ────────┘   (state       └──► glider reports
                                   │                   machine)
                                   └──► processing (stats, plug-ins) ──► store ──► download
```

* **Single event loop, no shared-state races.** UART interrupts only copy bytes into ring buffers.
  Everything else runs in the main loop, which sleeps until data arrives or a deadline expires. The
  legacy firmware polled the 32-byte UART FIFO and blocked for up to 200 ms during stop sequences,
  which could drop glider messages. Byte loss is now counted and reported (health bit 128).
* **One state machine** (Zephyr SMF, `src/core/controller.c`):

  ```
  BOOT ──► IDLE ◄──► PASSTHROUGH
            │ ▲
            ▼ │ glider session ends
          ACTIVE: ARMED ──► STARTING ──► SAMPLING ──► STOPPING ──► HOLDOFF ──► ARMED
  ```

  Every waiting state has a deadline. Acks are retried a bounded number of times. A failed start
  backs off and tries again later. The machine never waits forever.
* **Sampling policy.** The sensor should run while all of these hold: the glider session is open,
  the glider has commanded a start (Seaglider only), the flight phase is enabled, the depth is inside
  the window (with hysteresis), and the time is known or the time wait has expired. On Slocum, pilots
  can override the phase mode and depth window from shore through `sci_generic` inputs
  (`SMART_SLOCUM_IDX_IN_*`).
* **Recovery paths:**
  * a `$SD` after a board reset reopens the session;
  * a sensor that reboots mid-acquisition is restarted;
  * sensor data arriving while the board believes the sensor is idle triggers a stop;
  * data arriving before the start ack counts as the ack;
  * passthrough times out;
  * a hardware watchdog (4 s) covers everything else.
* **Glider checksums** on `$SD` are verified; bad lines are dropped and counted (health bit 64).

## Configuring a profile

Options live in `Kconfig` (all with defaults, run `west build -t menuconfig` to browse). A profile
only lists what differs. The most important groups:

**Glider link.** `SMART_GLIDER_SLOCUM` or `SMART_GLIDER_SEAGLIDER`, plus `SMART_GLIDER_BAUD` and
`SMART_SENSOR_BAUD`.

**Slocum indices.** These are positions in `extctl.ini`, counted from 0 across the `mp`, `os` and `is`
sections in order. Use -1 for anything not used.

| Option | Direction | Typical sensor |
|---|---|---|
| `SMART_SLOCUM_IDX_TIME` / `_DEPTH` / `_PHASE` | glider → board | `m_present_time`, `m_depth`, `cc_final_depth_state_mode` |
| `SMART_SLOCUM_IDX_IN_MODE` / `_IN_DEPTH_MIN` / `_IN_DEPTH_MAX` | glider → board | `sci_generic_*` set by the pilot |
| `SMART_SLOCUM_IDX_OUT_HEALTH` / `_TIME_VALID` / `_COUNT` / `_MAX_DEPTH` / `_LAST_DEPTH` | board → glider | `sci_generic_*` |

**Sensor.** All behaviour comes from strings:

| Option | UVP6 example |
|---|---|
| `SMART_SENSOR_READY_TOKEN` | `HW_CONF` (printed at power-up; proves the sensor booted) |
| `SMART_SENSOR_START_CMD` | `$start:ACQ_CSCS_002H,%Y%m%d,%H%M%S;%n` |
| `SMART_SENSOR_START_CMD_NOTIME` | `$start:ACQ_CSCS_002H;%n` |
| `SMART_SENSOR_STOP_CMD` | `$stop;%n` |
| `SMART_SENSOR_START_ACK` / `_STOP_ACK` | `$startack;` / `$stopack;` |
| `SMART_SENSOR_DATA_PREFIX` / `_DATA_DEPTH_FIELD` | `LPM_DATA,` / `1` |

Command template codes, modelled on logdev:

| Code | Meaning |
|---|---|
| `%Y %m %d %H %M %S` | glider UTC date and time |
| `%e` | epoch seconds |
| `%D` | depth (m, 2 decimals) |
| `%c` | cast (1 dive, 2 climb) |
| `%s` | segment number |
| `%r` / `%n` | CR / LF |
| `%%` | a literal `%` |

**Policy.** `SMART_POLICY_PHASE_*`, `SMART_POLICY_DEPTH_MIN_M` / `_MAX_M` / `_HYST_M`,
`SMART_POLICY_TIME_WAIT_MS`, `SMART_POLICY_REQUIRE_TIME`, `SMART_POLICY_RESTART_ON_PHASE_CHANGE`,
`SMART_POLICY_RESTART_HOLDOFF_MS`.

**Power.** `SMART_SENSOR_POWER_CONTROL` switches the sensor through the relay on GP16 (JP1 on A-C).

### Health bit field

Reported on Slocum through `SMART_SLOCUM_IDX_OUT_HEALTH`, and on Seaglider by `INFO` and in each
`#SEG` line. Bits 0-1 have the same meaning as the legacy `SW,0` status.

| Bit | Value | Meaning |
|---|---|---|
| 0 | 1 | valid glider traffic received |
| 1 | 2 | sensor power-up banner seen |
| 2 | 4 | sampling |
| 3 | 8 | glider time known |
| 4 | 16 | last start got no ack |
| 5 | 32 | last stop got no ack |
| 6 | 64 | glider messages with bad checksum or unknown commands |
| 7 | 128 | UART overruns or bytes dropped |

## Adding a new integration

1. Copy the closest profile folder, e.g. `cp -r profiles/uvp6_slocum profiles/mysensor_slocum`.
2. Set the sensor strings and the policy in `smart.conf`. On Slocum, write `extctl.ini` and set the
   indices to match it. On Seaglider, write the `.cnf` so its keywords match the
   `SMART_LOGDEV_CMD_*` options.
3. Build the tests with `-DSMART_PROFILE=mysensor_slocum`. Add a scenario test if the behaviour
   differs from the shipped ones (see `tests/unit/src/test_scenario_*.c`).
4. Bench test with `tools/glider_sim.py`, then a deck/sim dive.

Sensor-specific processing goes in a plug-in under `src/proc/` (see `uvp6_lpm.c`). It gets each data
record and the start and end of each segment, and can write to the store. Register it in
`src/proc/proc.c` and `smart.cmake`.

## Source map

| Path | Content |
|---|---|
| `src/main.c` | event loop: UART wait, feed, watchdog |
| `src/core/app.c` | routes bytes into adapters and the controller (shared with the tests) |
| `src/core/controller.c` | state machine and sampling policy |
| `src/glider/slocum_bsd.c` | Slocum Backseat Driver adapter |
| `src/glider/seaglider_logdev.c` | Seaglider logdev adapter |
| `src/sensor/sensor.c` | generic configurable sensor |
| `src/proc/` | segment statistics and processing plug-ins |
| `src/core/store.c` | RAM store for data waiting to be downloaded |
| `src/util/` | NMEA framing, time, templates, number formatting |
| `src/hw/` | UART ports, relay, watchdog |
| `tests/unit/` | ztest unit and end-to-end scenario tests (native_sim) |

## Changes in behaviour from the legacy UVP6 firmware

* `$SW` values are sent as decimals: max depth goes out as `25.25` rather than `2525`. The glider
  parses `$SW` values as floats.
* The health value is sent after `$HI`. The legacy firmware sent it 2 s after power-up, before the
  proglet was listening.
* Leg statistics go out in one `$SW` message when the leg ends. The legacy firmware sent a separate
  `SW,1:1` at the mode change.
* The UVP6 waits up to 5 s for the first `$SD` time before starting without a timestamp.

## Known limitations / next steps

* **The store is in RAM.** On Seaglider the `.cnf` sets `post-stop=on` so the board stays powered
  until the download. Persisting to flash (or to the OpenLog) is the next robustness step.
* **Configuration is compile-time.** A runtime layer is planned: settings changed over the glider
  port or from shore, saved in flash.
* **UART1 is shared with the OpenLog.** The sensor and the OpenLog share UART1 through JP2/JP3.
  Logging raw sensor data while talking to the sensor needs a PIO UART.
* **Not yet validated against real hardware or a real glider.** It has only run in the host tests.
