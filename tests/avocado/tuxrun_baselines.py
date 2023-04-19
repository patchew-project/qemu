# Functional test that boots known good tuxboot images the same way
# that tuxrun (www.tuxrun.org) does. This tool is used by things like
# the LKFT project to run regression tests on kernels.
#
# Copyright (c) 2023 Linaro Ltd.
#
# Author:
#  Alex Bennée <alex.bennee@linaro.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import time
import string
import random

from avocado import skip, skipIf
from avocado_qemu import QemuSystemTest
from avocado_qemu import exec_command, exec_command_and_wait_for_pattern
from avocado_qemu import wait_for_console_pattern
from avocado.utils import process
from avocado.utils.path import find_command

class TuxRunBaselineTest(QemuSystemTest):
    """
    :avocado: tags=accel:tcg
    """

    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0'
    # Tests are ~10-40s, allow for --debug/--enable-gcov overhead
    timeout = 100

    def get_tag(self, tagname, default=None):
        """
        Get the metadata tag or return the default.
        """
        utag = self._get_unique_tag_val(tagname)
        print(f"{tagname}/{default} -> {utag}")
        if utag:
            return utag

        return default

    def setUp(self):
        super().setUp()

        # We need zstd for all the tuxrun tests
        # See https://github.com/avocado-framework/avocado/issues/5609
        zstd = find_command('zstd', False)
        if zstd is False:
            self.cancel('Could not find "zstd", which is required to '
                        'decompress rootfs')
        self.zstd = zstd

        # Process the TuxRun specific tags, most machines work with
        # reasonable defaults but we sometimes need to tweak the
        # config. To avoid open coding everything we store all these
        # details in the metadata for each test.

        # The tuxboot tag matches the root directory
        self.tuxboot = self.get_tag('tuxboot')

        # Most Linux's use ttyS0 for their serial port
        self.console = self.get_tag('console', "ttyS0")

        # Does the machine shutdown QEMU nicely on "halt"
        self.shutdown = self.get_tag('shutdown')

        # The name of the kernel Image file
        self.image = self.get_tag('image', "Image")

        self.root = self.get_tag('root', "vda")

        # Occasionally we need extra devices to hook things up
        self.extradev = self.get_tag('extradev')

    def wait_for_console_pattern(self, success_message, vm=None):
        wait_for_console_pattern(self, success_message,
                                 failure_message='Kernel panic - not syncing',
                                 vm=vm)

    def fetch_tuxrun_assets(self, dt=None):
        """
        Fetch the TuxBoot assets. They are stored in a standard way so we
        use the per-test tags to fetch details.
        """
        base_url = f"https://storage.tuxboot.com/{self.tuxboot}/"
        kernel_image =  self.fetch_asset(base_url + self.image)
        disk_image_zst = self.fetch_asset(base_url + "rootfs.ext4.zst")

        cmd = f"{self.zstd} -d {disk_image_zst} -o {self.workdir}/rootfs.ext4"
        process.run(cmd)

        if dt:
            dtb = self.fetch_asset(base_url + dt)
        else:
            dtb = None

        return (kernel_image, self.workdir + "/rootfs.ext4", dtb)

    def prepare_run(self, kernel, disk, drive, dtb=None, console_index=0):
        """
        Setup to run and add the common parameters to the system
        """
        self.vm.set_console(console_index=console_index)

        # all block devices are raw ext4's
        blockdev = "driver=raw,file.driver=file," \
            + f"file.filename={disk},node-name=hd0"

        kcmd_line = self.KERNEL_COMMON_COMMAND_LINE
        kcmd_line += f" root=/dev/{self.root}"
        kcmd_line += f" console={self.console}"

        self.vm.add_args('-kernel', kernel,
                         '-append', kcmd_line,
                         '-blockdev', blockdev)

        # Sometimes we need extra devices attached
        if self.extradev:
            self.vm.add_args('-device', self.extradev)

        self.vm.add_args('-device',
                         f"{drive},drive=hd0")

        # Some machines need an explicit DTB
        if dtb:
            self.vm.add_args('-dtb', dtb)

    def run_tuxtest_tests(self, haltmsg):
        """
        Wait for the system to boot up, wait for the login prompt and
        then do a few things on the console. Trigger a shutdown and
        wait to exit cleanly.
        """
        self.wait_for_console_pattern("Welcome to TuxTest")
        time.sleep(0.2)
        exec_command(self, 'root')
        time.sleep(0.2)
        exec_command(self, 'cat /proc/interrupts')
        time.sleep(0.1)
        exec_command(self, 'cat /proc/self/maps')
        time.sleep(0.1)
        exec_command(self, 'uname -a')
        time.sleep(0.1)
        exec_command_and_wait_for_pattern(self, 'halt', haltmsg)

        # Wait for VM to shut down gracefully if it can
        if self.shutdown == "nowait":
            self.vm.shutdown()
        else:
            self.vm.wait()

    def common_tuxrun(self, dt=None,
                      drive="virtio-blk-device",
                      haltmsg="reboot: System halted",
                      console_index=0):
        """
        Common path for LKFT tests. Unless we need to do something
        special with the command line we can process most things using
        the tag metadata.
        """
        (kernel, disk, dtb) = self.fetch_tuxrun_assets(dt)

        self.prepare_run(kernel, disk, drive, dtb, console_index)
        self.vm.launch()
        self.run_tuxtest_tests(haltmsg)

    #
    # The tests themselves. The configuration is derived from how
    # tuxrun invokes qemu (with minor tweaks like using -blockdev
    # consistently). The tuxrun equivalent is something like:
    #
    # tuxrun --device qemu-{ARCH} \
    #        --kernel https://storage.tuxboot.com/{TUXBOOT}/{IMAGE}
    #

    def test_arm64(self):
        """
        :avocado: tags=arch:aarch64
        :avocado: tags=cpu:cortex-a57
        :avocado: tags=machine:virt
        :avocado: tags=tuxboot:arm64
        :avocado: tags=console:ttyAMA0
        :avocado: tags=shutdown:nowait
        """
        self.common_tuxrun()

    def test_arm64be(self):
        """
        :avocado: tags=arch:aarch64
        :avocado: tags=cpu:cortex-a57
        :avocado: tags=endian:big
        :avocado: tags=machine:virt
        :avocado: tags=tuxboot:arm64be
        :avocado: tags=console:ttyAMA0
        :avocado: tags=shutdown:nowait
        """
        self.common_tuxrun()

    def test_armv5(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=cpu:arm926
        :avocado: tags=machine:versatilepb
        :avocado: tags=tuxboot:armv5
        :avocado: tags=image:zImage
        :avocado: tags=console:ttyAMA0
        :avocado: tags=shutdown:nowait
        """
        self.common_tuxrun(drive="virtio-blk-pci",
                           dt="versatile-pb.dtb")

    def test_armv7(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=cpu:cortex-a15
        :avocado: tags=machine:virt
        :avocado: tags=tuxboot:armv7
        :avocado: tags=image:zImage
        :avocado: tags=console:ttyAMA0
        :avocado: tags=shutdown:nowait
        """
        self.common_tuxrun()

    def test_armv7be(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=cpu:cortex-a15
        :avocado: tags=endian:big
        :avocado: tags=machine:virt
        :avocado: tags=tuxboot:armv7be
        :avocado: tags=image:zImage
        :avocado: tags=console:ttyAMA0
        :avocado: tags=shutdown:nowait
        """
        self.common_tuxrun()

    def test_i386(self):
        """
        :avocado: tags=arch:i386
        :avocado: tags=cpu:coreduo
        :avocado: tags=machine:q35
        :avocado: tags=tuxboot:i386
        :avocado: tags=image:bzImage
        :avocado: tags=shutdown:nowait
        """
        self.common_tuxrun(drive="virtio-blk-pci")

    def test_mips32(self):
        """
        :avocado: tags=arch:mips
        :avocado: tags=machine:malta
        :avocado: tags=cpu:mips32r6-generic
        :avocado: tags=endian:big
        :avocado: tags=tuxboot:mips32
        :avocado: tags=image:vmlinux
        :avocado: tags=root:sda
        :avocado: tags=shutdown:nowait
        """
        self.common_tuxrun(drive="driver=ide-hd,bus=ide.0,unit=0")

    def test_mips32el(self):
        """
        :avocado: tags=arch:mipsel
        :avocado: tags=machine:malta
        :avocado: tags=cpu:mips32r6-generic
        :avocado: tags=tuxboot:mips32el
        :avocado: tags=image:vmlinux
        :avocado: tags=root:sda
        :avocado: tags=shutdown:nowait
        """
        self.common_tuxrun(drive="driver=ide-hd,bus=ide.0,unit=0")

    @skip("QEMU currently broken") # regression against stable QEMU
    def test_mips64(self):
        """
        :avocado: tags=arch:mips64
        :avocado: tags=machine:malta
        :avocado: tags=tuxboot:mips64
        :avocado: tags=endian:big
        :avocado: tags=image:vmlinux
        :avocado: tags=root:sda
        :avocado: tags=shutdown:nowait
        """
        self.common_tuxrun(drive="driver=ide-hd,bus=ide.0,unit=0")

    def test_mips64el(self):
        """
        :avocado: tags=arch:mips64el
        :avocado: tags=machine:malta
        :avocado: tags=tuxboot:mips64el
        :avocado: tags=image:vmlinux
        :avocado: tags=root:sda
        :avocado: tags=shutdown:nowait
        """
        self.common_tuxrun(drive="driver=ide-hd,bus=ide.0,unit=0")

    def test_ppc32(self):
        """
        :avocado: tags=arch:ppc
        :avocado: tags=machine:ppce500
        :avocado: tags=cpu:e500mc
        :avocado: tags=tuxboot:ppc32
        :avocado: tags=image:uImage
        :avocado: tags=shutdown:nowait
        """
        self.common_tuxrun(drive="virtio-blk-pci")

    def test_ppc64(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:pseries
        :avocado: tags=cpu:POWER10
        :avocado: tags=endian:big
        :avocado: tags=console:hvc0
        :avocado: tags=tuxboot:ppc64
        :avocado: tags=image:vmlinux
        :avocado: tags=extradev:driver=spapr-vscsi
        :avocado: tags=root:sda
        """
        # Generate a random string
        res = ''.join(random.choices(string.ascii_lowercase +
                                     string.digits, k=8))

        # create qcow2 image to be used later.
        process.run('./qemu-img create -f qcow2 '
                    '/tmp/tuxrun_baselines_ppc64_' + str(res) +
                    '.qcow2 1G')

        # add device args to command line.
        self.vm.add_args('-netdev', 'user,id=vnet,hostfwd=:127.0.0.1:0-:22',
                         '-device', 'virtio-net,netdev=vnet')
        self.vm.add_args('-netdev', '{"type":"user","id":"hostnet0"}',
                         '-device', '{"driver":"virtio-net-pci","netdev":'
                         '"hostnet0","id":"net0","mac":"52:54:00:4c:e3:86",'
                         '"bus":"pci.0","addr":"0x9"}')
        self.vm.add_args('-device', '{"driver":"qemu-xhci","p2":15,"p3":15,'
                         '"id":"usb","bus":"pci.0","addr":"0x2"}')
        self.vm.add_args('-device', '{"driver":"virtio-scsi-pci","id":"scsi0"'
                         ',"bus":"pci.0","addr":"0x3"}')
        self.vm.add_args('-device', '{"driver":"virtio-serial-pci","id":'
                         '"virtio-serial0","bus":"pci.0","addr":"0x4"}')
        self.vm.add_args('-device', '{"driver":"scsi-cd","bus":"scsi0.0"'
                         ',"channel":0,"scsi-id":0,"lun":0,"device_id":'
                         '"drive-scsi0-0-0-0","id":"scsi0-0-0-0"}')
        self.vm.add_args('-device', '{"driver":"virtio-balloon-pci",'
                         '"id":"balloon0","bus":"pci.0","addr":"0x6"}')
        self.vm.add_args('-audiodev', '{"id":"audio1","driver":"none"}')
        self.vm.add_args('-device', '{"driver":"usb-tablet","id":"input0"'
                         ',"bus":"usb.0","port":"1"}')
        self.vm.add_args('-device', '{"driver":"usb-kbd","id":"input1"'
                         ',"bus":"usb.0","port":"2"}')
        self.vm.add_args('-device', '{"driver":"VGA","id":"video0",'
                         '"vgamem_mb":16,"bus":"pci.0","addr":"0x7"}')
        self.vm.add_args('-object', '{"qom-type":"rng-random","id":"objrng0"'
                         ',"filename":"/dev/urandom"}',
                         '-device', '{"driver":"virtio-rng-pci","rng":"objrng0"'
                         ',"id":"rng0","bus":"pci.0","addr":"0x8"}')
        self.vm.add_args('-object', '{"qom-type":"cryptodev-backend-builtin",'
                         '"id":"objcrypto0","queues":1}',
                         '-device', '{"driver":"virtio-crypto-pci",'
                         '"cryptodev":"objcrypto0","id":"crypto0","bus"'
                         ':"pci.0","addr":"0xa"}')
        self.vm.add_args('-device', '{"driver":"spapr-pci-host-bridge"'
                         ',"index":1,"id":"pci.1"}')
        self.vm.add_args('-device', '{"driver":"spapr-vscsi","id":"scsi1"'
                         ',"reg":12288}')
        self.vm.add_args('-m', '2G,slots=32,maxmem=4G',
                         '-object', 'memory-backend-ram,id=ram1,size=1G',
                         '-device', 'pc-dimm,id=dimm1,memdev=ram1')
        self.vm.add_args('-drive', 'file=/tmp/tuxrun_baselines_ppc64_' +
                         str(res) + '.qcow2,format=qcow2,if=none,id='
                         'drive-virtio-disk1',
                         '-device', 'virtio-blk-pci,scsi=off,bus=pci.0,'
                         'addr=0xb,drive=drive-virtio-disk1,id=virtio-disk1'
                         ',bootindex=2')
        self.common_tuxrun(drive="scsi-hd")

        # remove qcow2 image
        process.run('rm /tmp/tuxrun_baselines_ppc64_' + str(res) + '.qcow2')

    def test_ppc64le(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:pseries
        :avocado: tags=cpu:POWER10
        :avocado: tags=console:hvc0
        :avocado: tags=tuxboot:ppc64le
        :avocado: tags=image:vmlinux
        :avocado: tags=extradev:driver=spapr-vscsi
        :avocado: tags=root:sda
        """
        # Generate a random string
        res = ''.join(random.choices(string.ascii_lowercase +
                                     string.digits, k=8))

        # create qcow2 image to be used later.
        process.run('./qemu-img create -f qcow2 '
                    '/tmp/tuxrun_baselines_ppc64le_' + str(res) +
                    '.qcow2 1G')

        # add device args to command line.
        self.vm.add_args('-netdev', 'user,id=vnet,hostfwd=:127.0.0.1:0-:22',
                         '-device', 'virtio-net,netdev=vnet')
        self.vm.add_args('-netdev', '{"type":"user","id":"hostnet0"}',
                         '-device', '{"driver":"virtio-net-pci","netdev":'
                         '"hostnet0","id":"net0","mac":"52:54:00:4c:e3:86",'
                         '"bus":"pci.0","addr":"0x9"}')
        self.vm.add_args('-device', '{"driver":"qemu-xhci","p2":15,"p3":15,'
                         '"id":"usb","bus":"pci.0","addr":"0x2"}')
        self.vm.add_args('-device', '{"driver":"virtio-scsi-pci","id":"scsi0"'
                         ',"bus":"pci.0","addr":"0x3"}')
        self.vm.add_args('-device', '{"driver":"virtio-serial-pci","id":'
                         '"virtio-serial0","bus":"pci.0","addr":"0x4"}')
        self.vm.add_args('-device', '{"driver":"scsi-cd","bus":"scsi0.0"'
                         ',"channel":0,"scsi-id":0,"lun":0,"device_id":'
                         '"drive-scsi0-0-0-0","id":"scsi0-0-0-0"}')
        self.vm.add_args('-device', '{"driver":"virtio-balloon-pci",'
                         '"id":"balloon0","bus":"pci.0","addr":"0x6"}')
        self.vm.add_args('-audiodev', '{"id":"audio1","driver":"none"}')
        self.vm.add_args('-device', '{"driver":"usb-tablet","id":"input0"'
                         ',"bus":"usb.0","port":"1"}')
        self.vm.add_args('-device', '{"driver":"usb-kbd","id":"input1"'
                         ',"bus":"usb.0","port":"2"}')
        self.vm.add_args('-device', '{"driver":"VGA","id":"video0",'
                         '"vgamem_mb":16,"bus":"pci.0","addr":"0x7"}')
        self.vm.add_args('-object', '{"qom-type":"rng-random","id":"objrng0"'
                         ',"filename":"/dev/urandom"}',
                         '-device', '{"driver":"virtio-rng-pci","rng":"objrng0"'
                         ',"id":"rng0","bus":"pci.0","addr":"0x8"}')
        self.vm.add_args('-object', '{"qom-type":"cryptodev-backend-builtin",'
                         '"id":"objcrypto0","queues":1}',
                         '-device', '{"driver":"virtio-crypto-pci",'
                         '"cryptodev":"objcrypto0","id":"crypto0","bus"'
                         ':"pci.0","addr":"0xa"}')
        self.vm.add_args('-device', '{"driver":"spapr-pci-host-bridge"'
                         ',"index":1,"id":"pci.1"}')
        self.vm.add_args('-device', '{"driver":"spapr-vscsi","id":"scsi1"'
                         ',"reg":12288}')
        self.vm.add_args('-m', '2G,slots=32,maxmem=4G',
                         '-object', 'memory-backend-ram,id=ram1,size=1G',
                         '-device', 'pc-dimm,id=dimm1,memdev=ram1')
        self.vm.add_args('-drive', 'file=/tmp/tuxrun_baselines_ppc64le_' +
                         str(res) + '.qcow2,format=qcow2,if=none,'
                         'id=drive-virtio-disk1',
                         '-device', 'virtio-blk-pci,scsi=off,bus=pci.0,'
                         'addr=0xb,drive=drive-virtio-disk1,id=virtio-disk1'
                         ',bootindex=2')
        self.common_tuxrun(drive="scsi-hd")

        # remove qcow2 image
        process.run('rm /tmp/tuxrun_baselines_ppc64le_' + str(res) + '.qcow2')

    def test_riscv32(self):
        """
        :avocado: tags=arch:riscv32
        :avocado: tags=machine:virt
        :avocado: tags=tuxboot:riscv32
        """
        self.common_tuxrun()

    def test_riscv64(self):
        """
        :avocado: tags=arch:riscv64
        :avocado: tags=machine:virt
        :avocado: tags=tuxboot:riscv64
        """
        self.common_tuxrun()

    def test_s390(self):
        """
        :avocado: tags=arch:s390x
        :avocado: tags=endian:big
        :avocado: tags=tuxboot:s390
        :avocado: tags=image:bzImage
        :avocado: tags=shutdown:nowait
        """
        self.common_tuxrun(drive="virtio-blk-ccw",
                           haltmsg="Requesting system halt")

    # Note: some segfaults caused by unaligned userspace access
    @skipIf(os.getenv('GITLAB_CI'), 'Skipping unstable test on GitLab')
    def test_sh4(self):
        """
        :avocado: tags=arch:sh4
        :avocado: tags=machine:r2d
        :avocado: tags=cpu:sh7785
        :avocado: tags=tuxboot:sh4
        :avocado: tags=image:zImage
        :avocado: tags=root:sda
        :avocado: tags=console:ttySC1
        """
        # The test is currently too unstable to do much in userspace
        # so we skip common_tuxrun and do a minimal boot and shutdown.
        (kernel, disk, dtb) = self.fetch_tuxrun_assets()

        # the console comes on the second serial port
        self.prepare_run(kernel, disk,
                         "driver=ide-hd,bus=ide.0,unit=0",
                         console_index=1)
        self.vm.launch()

        self.wait_for_console_pattern("Welcome to TuxTest")
        time.sleep(0.1)
        exec_command(self, 'root')
        time.sleep(0.1)
        exec_command_and_wait_for_pattern(self, 'halt',
                                          "reboot: System halted")

    def test_sparc64(self):
        """
        :avocado: tags=arch:sparc64
        :avocado: tags=tuxboot:sparc64
        :avocado: tags=image:vmlinux
        :avocado: tags=root:sda
        :avocado: tags=shutdown:nowait
        """
        self.common_tuxrun(drive="driver=ide-hd,bus=ide.0,unit=0")

    def test_x86_64(self):
        """
        :avocado: tags=arch:x86_64
        :avocado: tags=machine:q35
        :avocado: tags=cpu:Nehalem
        :avocado: tags=tuxboot:x86_64
        :avocado: tags=image:bzImage
        :avocado: tags=root:sda
        :avocado: tags=shutdown:nowait
        """
        self.common_tuxrun(drive="driver=ide-hd,bus=ide.0,unit=0")
