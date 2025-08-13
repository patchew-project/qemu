#!/usr/bin/env python3
#
# Functional test that tests vhost-user-blk local migration
# with fd passing
#
# Copyright (c) Yandex
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import time
import subprocess

from qemu_test import (
    LinuxKernelTest,
    Asset,
    exec_command_and_wait_for_pattern,
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

    DATA1 = "TEST_DATA_BEFORE_MIGRATION_12345"
    DATA2 = "TEST_DATA_AFTER_MIGRATION_54321"

    def write_data(self, data, vm) -> None:
        exec_command_and_wait_for_pattern(
            self,
            f'echo "{data}" | ' "dd of=/dev/vda bs=512 count=1 oflag=direct",
            "# ",
            vm=vm,
        )

    def read_data(self, data, vm) -> None:
        exec_command_and_wait_for_pattern(
            self,
            "dd if=/dev/vda bs=512 count=1 iflag=direct 2>/dev/null",
            data,
            vm=vm,
        )

    def setUp(self):
        super().setUp()
        self.vhost_proc = None

    def tearDown(self):
        # Cleanup vhost-user server process
        if self.vhost_proc:
            try:
                self.vhost_proc.terminate()
                self.vhost_proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.vhost_proc.kill()
                self.vhost_proc.wait()
            except:
                pass

        super().tearDown()

    def create_test_image(self):
        """Create a temporary test image for vhost-user-blk"""
        img_path = self.scratch_file("disk.img")

        # Create 64MB image
        with open(img_path, "wb") as f:
            f.write(b"\0" * (64 * 1024 * 1024))

        return img_path

    def start_vhost_user_server(self, socket_path, img_path):
        """Start vhost-user-blk server using contrib/vhost-user-blk"""
        # Find vhost-user-blk binary
        vub_binary = self.build_file(
            "contrib", "vhost-user-blk", "vhost-user-blk"
        )

        if not os.path.isfile(vub_binary) or not os.access(vub_binary, os.X_OK):
            self.skipTest("vhost-user-blk binary not found")

        # assert that our further waiting would be correct
        self.assertFalse(os.path.exists(socket_path))

        cmd = [vub_binary, "-s", socket_path, "-b", img_path]
        self.log.info(f'Starting vhost-user server: {" ".join(cmd)}')
        self.vhost_proc = subprocess.Popen(
            cmd, stderr=subprocess.PIPE, text=True, preexec_fn=os.setsid
        )

        # Wait for socket to be created
        for _ in range(100):  # 10 seconds timeout
            time.sleep(0.1)

            # Check if process is still running
            if self.vhost_proc.poll() is not None:
                self.fail(f"vhost-user server failed: {self.vhost_proc.stderr}")

            if os.path.exists(socket_path):
                return

        self.fail(f"vhost-user socket {socket_path} was not created")

    def setup_shared_memory(self):
        shm_path = f"/dev/shm/qemu_test_{os.getpid()}"

        try:
            with open(shm_path, "wb") as f:
                f.write(b"\0" * (1024 * 1024 * 1024))  # 1GB
        except Exception as e:
            self.fail(f"Failed to create shared memory file: {e}")

        return shm_path

    def prepare_and_launch_vm(
        self, shm_path, vhost_socket, incoming=False, vm=None
    ):
        if not vm:
            vm = self.vm

        vm.add_args("-accel", "kvm")
        vm.add_args("-device", "pcie-pci-bridge,id=pci.1,bus=pcie.0")
        vm.add_args("-m", "1G")
        vm.add_args("-append", "console=ttyS0 rd.rescue")

        vm.add_args(
            "-object",
            f"memory-backend-file,id=ram0,size=1G,mem-path={shm_path},share=on",
        )
        vm.add_args("-machine", "memory-backend=ram0")

        vm.add_args("-kernel", self.ASSET_KERNEL.fetch())
        vm.add_args("-initrd", self.ASSET_INITRD.fetch())

        vm.add_args("-S")

        if incoming:
            vm.add_args("-incoming", "defer")

        vm.set_console()

        vm_s = "target" if incoming else "source"
        self.log.info(f"Launching {vm_s} VM")
        vm.launch()

        self.set_migration_capabilities(vm)
        self.add_vhost_user_blk_device(vm, vhost_socket, incoming)

    def add_vhost_user_blk_device(self, vm, socket_path, incoming=False):
        # Add chardev
        chardev_params = {
            "id": "chardev-virtio-disk0",
            "backend": {
                "type": "socket",
                "data": {
                    "addr": {"type": "unix", "data": {"path": socket_path}},
                    "server": False,
                    "reconnect-ms": 20,
                    "support-local-migration": True,
                },
            },
        }

        if incoming:
            chardev_params["backend"]["data"]["local-incoming"] = True

        vm.cmd("chardev-add", chardev_params)

        # Add device
        device_params = {
            "id": "virtio-disk0",
            "driver": "vhost-user-blk-pci",
            "chardev": "chardev-virtio-disk0",
            "num-queues": 1,
            "bus": "pci.1",
            "config-wce": False,
            "bootindex": 1,
            "disable-legacy": "off",
        }

        if incoming:
            device_params["local-incoming"] = True

        vm.cmd("device_add", device_params)

    def set_migration_capabilities(self, vm):
        capabilities = [
            {"capability": "events", "state": True},
            {"capability": "x-ignore-shared", "state": True},
            {"capability": "local-vhost-user-blk", "state": True},
            {"capability": "local-char-socket", "state": True},
        ]
        vm.cmd("migrate-set-capabilities", {"capabilities": capabilities})

    def test_vhost_user_blk_fd_migration(self):
        self.require_accelerator("kvm")
        self.set_machine("q35")

        socket_dir = self.socket_dir()
        vhost_socket = os.path.join(socket_dir.name, "vhost-user-blk.sock")
        migration_socket = os.path.join(socket_dir.name, "migration.sock")

        img_path = self.create_test_image()
        shm_path = self.setup_shared_memory()

        self.start_vhost_user_server(vhost_socket, img_path)

        self.prepare_and_launch_vm(shm_path, vhost_socket)
        self.vm.cmd("cont")
        self.wait_for_console_pattern("Entering emergency mode.")
        self.wait_for_console_pattern("# ")

        self.write_data(self.DATA1, self.vm)
        self.read_data(self.DATA1, self.vm)

        target_vm = self.get_vm(name="target")
        self.prepare_and_launch_vm(
            shm_path, vhost_socket, incoming=True, vm=target_vm
        )

        target_vm.cmd("migrate-incoming", {"uri": f"unix:{migration_socket}"})

        self.log.info("Starting migration")
        self.vm.cmd("migrate", {"uri": f"unix:{migration_socket}"})

        self.log.info("Waiting for migration completion")
        wait_migration_finish(self.vm, target_vm)

        target_vm.cmd("cont")
        self.vm.shutdown()

        self.log.info("Verifying disk on target VM after migration")
        self.read_data(self.DATA1, target_vm)
        self.write_data(self.DATA2, target_vm)
        self.read_data(self.DATA2, target_vm)

        target_vm.shutdown()


if __name__ == "__main__":
    LinuxKernelTest.main()
