#!/usr/bin/env python3
"""Bench glider simulator for SMaRT.

Plays the glider side of a dive over a USB-serial adapter wired to the SMaRT
glider connector (J5), so a real sensor can be tested without a glider.

  slocum:     $HI, $SD (time, depth, depth state) every few seconds, $BY
  seaglider:  logdev wake-up, START/DEPTH/STOP per cast, then SEND_TXT_FILE

Examples:
  python3 glider_sim.py slocum /dev/ttyUSB0 --baud 38400 --depth 30
  python3 glider_sim.py seaglider /dev/ttyUSB0 --baud 115200 --depth 30

The SD indices default to profiles/uvp6_slocum/extctl.ini.
Requires pyserial (pip install pyserial).
"""
import argparse
import functools
import sys
import threading
import time

import serial


def nmea(payload):
    cs = functools.reduce(lambda a, c: a ^ ord(c), payload, 0)
    return f"${payload}*{cs:02X}\r\n".encode()


def reader(port, stop):
    buf = b""
    while not stop.is_set():
        buf += port.read(256)
        while b"\n" in buf or b"\r" in buf:
            cut = min(i for i in (buf.find(b"\n"), buf.find(b"\r")) if i >= 0)
            line, buf = buf[:cut], buf[cut + 1:]
            if line.strip():
                print(f"  <- {line.decode(errors='replace')}", flush=True)
        if buf.endswith(b">"):  # logdev prompt has no line ending after it
            print(f"  <- {buf.decode(errors='replace')}", flush=True)
            buf = b""


def profile(max_depth, rate):
    """Yield (phase, depth) per second: dive to max_depth, climb back."""
    d = 0.0
    while d < max_depth:
        yield 1, d
        d += rate
    while d > 0:
        yield 2, d
        d -= rate
    yield 0, 0.0


def send(port, data):
    print(f"-> {data.decode().strip() or '<CR>'}", flush=True)
    port.write(data)


def run_slocum(port, a):
    send(port, nmea("HI"))
    t0 = time.time()
    last = 0
    for phase, depth in profile(a.depth, a.rate):
        now = time.time()
        if now - last >= a.period:
            last = now
            send(port, nmea(f"SD,{a.idx_time}:{now:.0f},{a.idx_depth}:{depth:.2f},"
                            f"{a.idx_phase}:{phase}"))
        time.sleep(1.0)
    time.sleep(3)
    send(port, nmea("BY"))
    print(f"dive took {time.time() - t0:.0f} s")


def run_seaglider(port, a):
    send(port, b"\r")
    time.sleep(1)
    cast = None
    last = 0
    for phase, depth in profile(a.depth, a.rate):
        if phase in (1, 2) and phase != cast:
            if cast is not None:
                send(port, b"STOP\r")
                time.sleep(4)
            cast = phase
            stamp = time.strftime("%Y%m%d,%H%M%S", time.gmtime())
            send(port, f"START {stamp},{cast}\r".encode())
        if time.time() - last >= a.period:
            last = time.time()
            send(port, f"DEPTH:{depth:.2f}\r".encode())
        time.sleep(1.0)
    send(port, b"STOP\r")
    time.sleep(4)
    for c in (1, 2):
        send(port, f"SEND_TXT_FILE {c}\r".encode())
        time.sleep(3)


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("glider", choices=["slocum", "seaglider"])
    p.add_argument("port")
    p.add_argument("--baud", type=int, default=38400)
    p.add_argument("--depth", type=float, default=20.0, help="max depth (m)")
    p.add_argument("--rate", type=float, default=0.5, help="vertical speed (m/s)")
    p.add_argument("--period", type=float, default=4.0, help="seconds between updates")
    p.add_argument("--idx-time", type=int, default=4)
    p.add_argument("--idx-depth", type=int, default=5)
    p.add_argument("--idx-phase", type=int, default=6)
    a = p.parse_args()

    with serial.Serial(a.port, a.baud, timeout=0.1) as port:
        stop = threading.Event()
        threading.Thread(target=reader, args=(port, stop), daemon=True).start()
        time.sleep(1)
        try:
            (run_slocum if a.glider == "slocum" else run_seaglider)(port, a)
            time.sleep(2)
        except KeyboardInterrupt:
            pass
        stop.set()
    return 0


if __name__ == "__main__":
    sys.exit(main())
