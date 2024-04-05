# Record/replay test that boots a complete Linux system via a cloud image
#
# Copyright (c) 2020 ISP RAS
#
# Author:
#  Pavel Dovgalyuk <Pavel.Dovgaluk@ispras.ru>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import os
import logging
import time
import subprocess

from avocado import skipIf, skipUnless
from avocado_qemu import BUILD_DIR
from avocado.utils import cloudinit
from avocado.utils import network
from avocado.utils import vmimage
from avocado.utils import datadrainer
from avocado.utils.path import find_command
from avocado_qemu import LinuxTest

class ReplayLinux(LinuxTest):
    """
    Boots a Linux system, checking for a successful initialization
    """

    timeout = 1800
    chksum = None
    hdd = 'ide-hd'
    cd = 'ide-cd'
    bus = 'ide'

    def setUp(self):
        # LinuxTest does many replay-incompatible things, but includes
        # useful methods. Do not setup LinuxTest here and just
        # call some functions.
        super(LinuxTest, self).setUp()
        self._set_distro()
        self.boot_path = self.download_boot()
        self.phone_server = cloudinit.PhoneHomeServer(('0.0.0.0', 0),
                                                      self.name)
        ssh_pubkey, self.ssh_key = self.set_up_existing_ssh_keys()
        self.cloudinit_path = self.prepare_cloudinit(ssh_pubkey)

    def vm_add_disk(self, vm, path, id, device):
        bus_string = ''
        if self.bus:
            bus_string = ',bus=%s.%d' % (self.bus, id,)
        vm.add_args('-drive', 'file=%s,snapshot=on,id=disk%s,if=none' % (path, id))
        vm.add_args('-drive',
            'driver=blkreplay,id=disk%s-rr,if=none,image=disk%s' % (id, id))
        vm.add_args('-device',
            '%s,drive=disk%s-rr%s' % (device, id, bus_string))

    def vm_add_cdrom(self, vm, path, id, device):
        vm.add_args('-device',
            '%s,drive=disk%s' % (device, id))
        vm.add_args('-drive', 'file=%s,id=disk%s,if=none,media=cdrom' % (path, id))

    def launch_and_wait(self, record, args, shift):
        self.require_netdev('user')
        vm = self.get_vm()
        vm.add_args('-smp', '1')
        vm.add_args('-m', '1024')
        vm.add_args('-netdev', 'user,id=vnet,hostfwd=:127.0.0.1:0-:22',
                    '-device', 'virtio-net,netdev=vnet')
        vm.add_args('-object', 'filter-replay,id=replay,netdev=vnet')
        if args:
            vm.add_args(*args)
        self.vm_add_disk(vm, self.boot_path, 0, self.hdd)
        self.vm_add_cdrom(vm, self.cloudinit_path, 1, self.cd)
        logger = logging.getLogger('replay')
        if record:
            logger.info('recording the execution...')
            mode = 'record'
        else:
            logger.info('replaying the execution...')
            mode = 'replay'
        replay_path = os.path.join(self.workdir, 'replay.bin')
        vm.add_args('-icount', 'shift=%s,rr=%s,rrfile=%s' %
                    (shift, mode, replay_path))

        start_time = time.time()

        vm.set_console()
        vm.launch()
        console_drainer = datadrainer.LineLogger(vm.console_socket.fileno(),
                                    logger=self.log.getChild('console'),
                                    stop_check=(lambda : not vm.is_running()))
        console_drainer.start()
        if record:
            while not self.phone_server.instance_phoned_back:
                self.phone_server.handle_request()
            vm.shutdown()
            logger.info('finished the recording with log size %s bytes'
                % os.path.getsize(replay_path))
            self.run_replay_dump(replay_path)
            logger.info('successfully tested replay-dump.py')
        else:
            vm.event_wait('SHUTDOWN', self.timeout)
            vm.wait()
            logger.info('successfully finished the replay')
        elapsed = time.time() - start_time
        logger.info('elapsed time %.2f sec' % elapsed)
        return elapsed

    def run_rr(self, args=None, shift=7):
        t1 = self.launch_and_wait(True, args, shift)
        t2 = self.launch_and_wait(False, args, shift)
        logger = logging.getLogger('replay')
        logger.info('replay overhead {:.2%}'.format(t2 / t1 - 1))

    def run_replay_dump(self, replay_path):
        try:
            subprocess.check_call(["./scripts/replay-dump.py",
                                   "-f", replay_path],
                                  stdout=subprocess.DEVNULL)
        except subprocess.CalledProcessError:
            self.fail('replay-dump.py failed')

@skipUnless(os.getenv('SPEED') == 'slow', 'runtime limited')
class ReplayLinuxX8664(ReplayLinux):
    """
    :avocado: tags=arch:x86_64
    :avocado: tags=accel:tcg
    """

    chksum = 'e3c1b309d9203604922d6e255c2c5d098a309c2d46215d8fc026954f3c5c27a0'

    @skipUnless(os.getenv('QEMU_TEST_FLAKY_TESTS'), 'Test is unstable')
    def test_pc_i440fx(self):
        """
        :avocado: tags=machine:pc
        """
        self.run_rr(shift=1)

    @skipUnless(os.getenv('QEMU_TEST_FLAKY_TESTS'), 'Test is unstable')
    def test_pc_q35(self):
        """
        :avocado: tags=machine:q35
        """
        self.run_rr(shift=3)

@skipUnless(os.getenv('SPEED') == 'slow', 'runtime limited')
class ReplayLinuxX8664Virtio(ReplayLinux):
    """
    :avocado: tags=arch:x86_64
    :avocado: tags=virtio
    :avocado: tags=accel:tcg
    """

    hdd = 'virtio-blk-pci'
    cd = 'virtio-blk-pci'
    bus = None

    chksum = 'e3c1b309d9203604922d6e255c2c5d098a309c2d46215d8fc026954f3c5c27a0'

    @skipUnless(os.getenv('QEMU_TEST_FLAKY_TESTS'), 'Test is unstable')
    def test_pc_i440fx(self):
        """
        :avocado: tags=machine:pc
        """
        self.run_rr(shift=1)

    def test_pc_q35(self):
        """
        :avocado: tags=machine:q35
        """
        self.run_rr(shift=3)

@skipUnless(os.getenv('SPEED') == 'slow', 'runtime limited')
class ReplayLinuxAarch64(ReplayLinux):
    """
    :avocado: tags=accel:tcg
    :avocado: tags=arch:aarch64
    :avocado: tags=machine:virt
    :avocado: tags=cpu:max
    """

    chksum = '1e18d9c0cf734940c4b5d5ec592facaed2af0ad0329383d5639c997fdf16fe49'

    hdd = 'virtio-blk-device'
    cd = 'virtio-blk-device'
    bus = None

    def get_common_args(self):
        return ('-bios',
                os.path.join(BUILD_DIR, 'pc-bios', 'edk2-aarch64-code.fd'),
                "-cpu", "max,lpa2=off",
                '-device', 'virtio-rng-pci,rng=rng0',
                '-object', 'rng-builtin,id=rng0')

    @skipUnless(os.getenv('QEMU_TEST_FLAKY_TESTS'), 'Test is unstable')
    def test_virt_gicv2(self):
        """
        :avocado: tags=machine:gic-version=2
        """

        self.run_rr(shift=3,
                    args=(*self.get_common_args(),
                          "-machine", "virt,gic-version=2"))

    def test_virt_gicv3(self):
        """
        :avocado: tags=machine:gic-version=3
        """

        self.run_rr(shift=3,
                    args=(*self.get_common_args(),
                          "-machine", "virt,gic-version=3"))

# ppc64 pseries test.
#
# This machine tends to fail replay and hang very close to the end of the
# trace, with missing events, which is still an open issue.
#
# spapr-scsi IO driven by SLOF/grub is extremely slow in record/replay mode,
# so jump through some hoops to boot the kernel directly. With this, the test
# runs in about 5 minutes (modulo hang), which suggests other machines may
# have similar issues and could benefit from bypassing bootloaders.
#
ppc_deps = ["guestfish"] # dependent tools needed in the test setup/box.

def which(tool):
    """ looks up the full path for @tool, returns None if not found
        or if @tool does not have executable permissions.
    """
    paths=os.getenv('PATH')
    for p in paths.split(os.path.pathsep):
        p = os.path.join(p, tool)
        if os.path.exists(p) and os.access(p, os.X_OK):
            return p
    return None

def ppc_missing_deps():
    """ returns True if any of the test dependent tools are absent.
    """
    for dep in ppc_deps:
        if which(dep) is None:
            return True
    return False

@skipUnless(os.getenv('SPEED') == 'slow', 'runtime limited')
class ReplayLinuxPPC64(ReplayLinux):
    """
    :avocado: tags=arch:ppc64
    :avocado: tags=accel:tcg
    """

    hdd = 'virtio-blk-pci'
    cd = 'scsi-cd'
    bus = None

    def setUp(self):
        super().setUp()

        if not ppc_missing_deps():
            # kernel, initramfs, and kernel cmdline are all taken by hand from
            # the Fedora image.
            self.kernel="vmlinuz-5.3.7-301.fc31.ppc64le"
            self.initramfs="initramfs-5.3.7-301.fc31.ppc64le.img"
            cmd = "guestfish --ro -a %s run " ": mount /dev/sda2 / " ": copy-out /boot/%s %s " ": copy-out /boot/%s %s " % (self.boot_path, self.kernel, self.workdir, self.initramfs, self.workdir)
            subprocess.run(cmd.split())

    @skipIf(ppc_missing_deps(), 'dependencies (%s) not installed' % ','.join(ppc_deps))
    def test_pseries(self):
        """
        :avocado: tags=machine:pseries
        """
        kernel=os.path.normpath(os.path.join(self.workdir, self.kernel))
        initramfs=os.path.normpath(os.path.join(self.workdir, self.initramfs))
        cmdline="root=UUID=8a409ee6-3cb3-4b06-a266-39e2dae3e5fa ro no_timer_check net.fnames=0 console=tty1 console=ttyS0,115200n8"
        self.run_rr(shift=3, args=("-device", "spapr-vscsi",
                                   "-machine", "x-vof=on",
                                   "-kernel", kernel,
                                   "-initrd", initramfs,
                                   "-append", cmdline))
