#!/usr/bin/env python3
#
# Functional test that tests TAP local migration
# with fd passing
#
# Copyright (c) Yandex Technologies LLC, 2025
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import time
import subprocess
from subprocess import run
import signal
from typing import Tuple

from qemu_test import (
    LinuxKernelTest,
    Asset,
    exec_command_and_wait_for_pattern,
)
from qemu_test.decorators import skipWithoutSudo

GUEST_IP = "10.0.1.2"
GUEST_IP_MASK = f"{GUEST_IP}/24"
GUEST_MAC = "d6:0d:75:f8:0f:b7"
HOST_IP = "10.0.1.1"
HOST_IP_MASK = f"{HOST_IP}/24"
TAP_ID = "tap0"
TAP_ID2 = "tap1"
TAP_MAC = "e6:1d:44:b5:03:5d"


def ip(args, check=True) -> None:
    """Run ip command with sudo"""
    run(["sudo", "ip"] + args, check=check)


def del_tap(tap_name: str = TAP_ID) -> None:
    ip(["tuntap", "del", tap_name, "mode", "tap", "multi_queue"], check=False)


def init_tap(tap_name: str = TAP_ID, with_ip: bool = True) -> None:
    ip(["tuntap", "add", "dev", tap_name, "mode", "tap", "multi_queue"])
    if with_ip:
        ip(["link", "set", "dev", tap_name, "address", TAP_MAC])
        ip(["addr", "add", HOST_IP_MASK, "dev", tap_name])
    ip(["link", "set", tap_name, "up"])


def switch_network_to_tap2() -> None:
    ip(["link", "set", TAP_ID2, "down"])
    ip(["link", "set", TAP_ID, "down"])
    ip(["addr", "delete", HOST_IP_MASK, "dev", TAP_ID])
    ip(["link", "set", "dev", TAP_ID2, "address", TAP_MAC])
    ip(["addr", "add", HOST_IP_MASK, "dev", TAP_ID2])
    ip(["link", "set", TAP_ID2, "up"])


def parse_ping_line(line: str) -> float:
    # suspect lines like
    # [1748524876.590509] 64 bytes from 94.245.155.3 \
    #      (94.245.155.3): icmp_seq=1 ttl=250 time=101 ms
    spl = line.split()
    return float(spl[0][1:-1])


def parse_ping_output(out) -> Tuple[bool, float, float]:
    lines = [x for x in out.split("\n") if x.startswith("[")]

    try:
        first_no_ans = next(
            (ind for ind in range(len(lines)) if lines[ind][20:26] == "no ans")
        )
    except StopIteration:
        return False, parse_ping_line(lines[0]), parse_ping_line(lines[-1])

    last_no_ans = next(
        ind
        for ind in range(len(lines) - 1, -1, -1)
        if lines[ind][20:26] == "no ans"
    )

    return (
        True,
        parse_ping_line(lines[first_no_ans]),
        parse_ping_line(lines[last_no_ans]),
    )


def wait_migration_finish(source_vm, target_vm):
    migr_events = (
        ("MIGRATION", {"data": {"status": "completed"}}),
        ("MIGRATION", {"data": {"status": "failed"}}),
    )

    source_e = source_vm.events_wait(migr_events)["data"]
    target_e = target_vm.events_wait(migr_events)["data"]

    source_s = source_vm.cmd("query-status")["status"]
    target_s = target_vm.cmd("query-status")["status"]

    assert (
        source_e["status"] == "completed"
        and target_e["status"] == "completed"
        and source_s == "postmigrate"
        and target_s == "paused"
    ), f"""Migration failed:
    SRC status: {source_s}
    SRC event: {source_e}
    TGT status: {target_s}
    TGT event:{target_e}"""


@skipWithoutSudo()
class VhostUserBlkFdMigration(LinuxKernelTest):

    ASSET_KERNEL = Asset(
        (
            "https://archives.fedoraproject.org/pub/archive/fedora/linux/releases"
            "/31/Server/x86_64/os/images/pxeboot/vmlinuz"
        ),
        "d4738d03dbbe083ca610d0821d0a8f1488bebbdccef54ce33e3adb35fda00129",
    )

    ASSET_INITRD = Asset(
        (
            "https://archives.fedoraproject.org/pub/archive/fedora/linux/releases"
            "/31/Server/x86_64/os/images/pxeboot/initrd.img"
        ),
        "277cd6c7adf77c7e63d73bbb2cded8ef9e2d3a2f100000e92ff1f8396513cd8b",
    )

    ASSET_ALPINE_ISO = Asset(
        (
            "https://dl-cdn.alpinelinux.org/"
            "alpine/v3.22/releases/x86_64/alpine-standard-3.22.1-x86_64.iso"
        ),
        "96d1b44ea1b8a5a884f193526d92edb4676054e9fa903ad2f016441a0fe13089",
    )

    def setUp(self):
        super().setUp()

        init_tap()

        self.outer_ping_proc = None

    def tearDown(self):
        try:
            del_tap(TAP_ID)
            del_tap(TAP_ID2)

            if self.outer_ping_proc:
                self.stop_outer_ping()
        finally:
            super().tearDown()

    def start_outer_ping(self) -> None:
        assert self.outer_ping_proc is None
        self.outer_ping_log = self.scratch_file("ping.log")
        with open(self.outer_ping_log, "w") as f:
            self.outer_ping_proc = subprocess.Popen(
                ["ping", "-i", "0", "-O", "-D", GUEST_IP],
                text=True,
                stdout=f,
            )

    def stop_outer_ping(self) -> str:
        assert self.outer_ping_proc
        self.outer_ping_proc.send_signal(signal.SIGINT)

        self.outer_ping_proc.communicate(timeout=5)
        self.outer_ping_proc = None

        with open(self.outer_ping_log) as f:
            return f.read()

    def stop_ping_and_check(self, stop_time, resume_time):
        ping_res = self.stop_outer_ping()

        discon, a, b = parse_ping_output(ping_res)

        if not discon:
            text = (
                f"STOP: {stop_time}, RESUME: {resume_time}," f"PING: {a} - {b}"
            )
            if a > stop_time or b < resume_time:
                self.fail(f"PING failed: {text}")
            self.log.info(f"PING: no packets lost: {text}")
            return

        text = (
            f"STOP: {stop_time}, RESUME: {resume_time},"
            f"PING: disconnect: {a} - {b}"
        )
        self.log.info(text)
        eps = 0.01
        if a < stop_time - eps or b > resume_time + eps:
            self.fail(text)

    def one_ping_from_guest(self, vm) -> None:
        exec_command_and_wait_for_pattern(
            self,
            f"ping -c 1 -W 1 {HOST_IP}",
            "1 packets transmitted, 1 packets received",
            "1 packets transmitted, 0 packets received",
            vm=vm,
        )
        self.wait_for_console_pattern("# ", vm=vm)

    def one_ping_from_host(self) -> None:
        run(["ping", "-c", "1", "-W", "1", GUEST_IP])

    def setup_shared_memory(self):
        shm_path = f"/dev/shm/qemu_test_{os.getpid()}"

        try:
            with open(shm_path, "wb") as f:
                f.write(b"\0" * (1024 * 1024 * 1024))  # 1GB
        except Exception as e:
            self.fail(f"Failed to create shared memory file: {e}")

        return shm_path

    def prepare_and_launch_vm(
        self, shm_path, vhost, incoming=False, vm=None, backend_transfer=True
    ):
        if not vm:
            vm = self.vm

        vm.set_console()
        vm.add_args("-accel", "kvm")
        vm.add_args("-device", "pcie-pci-bridge,id=pci.1,bus=pcie.0")
        vm.add_args("-m", "1G")

        vm.add_args(
            "-object",
            f"memory-backend-file,id=ram0,size=1G,mem-path={shm_path},share=on",
        )
        vm.add_args("-machine", "memory-backend=ram0")

        vm.add_args(
            "-drive",
            f"file={self.ASSET_ALPINE_ISO.fetch()},media=cdrom,format=raw",
        )

        vm.add_args("-S")

        if incoming:
            vm.add_args("-incoming", "defer")

        vm_s = "target" if incoming else "source"
        self.log.info(f"Launching {vm_s} VM")
        vm.launch()

        self.set_migration_capabilities(vm, backend_transfer)

        if not backend_transfer:
            tap_name = TAP_ID2 if incoming else TAP_ID
        else:
            tap_name = TAP_ID

        self.add_virtio_net(vm, vhost, tap_name)

    def add_virtio_net(self, vm, vhost: bool, tap_name: str = "tap0"):
        netdev_params = {
            "id": "netdev.1",
            "vhost": vhost,
            "type": "tap",
            "ifname": tap_name,
            "script": "no",
            "downscript": "no",
            "queues": 4,
            "vnet_hdr": True,
        }

        vm.cmd("netdev_add", netdev_params)

        vm.cmd(
            "device_add",
            driver="virtio-net-pci",
            romfile="",
            id="vnet.1",
            netdev="netdev.1",
            mq=True,
            vectors=18,
            bus="pci.1",
            mac=GUEST_MAC,
            disable_legacy="off",
        )

    def set_migration_capabilities(self, vm, backend_transfer=True):
        capabilities = [
            {"capability": "events", "state": True},
            {"capability": "x-ignore-shared", "state": True},
        ]
        vm.cmd("migrate-set-capabilities", {"capabilities": capabilities})
        if backend_transfer:
            vm.cmd(
                "migrate-set-parameters",
                {"backend-transfer": ["virtio-net-tap"]},
            )

    def setup_guest_network(self) -> None:
        exec_command_and_wait_for_pattern(self, "ip addr", "# ")
        exec_command_and_wait_for_pattern(
            self,
            f"ip addr add {GUEST_IP_MASK} dev eth0 && "
            "ip link set eth0 up && echo OK",
            "OK",
        )
        self.wait_for_console_pattern("# ")

    def do_test_tap_fd_migration(self, vhost, backend_transfer=True):
        self.require_accelerator("kvm")
        self.set_machine("q35")

        socket_dir = self.socket_dir()
        migration_socket = os.path.join(socket_dir.name, "migration.sock")

        shm_path = self.setup_shared_memory()

        # Setup second TAP if needed
        if not backend_transfer:
            del_tap(TAP_ID2)
            init_tap(TAP_ID2, with_ip=False)

        self.prepare_and_launch_vm(
            shm_path, vhost, backend_transfer=backend_transfer
        )
        self.vm.cmd("cont")
        self.wait_for_console_pattern("login:")
        exec_command_and_wait_for_pattern(self, "root", "# ")

        self.setup_guest_network()

        self.one_ping_from_guest(self.vm)
        self.one_ping_from_host()
        self.start_outer_ping()

        # Get some successful pings before migration
        time.sleep(0.5)

        target_vm = self.get_vm(name="target")
        self.prepare_and_launch_vm(
            shm_path,
            vhost,
            incoming=True,
            vm=target_vm,
            backend_transfer=backend_transfer,
        )

        target_vm.cmd("migrate-incoming", {"uri": f"unix:{migration_socket}"})

        self.log.info("Starting migration")
        freeze_start = time.time()
        self.vm.cmd("migrate", {"uri": f"unix:{migration_socket}"})

        self.log.info("Waiting for migration completion")
        wait_migration_finish(self.vm, target_vm)

        # Switch network to tap1 if not using backend transfer
        if not backend_transfer:
            switch_network_to_tap2()

        target_vm.cmd("cont")
        freeze_end = time.time()

        self.vm.shutdown()

        self.log.info("Verifying PING on target VM after migration")
        self.one_ping_from_guest(target_vm)
        self.one_ping_from_host()

        # And a bit more pings after source shutdown
        time.sleep(0.3)
        self.stop_ping_and_check(freeze_start, freeze_end)

        target_vm.shutdown()

    def test_tap_fd_migration(self):
        self.do_test_tap_fd_migration(False)

    def test_tap_fd_migration_vhost(self):
        self.do_test_tap_fd_migration(True)

    def test_tap_new_tap_migration(self):
        self.do_test_tap_fd_migration(False, backend_transfer=False)

    def test_tap_new_tap_migration_vhost(self):
        self.do_test_tap_fd_migration(True, backend_transfer=False)


if __name__ == "__main__":
    LinuxKernelTest.main()
