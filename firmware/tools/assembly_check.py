#!/usr/bin/env python3
"""Interactive assembly verification for a SMaRT board.

Walks through populating the board one part at a time and checks each part
as it is added, using the self-test firmware (firmware/selftest) rather than
the real glider/sensor protocol:

  ID          -> identifies which UART (GLIDER or SENSOR) you are talking to
  PING        -> PONG
  RELAY ON/OFF -> drives the sensor power relay, replies RELAY=ON / RELAY=OFF
  anything else on one UART is forwarded to the other as FWD:<PORT>:<text>

A reply that is not a plain echo of what was sent is what makes this a real
test: a wire accidentally shorted TX-to-RX would echo, but it could never
answer PING with PONG or relay a message to the *other* connector.

Flash the self-test image first:
  west build -p -b rpi_pico firmware/selftest -d build-selftest
  west flash -d build-selftest
  (or drag-and-drop build-selftest/zephyr/zephyr.uf2 onto the BOOTSEL drive)

Then, as you populate the board:
  python3 assembly_check.py
  python3 assembly_check.py --from-stage 3   # resume partway through
  python3 assembly_check.py --list

Requires pyserial (pip install pyserial). The SWD stage additionally needs
the SEGGER J-Link tools (JLinkExe, JLinkRTTLogger) on PATH.
"""
import argparse
import os
import random
import string
import subprocess
import sys
import tempfile
import time

import serial
import serial.tools.list_ports

BAUD = 115200  # fixed; the self-test image does not do runtime baud changes

JLINK_DEVICE = "RP2040_M0_0"  # core 0; matches Zephyr's west/pyocd runner for rpi_pico


def nonce(n=8):
    return "".join(random.choice(string.ascii_uppercase + string.digits) for _ in range(n))


def list_ports():
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print("  (no serial ports detected)")
        return
    for p in ports:
        print(f"  {p.device:20s} {p.description}")


def open_port(role, remembered):
    """Ask which serial device to use for `role`, offering the previous pick."""
    while True:
        if remembered:
            ans = input(f"Use {remembered} for the {role} adapter again? [Y/n/list] ").strip().lower()
            if ans in ("", "y", "yes"):
                path = remembered
            elif ans == "list":
                list_ports()
                continue
            else:
                path = input(f"Serial device for the {role} adapter: ").strip()
        else:
            print(f"Available serial ports:")
            list_ports()
            path = input(f"Serial device for the {role} adapter: ").strip()
        try:
            ser = serial.Serial(path, BAUD, timeout=0.3)
            time.sleep(0.2)
            ser.reset_input_buffer()
            return ser, path
        except serial.SerialException as e:
            print(f"  could not open {path}: {e}")


def transact(ser, line, timeout=2.0, idle=0.2):
    """Send one line, return what comes back.

    Waits for the first byte then returns `idle` seconds after the last byte
    received, so a quick reply does not cost the full timeout; a dead port
    still waits the full timeout before giving up.
    """
    ser.reset_input_buffer()
    ser.write((line + "\r\n").encode())
    deadline = time.time() + timeout
    buf = b""
    last_rx = None
    while time.time() < deadline:
        chunk = ser.read(256)
        if chunk:
            buf += chunk
            last_rx = time.time()
        elif last_rx is not None and time.time() - last_rx > idle:
            break
        else:
            time.sleep(0.02)
    return buf.decode(errors="replace")


def check(label, ok):
    print(f"  [{'PASS' if ok else 'FAIL'}] {label}")
    return ok


def check_id_ping(ser, expect_port, who):
    reply = transact(ser, "ID")
    ok = check(f"{who}: ID -> PORT={expect_port}", f"PORT={expect_port}" in reply)
    reply = transact(ser, "PING")
    ok = check(f"{who}: PING -> PONG", "PONG" in reply) and ok
    return ok


def pause(instructions):
    print()
    print(instructions)
    input("Press Enter when ready to test... ")
    print()


# --------------------------------------------------------------------------
# J-Link / SWD
# --------------------------------------------------------------------------

JLINK_FAIL_PHRASES = (
    "no j-link", "cannot connect", "can not connect", "could not connect",
    "communication timed out", "connection failed", "failed to connect",
    "unknown device",
)


def run_jlink_tool(tool, extra_args, timeout_s):
    """Run a SEGGER J-Link CLI tool, hard-killed after timeout_s.

    Without a probe attached, JLinkExe/JLinkRTTLogger block on "Connecting to
    J-Link via USB..." forever and ignore SIGTERM, so this always runs them
    under `timeout -s KILL` - there is no clean way to ask them to give up.
    Returns (timed_out, output_text).
    """
    cmd = ["timeout", "-s", "KILL", str(timeout_s), tool,
           "-Device", JLINK_DEVICE, "-If", "SWD", "-Speed", "4000"] + extra_args
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout_s + 5)
    except subprocess.TimeoutExpired:
        return True, ""
    # subprocess reports a signal-killed child as a negative returncode
    # (-9 for SIGKILL), not the shell's 128+signal convention.
    return proc.returncode < 0, (proc.stdout + proc.stderr)


def stage_jlink(ctx):
    pause(
        "Stage: SWD debug probe (J8, 'Programmer' header)\n"
        "  - Connect a J-Link (or compatible) debug probe to J8.\n"
        "  - This resets the board over SWD and then reads back the self-test\n"
        "    firmware's RTT boot banner: the same debug path (SEGGER RTT via\n"
        "    J8) the real SMaRT firmware uses for all of its logging.\n"
        "  Needs JLinkExe and JLinkRTTLogger on PATH."
    )
    print("  connecting over SWD (up to 10s if the probe is not found)...")
    with tempfile.NamedTemporaryFile("w", suffix=".jlink", delete=False) as f:
        f.write("r\ng\nexit\n")
        script_path = f.name
    try:
        timed_out, out = run_jlink_tool(
            "JLinkExe", ["-autoconnect", "1", "-CommanderScript", script_path], 10)
    finally:
        os.unlink(script_path)

    failed = timed_out or any(p in out.lower() for p in JLINK_FAIL_PHRASES)
    ok = check("J-Link SWD connect + reset", not failed)
    if failed:
        print("  (no response within 10s - check the probe is connected and powered)"
              if timed_out else "")
        if out.strip():
            print("  ---- JLinkExe output ----")
            print("  " + out.strip().replace("\n", "\n  "))
            print("  --------------------------")
        return False

    print("  reading RTT boot banner (5s)...")
    with tempfile.NamedTemporaryFile(delete=False) as f:
        rtt_log = f.name
    try:
        run_jlink_tool("JLinkRTTLogger", ["-RTTChannel", "0", rtt_log], 5)
        text = open(rtt_log, errors="replace").read()
    finally:
        os.unlink(rtt_log)
    ok = check("RTT banner seen ('SMaRT self-test')", "SMaRT self-test" in text) and ok
    if not ok:
        print(f"  (RTT capture was: {text.strip()!r})")
    return ok


# --------------------------------------------------------------------------
# Stages
# --------------------------------------------------------------------------

def stage_1_pico(ctx):
    pause(
        "Stage 1: Pico bring-up (logic level, no RS232 transceivers yet)\n"
        "  - Flash the self-test image (see --help) if you have not already.\n"
        "  - Wire a 3.3V TTL USB-serial adapter to J1 (TestPins_Glider):\n"
        "    TX, RX, GND. Do NOT use an RS232-level adapter here."
    )
    ser, path = open_port("glider (J1, logic level)", ctx.get("glider_path"))
    ctx["glider_path"] = path
    ok = check_id_ping(ser, "GLIDER", "J1 logic level")
    ser.close()
    return ok


def stage_2_power_glider(ctx):
    pause(
        "Stage 2: power + glider RS232 transceiver (U1 MP1584, U2 MAX3232)\n"
        "  - Populate U1 (buck regulator) and U2 (MAX3232) and apply board power\n"
        "    through J4.\n"
        "  - Before connecting anything else, you may want to confirm the\n"
        "    regulated rail at test point J10 reads the expected voltage for\n"
        "    this build with a multimeter.\n"
        "  - Move the adapter from J1 to J5 (Glider connector). This now needs\n"
        "    an RS232-level USB-serial adapter, not the 3.3V TTL one."
    )
    ans = input("Confirm J10 reads the expected regulated voltage [y/N]: ").strip().lower()
    if ans not in ("y", "yes"):
        print("  Stopping here - check the regulator before applying RS232 signals to it.")
        return False
    ser, path = open_port("glider (J5, RS232)", None)  # new adapter, don't offer the TTL one
    ctx["glider_path"] = path
    ok = check_id_ping(ser, "GLIDER", "J5 via U2")
    ser.close()
    return ok


def stage_3_sensor(ctx):
    pause(
        "Stage 3: sensor RS232 transceiver (U4 MAX3232) + full passthrough\n"
        "  - Populate U4 and connect a second RS232 USB-serial adapter to J13\n"
        "    (Sensor connector).\n"
        "  - Also reconnect the glider-side adapter to J5 if it is not still there."
    )
    glider, gpath = open_port("glider (J5, RS232)", ctx.get("glider_path"))
    ctx["glider_path"] = gpath
    sensor, spath = open_port("sensor (J13, RS232)", ctx.get("sensor_path"))
    ctx["sensor_path"] = spath

    ok = check_id_ping(sensor, "SENSOR", "J13 via U4")

    tag = nonce()
    reply = transact(glider, tag)
    ok = check("glider TX: 'FWD:SENT' ack for own send", "FWD:SENT" in reply) and ok
    time.sleep(0.2)
    seen = sensor.read(256).decode(errors="replace")
    ok = check(f"glider->sensor passthrough ({tag})", f"FWD:GLIDER:{tag}" in seen) and ok

    tag2 = nonce()
    reply = transact(sensor, tag2)
    ok = check("sensor TX: 'FWD:SENT' ack for own send", "FWD:SENT" in reply) and ok
    time.sleep(0.2)
    seen = glider.read(256).decode(errors="replace")
    ok = check(f"sensor->glider passthrough ({tag2})", f"FWD:SENSOR:{tag2}" in seen) and ok

    glider.close()
    sensor.close()
    return ok


def stage_4_relay(ctx):
    pause(
        "Stage 4: sensor power relay (U3) / JP1\n"
        "  - Populate U3 (G3VM-41AY1 relay).\n"
        "  - For this test to mean anything, JP1 must NOT be bridging A-C\n"
        "    (that position bypasses the relay and wires the sensor return\n"
        "    straight through).\n"
        "  - Connect a multimeter (continuity) or the sensor itself across J9\n"
        "    so you can see it switch."
    )
    ser, path = open_port("glider (J5, RS232)", ctx.get("glider_path"))
    ctx["glider_path"] = path

    reply = transact(ser, "RELAY ON")
    check("firmware ack: RELAY=ON", "RELAY=ON" in reply)
    ans1 = input("Did J9 switch ON? [y/N]: ").strip().lower() in ("y", "yes")
    check("J9 relay contact ON", ans1)

    reply = transact(ser, "RELAY OFF")
    check("firmware ack: RELAY=OFF", "RELAY=OFF" in reply)
    ans2 = input("Did J9 switch OFF? [y/N]: ").strip().lower() in ("y", "yes")
    check("J9 relay contact OFF", ans2)

    ser.close()
    return ans1 and ans2


def stage_5_optional(ctx):
    pause(
        "Stage 5: OpenLog (U5, JP2/JP3) and Qwiic (J2/J3) - manual checklist\n"
        "  These are not exercised by the self-test image's own protocol, so\n"
        "  there is nothing to automate here yet:\n"
        "   - OpenLog: with JP2/JP3 selecting it onto UART1 instead of the\n"
        "     sensor transceiver, power up and confirm its activity LED blinks\n"
        "     once at boot (it prints its own startup banner).\n"
        "   - Qwiic (J2/J3): visually confirm continuity/orientation; there is\n"
        "     no I2C traffic generated by this tool yet."
    )
    ans = input("Mark this checklist as reviewed? [y/N]: ").strip().lower()
    return ans in ("y", "yes")


STAGES = [
    ("Pico bring-up (logic level)", stage_1_pico),
    ("SWD debug probe (J8)", stage_jlink),
    ("Power + glider transceiver (U2)", stage_2_power_glider),
    ("Sensor transceiver (U4) + passthrough", stage_3_sensor),
    ("Sensor power relay (U3)", stage_4_relay),
    ("OpenLog / Qwiic checklist", stage_5_optional),
]


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--from-stage", type=int, default=1,
                    help="1-based stage number to start from (default 1)")
    p.add_argument("--list", action="store_true", help="list stages and exit")
    a = p.parse_args()

    if a.list:
        for i, (name, _) in enumerate(STAGES, 1):
            print(f"  {i}. {name}")
        return 0

    ctx = {}
    i = max(1, a.from_stage)
    while i <= len(STAGES):
        name, fn = STAGES[i - 1]
        print(f"\n=== Stage {i}/{len(STAGES)}: {name} ===")
        try:
            ok = fn(ctx)
        except KeyboardInterrupt:
            print("\ninterrupted")
            return 1
        if not ok:
            print(f"\nStage {i} ({name}) did not pass.")
            again = input("Retry this stage? [Y/n] ").strip().lower()
            if again in ("", "y", "yes"):
                continue
            print("Stopping here.")
            return 1
        print(f"Stage {i} ({name}) passed.")
        i += 1

    print("\nAll stages passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
