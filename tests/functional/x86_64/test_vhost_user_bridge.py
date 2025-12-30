#!/usr/bin/env python3
#
# Copyright (c) 2025 Software Freedom Conservancy, Inc.
#
# Author: Yodel Eldar <yodel.eldar@yodel.dev>
#
# SPDX-License-Identifier: GPL-2.0-or-later
"""
Test vhost-user-bridge (vubr) functionality:

    1) Run vhost-user-bridge on the host.
    2) Launch a guest VM:
        a) Instantiate a unix domain socket to the vubr-created path
        b) Instantiate a vhost-user net backend on top of that socket
        c) Expose vhost-user with a virtio-net-pci interface
        d) Instantiate UDP socket and user-mode net backends
        e) Hub the UDP and user-mode backends
    3) Run udhcpc in the guest to auto-configure networking.
    4) Run wget in the guest and check its retcode to test internet connectivity

The test fails if wget returns 1 and succeeds on 0.
"""

import os
import subprocess
from qemu_test import Asset, QemuSystemTest, which
from qemu_test import exec_command_and_wait_for_pattern
from qemu_test import is_readable_executable_file
from qemu_test import wait_for_console_pattern
from qemu_test.ports import Ports

class VhostUserBridge(QemuSystemTest):

    ASSET_KERNEL_INITRAMFS = Asset(
        "https://github.com/yodel/vhost-user-bridge-test/raw/refs/heads/main/bzImage",
        "3790bf35e4ddfe062425bca45e923df5a5ee4de44e456d6b00cf47f04991d549")

    def configure_vm(self, ud_socket_path, lport, rport):
        kernel_path = self.ASSET_KERNEL_INITRAMFS.fetch()

        self.require_accelerator("kvm")
        self.require_netdev("vhost-user")
        self.require_netdev("socket")
        self.require_netdev("hubport")
        self.require_netdev("user")
        self.require_device("virtio-net-pci")
        self.set_machine("q35")
        self.vm.set_console()
        self.vm.add_args(
            "-cpu",      "host",
            "-accel",    "kvm",
            "-kernel",   kernel_path,
            "-append",   "console=ttyS0",
            "-smp",      "2",
            "-m",        "128M",
            "-object",   "memory-backend-memfd,id=mem0,"
                         "size=128M,share=on,prealloc=on",
            "-numa",     "node,memdev=mem0",
            "-chardev", f"socket,id=char0,path={ud_socket_path}",
            "-netdev",   "vhost-user,id=vhost0,chardev=char0,vhostforce=on",
            "-device",   "virtio-net-pci,netdev=vhost0",
            "-netdev",  f"socket,id=udp0,udp=localhost:{lport},"
                        f"localaddr=localhost:{rport}",
            "-netdev",   "hubport,id=hub0,hubid=0,netdev=udp0",
            "-netdev",   "user,id=user0",
            "-netdev",   "hubport,id=hub1,hubid=0,netdev=user0"
        )

    def assemble_vubr_args(self, vubr_path, ud_socket_path, lport, rport):
        vubr_args = []

        if (stdbuf_path := which("stdbuf")) is None:
            self.log.info("Could not find stdbuf: vhost-user-bridge "
                          "log lines may appear out of order")
        else:
            vubr_args += [stdbuf_path, "-o0", "-e0"]

        vubr_args += [vubr_path, "-u", f"{ud_socket_path}",
                      "-l", f"127.0.0.1:{lport}", "-r", f"127.0.0.1:{rport}"]

        return vubr_args

    def test_vhost_user_bridge(self):
        prompt = "~ # "

        vubr_path = self.build_file("tests", "vhost-user-bridge")
        if is_readable_executable_file(vubr_path) is None:
            self.skipTest("Could not find a readable and executable "
                          "vhost-user-bridge")

        with Ports() as ports:
            sock_dir = self.socket_dir()
            ud_socket_path = os.path.join(sock_dir.name, "vubr-test.sock")
            lport, rport = ports.find_free_ports(2)

            self.configure_vm(ud_socket_path, lport, rport)

            vubr_log_path = self.log_file("vhost-user-bridge.log")
            self.log.info("For the vhost-user-bridge application log,"
                         f" see: {vubr_log_path}")

            vubr_args = self.assemble_vubr_args(vubr_path, ud_socket_path,
                                                lport, rport)

            with open(vubr_log_path, "w") as vubr_log, \
                 subprocess.Popen(vubr_args, stdin=subprocess.DEVNULL,
                                  stdout=vubr_log, stderr=subprocess.STDOUT):
                self.vm.launch()

                wait_for_console_pattern(self, prompt)
                exec_command_and_wait_for_pattern(self, "udhcpc -nt 1", prompt)
                exec_command_and_wait_for_pattern(self,
                    "wget -qT 2 --spider example.org", prompt)

                try:
                    exec_command_and_wait_for_pattern(self, "echo $?", "0", "1")
                except AssertionError:
                    self.log.error("Unable to confirm internet connectivity")
                    raise
                finally:
                    self.vm.shutdown()

if __name__ == '__main__':
    QemuSystemTest.main()
