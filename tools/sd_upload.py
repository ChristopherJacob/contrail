#!/usr/bin/env python3
"""Write a file to the CYD's microSD card over USB serial, through the cyd_sdload helper firmware.

Flash the helper first (pio run -e cyd_sdload -t upload), then run this. It resets the board, waits
for the helper's READY banner (echoing the boot info: card type, size, FAT type, listings), sends
PUT <dest> <size> <crc32> and streams the file in the chunks the device asks for, each followed by
its CRC32; a chunk the device rejects is sent again. The device reads the whole file back, checks
the CRC and only then renames it into place. The protocol is documented in
firmware/src/main_sdload.cpp. Needs pyserial (tools/requirements.txt).

  tools/.venv/bin/python -I tools/sd_upload.py --port /dev/cu.usbserial-114440 --file logos.pak \\
      [--dest /contrail/logos.pak] [--baud 460800] [--no-reset]
  tools/.venv/bin/python -I tools/sd_upload.py --port PORT --cmd "LS /contrail" [--cmd "CRC /contrail/logos.pak"]

Exit codes: 0 done, 1 the device reported an error or a check failed, 2 no READY from the device.
"""
import argparse
import os
import struct
import sys
import time
import zlib

import serial


def read_line(s, timeout):
    """One line from the device (without the newline), or None on timeout."""
    end = time.time() + timeout
    buf = bytearray()
    while time.time() < end:
        c = s.read(1)
        if not c:
            continue
        if c == b"\n":
            return buf.decode(errors="replace").rstrip("\r")
        buf += c
    return None


def wait_ready(s, reset, timeout):
    if reset:
        s.dtr = False
        s.rts = True
        time.sleep(0.1)
        s.rts = False
    end = time.time() + timeout
    next_ping = time.time() + (3 if reset else 0)
    while time.time() < end:
        if time.time() >= next_ping:
            s.write(b"PING\n")
            next_ping = time.time() + 1.5
        line = read_line(s, 0.5)
        if line is None:
            continue
        if line.startswith("[sdload]") or line.startswith(("F ", "D ")):
            print(line, flush=True)
        if "STOPPED" in line or "FATAL" in line:
            print(line, file=sys.stderr)
            return False
        if line == "[sdload] READY" or line == "OK READY":
            time.sleep(0.5)  # let replies to any PINGs still queued on the device arrive
            s.reset_input_buffer()
            return True
    return False


def command(s, cmd, timeout=120):
    """Send one command; print its reply lines; return the final OK/ERR line."""
    s.write((cmd + "\n").encode())
    while True:
        line = read_line(s, timeout)
        if line is None:
            return "ERR timeout"
        print(line, flush=True)
        if line.startswith(("OK", "ERR")):
            return line


def put(s, path, dest):
    data = open(path, "rb").read()
    crc = zlib.crc32(data) & 0xFFFFFFFF
    print("PUT %s: %d bytes, crc32 %08x" % (dest, len(data), crc), flush=True)
    s.write(("PUT %s %d %08x\n" % (dest, len(data), crc)).encode())
    line = read_line(s, 10)
    while line == "OK READY":
        line = read_line(s, 10)
    if not line or not line.startswith("GO "):
        print("device refused: %s" % line, file=sys.stderr)
        return False
    chunk = int(line.split()[1])
    off, resends, t0, last_print = 0, 0, time.time(), 0
    while off < len(data):
        piece = data[off:off + chunk]
        s.write(piece + struct.pack("<I", zlib.crc32(piece) & 0xFFFFFFFF))
        line = read_line(s, 15)
        if line is None:
            print("\ntimeout waiting for ack at %d" % off, file=sys.stderr)
            return False
        if line.startswith("A "):
            off = int(line.split()[1])
        elif line.startswith("N "):
            resends += 1
        else:
            print("\n%s" % line, file=sys.stderr)
            return False
        if time.time() - last_print > 0.5 or off == len(data):
            last_print = time.time()
            el = time.time() - t0
            print("\r  %7d / %d KB  %5.1f%%  %5.1f KB/s  resends %d " % (
                off // 1024, len(data) // 1024, 100.0 * off / len(data),
                off / 1024 / max(el, 1e-6), resends), end="", flush=True)
    el = time.time() - t0
    print("\nsent in %.1f s (%.1f KB/s), %d resends; device verifying..." % (
        el, len(data) / 1024 / el, resends), flush=True)
    while True:
        line = read_line(s, 120)
        if line is None:
            print("timeout waiting for verification", file=sys.stderr)
            return False
        print(line, flush=True)
        if line.startswith("ERR"):
            return False
        if line.startswith("OK"):
            want = "OK %d %08x" % (len(data), crc)
            if line != want:
                print("unexpected reply, wanted %r" % want, file=sys.stderr)
                return False
            print("total %.1f s" % (time.time() - t0))
            return True


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=460800)
    ap.add_argument("--file", help="local file to upload")
    ap.add_argument("--dest", default="/contrail/logos.pak")
    ap.add_argument("--cmd", action="append", default=[], help="LS <dir> or CRC <path>; repeatable")
    ap.add_argument("--no-reset", action="store_true", help="don't reset the board first")
    a = ap.parse_args()
    if not a.file and not a.cmd:
        ap.error("give --file and/or --cmd")
    if a.file and not os.path.isfile(a.file):
        ap.error("no such file: %s" % a.file)
    s = serial.Serial(a.port, a.baud, timeout=0.1)
    if not wait_ready(s, not a.no_reset, 20):
        print("no READY from the device", file=sys.stderr)
        return 2
    if a.file and not put(s, a.file, a.dest):
        return 1
    for c in a.cmd:
        if not command(s, c).startswith("OK"):
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
