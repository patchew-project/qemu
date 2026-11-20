#!/usr/bin/env python3
#
# Tests that specifically try to exercise hypervisor features of the
# target machines. powernv supports the Power hypervisor ISA, and
# pseries supports the nested-HV hypervisor spec.
#
# Copyright (c) 2023 IBM Corporation
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import os
import re
import subprocess

from datetime import datetime, timezone
from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern, exec_command
from qemu_test import skipIfMissingCommands, skipBigDataTest
from qemu_test import exec_command_and_wait_for_pattern

# Alpine is a light weight distro that supports QEMU. These tests boot
# that on the machine then run a QEMU guest inside it in KVM mode,
# that runs the same Alpine distro image.
# QEMU packages are downloaded and installed on each test. That's not a
# large download, but it may be more polite to create qcow2 image with
# QEMU already installed and use that.
# XXX: The order of these tests seems to matter, see git blame.
@skipIfMissingCommands("xorriso")
@skipBigDataTest()
class HypervisorTest(QemuSystemTest):

    panic_message = 'Kernel panic - not syncing'
    good_message = 'VFS: Cannot open root device'

    ASSET_ISO = Asset(
        ('https://dl-cdn.alpinelinux.org/alpine/v3.21/'
         'releases/ppc64le/alpine-standard-3.21.0-ppc64le.iso'),
        '7651ab4e3027604535c0b36e86c901b4695bf8fe97b908f5b48590f6baae8f30')

    def extract_from_iso(self, iso, path):
        """
        Extracts a file from an iso file into the test workdir

        :param iso: path to the iso file
        :param path: path within the iso file of the file to be extracted
        :returns: path of the extracted file
        """
        filename = self.scratch_file(os.path.basename(path))

        cmd = f"xorriso -osirrox on -indev {iso} -cpx {path} {filename}"
        subprocess.run(cmd.split(), check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        os.chmod(filename, 0o600)

        return filename

    def setUp(self):
        super().setUp()

        self.iso_path = self.ASSET_ISO.fetch()
        self.vmlinuz = self.extract_from_iso(self.iso_path, '/boot/vmlinuz-lts')
        self.initramfs = self.extract_from_iso(self.iso_path, '/boot/initramfs-lts')

    def do_start_alpine(self):
        self.log.debug("do_start_alpine: launching L1 VM")
        self.vm.set_console()
        self.vm.add_args(
            "-kernel", self.vmlinuz,
            "-initrd", self.initramfs,
            "-smp", "4", "-m", "2g",
            "-drive", f"file={self.iso_path},format=raw,if=none,id=drive0,read-only=true",
            "-append", "console=hvc0 usbcore.nousb",
        )
        self.vm.launch()
        self.log.debug("do_start_alpine: VM launched, waiting for login prompt")
        ps1='localhost:~#'
        wait_for_console_pattern(self, 'localhost login:')
        self.log.debug("do_start_alpine: got login prompt, logging in as root")
        exec_command_and_wait_for_pattern(self, 'root', ps1)
        self.log.debug("do_start_alpine: logged in, setting system clock")
        # If the time is wrong, SSL certificates can fail.
        cmd='date -s "' + datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M:%S' + '"')
        exec_command_and_wait_for_pattern(self, cmd, ps1)
        ps1='alpine:~#'
        exec_command_and_wait_for_pattern(self, 'setup-alpine -qe', ps1)
        exec_command_and_wait_for_pattern(
            self,
            'for i in 1 2 3 4 5; do apk update && break; sleep 10; done',
            ps1)
        self.log.debug("do_start_alpine: apk update complete, Alpine L1 is ready")
        # Could upgrade here but it usually should not be necessary
        # exec_command_and_wait_for_pattern(self, 'apk upgrade --available', ps1)

    def do_stop_alpine(self):
        exec_command(self, 'echo "TEST ME"')
        wait_for_console_pattern(self, 'alpine:~#')
        exec_command(self, 'poweroff')
        wait_for_console_pattern(self, 'reboot: Power down')
        self.vm.wait()

    def do_setup_kvm(self):
        self.log.debug("do_setup_kvm: installing qemu-system-ppc64 in L1")
        ps1 = 'alpine:~#'
        exec_command_and_wait_for_pattern(self, 'apk add qemu-system-ppc64', ps1)
        self.log.debug("do_setup_kvm: qemu-system-ppc64 installed, loading kvm-hv")
        exec_command_and_wait_for_pattern(self, 'modprobe kvm-hv', ps1)
        self.log.debug("do_setup_kvm: kvm-hv module loaded")

    def _resolve_nvme_dev(self):
        """
        Determine the NVMe block device path that the ISO is attached as
        inside the L1 Alpine guest.  The device name is not stable across
        machine types (e.g. pseries may enumerate it as nvme0n1 or nvme1n1
        depending on the PCIe bus topology), so we ask the kernel directly.

        Returns the literal device path string, e.g. '/dev/nvme0n1', so
        callers can bake it directly into shell scripts without any shell
        variable scoping issues.
        """
        self.log.debug("_resolve_nvme_dev: probing NVMe device path in L1")
        ps1 = 'alpine:~#'
        # Print a clearly delimited marker and wait for the shell prompt so
        # we accumulate all console bytes in one call — including any bytes
        # that the terminal wraps across line boundaries.
        out = exec_command_and_wait_for_pattern(
            self,
            'echo "NVME_PATH:$(ls /dev/nvme?n1 | head -1)"',
            ps1,
        )
        # Decode, strip ANSI escape sequences and newlines, then extract the
        # path.  Terminal line-wrap can split the token mid-string so we join
        # all non-whitespace characters before matching.
        text = out.decode('utf-8', errors='replace')
        text = re.sub(r'\x1b\[[0-9;]*[A-Za-z]', '', text)
        joined = ''.join(text.split())
        # The joined string looks like: ...NVME_PATH:/dev/nvme0n1alpine:~#
        m = re.search(r'NVME_PATH:(/dev/nvme\S+?)alpine', joined)
        if not m:
            m = re.search(r'NVME_PATH:(/dev/nvme\S+)', joined)
        if m:
            dev = m.group(1).rstrip('#~:').strip()
        else:
            dev = '/dev/nvme0n1'
            self.log.debug("_resolve_nvme_dev: marker not found, using fallback")
        self.log.debug("_resolve_nvme_dev: resolved device=%s", dev)
        return dev

    # This uses the host's block device as the source file for guest block
    # device for install media. This is a bit hacky but allows reuse of the
    # iso without having a passthrough filesystem configured.
    def do_test_kvm(self, hpt=False):
        if hpt:
            append = 'disable_radix'
            self.log.debug("do_test_kvm: launching L2 in HPT mode")
        else:
            append = ''
            self.log.debug("do_test_kvm: launching L2 in Radix mode (default)")

        # Resolve the NVMe device path inside the L1 guest first so we do
        # not hardcode a path that may differ by machine type.
        nvme_dev = self._resolve_nvme_dev()

        # Write the L2 launch command to a script to avoid terminal line-wrap
        # breaking shell argument parsing, and to keep individual lines short.
        # nvme_dev is the literal path, e.g. /dev/nvme0n1 — bake it in.
        mnt = f'/media/$(basename {nvme_dev})'
        ps1 = 'alpine:~#'
        l2_script = [
            ("cat > /tmp/run-l2.sh << 'EOF'", '>'),
            ('#!/bin/sh', '>'),
            ('qemu-system-ppc64 -nographic \\', '>'),
            ('  -smp 2 -m 1g \\', '>'),
            ('  -cpu POWER9 \\', '>'),
            ('  -machine pseries,x-vof=on,accel=kvm \\', '>'),
            ('  -machine cap-cfpc=broken,cap-sbbc=broken,cap-ibs=broken,cap-ccf-assist=off \\', '>'),
            (f'  -drive file={nvme_dev},format=raw,readonly=on \\', '>'),
            (f'  -initrd {mnt}/boot/initramfs-lts \\', '>'),
            (f'  -kernel {mnt}/boot/vmlinuz-lts \\', '>'),
            (f'  -append "usbcore.nousb {append}"', '>'),
            ('EOF', ps1),
            ('chmod +x /tmp/run-l2.sh', ps1),
        ]
        for cmd, expected_pattern in l2_script:
            exec_command_and_wait_for_pattern(self, cmd, expected_pattern)
        self.log.debug("do_test_kvm: L2 launch script written, executing")
        exec_command(self, '/tmp/run-l2.sh')
        self.log.debug("do_test_kvm: L2 QEMU launched, waiting for L2 login prompt")
        # Alpine 3.21 kernel seems to crash in XHCI USB driver.
        ps1 = 'localhost:~#'
        wait_for_console_pattern(self, 'localhost login:',
                                 failure_message=self.panic_message)
        self.log.debug("do_test_kvm: L2 login prompt seen, logging in")
        exec_command_and_wait_for_pattern(self, 'root', ps1)
        self.log.debug("do_test_kvm: logged into L2, issuing poweroff")
        exec_command(self, 'poweroff')
        wait_for_console_pattern(self, 'reboot: Power down')
        self.log.debug("do_test_kvm: L2 powered down, waiting for L1 prompt")
        # Now wait for the host's prompt to come back
        wait_for_console_pattern(self, 'alpine:~#')
        self.log.debug("do_test_kvm: L1 prompt restored")

    def test_hv_pseries(self):
        self.require_accelerator("tcg")
        self.require_netdev('user')
        # Pin to pseries-8.2 (default CPU power9_v2.2) so that kvm-hv.ko inside
        # the L1 Alpine guest sees a POWER9 register set.  The generic 'pseries'
        # alias resolves to pseries-11.2 (power11_v2.0) which Alpine's packaged
        # qemu-system-ppc64 does not recognise, causing the L2 "Register sync
        # failed" error.  The L2 script passes '-cpu POWER9' to match.
        self.set_machine('pseries-8.2')
        self.vm.add_args("-accel", "tcg,thread=multi")
        self.vm.add_args('-device', 'nvme,serial=1234,drive=drive0')
        self.vm.add_args("-machine", "x-vof=on,cap-nested-hv=on")
        self.do_start_alpine()
        self.do_setup_kvm()
        self.do_test_kvm()
        self.do_stop_alpine()

    def test_hv_pseries_kvm(self):
        # L1 runs under TCG; L2 is launched with accel=kvm inside L1
        # (see do_test_kvm).
        self.require_accelerator("tcg")
        self.require_netdev('user')
        # Same CPU pinning rationale as test_hv_pseries above.
        self.set_machine('pseries-8.2')
        self.vm.add_args("-accel", "tcg,thread=multi")
        self.vm.add_args('-device', 'nvme,serial=1234,drive=drive0')
        self.vm.add_args("-machine", "x-vof=on,cap-nested-hv=on,cap-ccf-assist=off")
        self.do_start_alpine()
        self.do_setup_kvm()
        self.do_test_kvm()
        self.do_stop_alpine()

    def test_hv_powernv(self):
        self.require_accelerator("tcg")
        self.require_netdev('user')
        # Pin to powernv9 so the PCIe topology matches (pcie.0, pcie.2) and
        # kvm-hv.ko runs with the POWER9 CPU model.
        self.set_machine('powernv9')
        self.vm.add_args("-accel", "tcg,thread=multi")
        self.vm.add_args('-device', 'nvme,bus=pcie.2,addr=0x0,serial=1234,drive=drive0',
                         '-device', 'e1000e,netdev=net0,mac=C0:FF:EE:00:00:02,bus=pcie.0,addr=0x0',
                         '-netdev', 'user,id=net0,hostfwd=::20022-:22,hostname=alpine')
        self.do_start_alpine()
        self.do_setup_kvm()
        self.do_test_kvm()
        self.do_test_kvm(True)
        self.do_stop_alpine()

if __name__ == '__main__':
    QemuSystemTest.main()
