#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Copyright (c) 2025-2026 Leonid Bloch <lb.workbox@gmail.com>

"""Reference: mirror host laptop power state into a QEMU guest via QMP.

The C devices (battery/acad/button) are pure QMP-controlled; this script
shows how a management layer (libvirt etc.) might wire host sysfs/procfs
state to the QMP commands.  See docs/tools/laptop-mirror.rst.
"""
from __future__ import annotations

import argparse
import logging
import os
import signal
import socket
import sys
import time
from pathlib import Path
from typing import Any

try:
    from qemu.qmp import QMPError
    from qemu.qmp.legacy import QEMUMonitorProtocol
except ModuleNotFoundError as exc:
    print(f"Module '{exc.name}' not found.", file=sys.stderr)
    print(f"Try $builddir/run {' '.join(sys.argv)}", file=sys.stderr)
    sys.exit(1)


log = logging.getLogger("laptop-mirror")

POWER_SUPPLY = Path("/sys/class/power_supply")
ACPI_BUTTON = Path("/proc/acpi/button")


def read_str(p: Path) -> str | None:
    try:
        return p.read_text().strip()
    except OSError:
        return None


def read_int(p: Path) -> int | None:
    s = read_str(p)
    try:
        return int(s) if s is not None else None
    except ValueError:
        return None


def find_supply(kind: str) -> Path | None:
    if not POWER_SUPPLY.is_dir():
        return None
    for d in sorted(POWER_SUPPLY.iterdir()):
        if read_str(d / "type") == kind:
            return d
    return None


def find_lid() -> Path | None:
    lid_dir = ACPI_BUTTON / "lid"
    if not lid_dir.is_dir():
        return None
    for sub in sorted(lid_dir.iterdir()):
        if (state := sub / "state").is_file():
            return state
    return None


def battery_state(path: Path) -> dict[str, Any] | None:
    status = read_str(path / "status") or ""
    cap = read_int(path / "capacity")
    if cap is None:
        en, ef = read_int(path / "energy_now"), read_int(path / "energy_full")
        if en is None or not ef:
            return None
        cap = en * 100 // ef

    state: dict[str, Any] = {
        "present": True,
        "charging": status == "Charging",
        "discharging": status == "Discharging",
        "charge-percent": max(0, min(100, cap)),
    }
    pw = read_int(path / "power_now")
    if pw is not None:
        state["rate"] = abs(pw) // 1000
    return state


def ac_online(path: Path) -> bool | None:
    v = read_int(path / "online")
    return None if v is None else bool(v)


def lid_open(path: Path) -> bool | None:
    s = read_str(path)
    return None if s is None else "open" in s.lower()


def qmp_connect(address, timeout):
    if isinstance(address, tuple):
        sock = socket.create_connection(address, timeout=timeout)
    else:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        sock.connect(address)
    sock.settimeout(timeout)
    if not sock.recv(1, socket.MSG_PEEK):
        sock.close()
        raise TimeoutError
    sock.settimeout(None)
    return sock


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Mirror host laptop hardware state to a QEMU guest "
                    "via QMP.")
    p.add_argument("-s", "--socket", default=os.environ.get("QMP_SOCKET"),
                   help="QMP socket: unix path or addr:port "
                        "(default: $QMP_SOCKET)")
    p.add_argument("-i", "--interval", type=float, default=2.0,
                   metavar="SECONDS",
                   help="polling interval in seconds (default: 2.0)")
    p.add_argument("-v", "--verbose", action="count", default=0,
                   help="increase verbosity (-v info, -vv debug)")
    p.add_argument("--battery", action=argparse.BooleanOptionalAction,
                   default=True, help="monitor the battery")
    p.add_argument("--ac-adapter", action=argparse.BooleanOptionalAction,
                   default=True, help="monitor the AC adapter")
    p.add_argument("--lid", action=argparse.BooleanOptionalAction,
                   default=True, help="monitor the lid button")
    args = p.parse_args()
    if not args.socket:
        p.error("--socket is required (or set $QMP_SOCKET)")
    if args.interval <= 0:
        p.error("--interval must be positive")
    if not (args.battery or args.ac_adapter or args.lid):
        p.error("at least one device must be enabled")
    return args


def main() -> int:
    args = parse_args()
    levels = [logging.WARNING, logging.INFO, logging.DEBUG]
    logging.basicConfig(level=levels[min(args.verbose, 2)],
                        format="%(message)s", stream=sys.stderr)
    logging.getLogger("qemu.qmp").setLevel(logging.CRITICAL)

    bat = find_supply("Battery") if args.battery else None
    ac = find_supply("Mains") if args.ac_adapter else None
    lid = find_lid() if args.lid else None
    if not (bat or ac or lid):
        log.error("No host laptop devices found to mirror")
        return 1
    for name, path in (("battery", bat), ("ac-adapter", ac), ("lid", lid)):
        if path is not None:
            log.info("Mirroring %s from %s", name, path)

    try:
        sock = qmp_connect(QEMUMonitorProtocol.parse_address(args.socket), 10)
    except TimeoutError:
        log.error("Timed out negotiating QMP with %s. Is another QMP "
                  "client (e.g. qmp-shell) holding the socket?", args.socket)
        return 1
    except OSError as exc:
        log.error("Could not connect to %s: %s", args.socket, exc)
        return 1

    qmp = QEMUMonitorProtocol(sock)
    try:
        qmp.connect()
    except QMPError as exc:
        log.error("QMP error: %s", exc)
        return 1

    prev: dict[str, dict[str, Any]] = {}

    def push(command: str, payload: dict[str, Any]) -> None:
        if prev.get(command) == payload:
            return
        try:
            qmp.cmd(command, **payload)
        except QMPError as exc:
            log.warning("%s failed: %s", command, exc)
            return
        prev[command] = payload
        log.info("%s -> %s", command, payload)

    running = True

    def stop(_signum, _frame):
        nonlocal running
        running = False

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)

    try:
        while running:
            if bat and (s := battery_state(bat)) is not None:
                push("battery-set-state", {"state": s})
            if ac and (c := ac_online(ac)) is not None:
                push("ac-adapter-set-state", {"connected": c})
            if lid and (o := lid_open(lid)) is not None:
                push("lid-button-set-state", {"open": o})
            time.sleep(args.interval)
        return 0
    finally:
        qmp.close()


if __name__ == "__main__":
    sys.exit(main())
