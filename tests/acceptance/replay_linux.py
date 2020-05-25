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

from avocado_qemu import Test, BUILD_DIR

from avocado.utils import cloudinit
from avocado.utils import network
from avocado.utils import vmimage
from avocado.utils import datadrainer
from avocado.utils.path import find_command

class ReplayLinux(Test):
    """
    Boots a Linux system, checking for a successful initialization
    """

    timeout = 1800
    chksum = None
    hdd = 'ide-hd'
    cd = 'ide-cd'
    bus = ''

    def setUp(self):
        super(ReplayLinux, self).setUp()
        self.prepare_boot()
        self.prepare_cloudinit()

    def vm_add_disk(self, vm, path, id, device):
        bus_string = ''
        if self.bus != '':
            bus_string = ',bus=%s.%d' % (self.bus, id,)
        vm.add_args('-drive', 'file=%s,snapshot,id=disk%s,if=none' % (path, id))
        vm.add_args('-drive', 'driver=blkreplay,id=disk%s-rr,if=none,image=disk%s' % (id, id))
        vm.add_args('-device', '%s,drive=disk%s-rr%s' % (device, id, bus_string))

    def prepare_boot(self):
        self.log.debug('Looking for and selecting a qemu-img binary to be '
                       'used to create the bootable snapshot image')
        # If qemu-img has been built, use it, otherwise the system wide one
        # will be used.  If none is available, the test will cancel.
        qemu_img = os.path.join(BUILD_DIR, 'qemu-img')
        if not os.path.exists(qemu_img):
            qemu_img = find_command('qemu-img', False)
        if qemu_img is False:
            self.cancel('Could not find "qemu-img", which is required to '
                        'create the bootable image')
        vmimage.QEMU_IMG = qemu_img

        self.log.info('Downloading/preparing boot image')
        # Fedora 31 only provides ppc64le images
        image_arch = self.arch
        if image_arch == 'ppc64':
            image_arch = 'ppc64le'
        try:
            self.boot = vmimage.get(
                'fedora', arch=image_arch, version='31',
                checksum=self.chksum,
                algorithm='sha256',
                cache_dir=self.cache_dirs[0],
                snapshot_dir=self.workdir)
        except:
            self.cancel('Failed to download/prepare boot image')

    def prepare_cloudinit(self):
        self.log.info('Preparing cloudinit image')
        try:
            self.cloudinit_iso = os.path.join(self.workdir, 'cloudinit.iso')
            self.phone_home_port = network.find_free_port()
            cloudinit.iso(self.cloudinit_iso, self.name,
                          username='root',
                          password='password',
                          # QEMU's hard coded usermode router address
                          phone_home_host='10.0.2.2',
                          phone_home_port=self.phone_home_port)
        except Exception:
            self.cancel('Failed to prepared cloudinit image')

    def launch_and_wait(self, record, args, shift):
        vm = self.get_vm()
        vm.add_args('-smp', '1')
        vm.add_args('-m', '1024')
        vm.add_args('-object', 'filter-replay,id=replay,netdev=hub0port0')
        vm.add_args(*args)
        self.vm_add_disk(vm, self.boot.path, 0, self.hdd)
        self.vm_add_disk(vm, self.cloudinit_iso, 1, self.cd)
        if record:
            mode = 'record'
        else:
            mode = 'replay'
        vm.add_args('-icount', 'shift=%s,rr=%s,rrfile=%s' %
                    (shift, mode, os.path.join(self.workdir, 'replay.bin')))

        vm.set_console()
        vm.launch()
        console_drainer = datadrainer.LineLogger(vm.console_socket.fileno(),
                                                 logger=self.log.getChild('console'),
                                                 stop_check=(lambda : not vm.is_running()))
        console_drainer.start()
        if record:
            self.log.info('VM launched, waiting for boot confirmation from guest')
            cloudinit.wait_for_phone_home(('0.0.0.0', self.phone_home_port), self.name)
            vm.shutdown()
        else:
            self.log.info('VM launched, waiting the recorded execution to be replayed')
            vm.wait()

    def run_rr(self, args=(), shift=7):
        self.launch_and_wait(True, args, shift)
        self.launch_and_wait(False, args, shift)

class ReplayLinuxX8664(ReplayLinux):
    """
    :avocado: tags=arch:x86_64
    """
    bus = 'ide'

    chksum = 'e3c1b309d9203604922d6e255c2c5d098a309c2d46215d8fc026954f3c5c27a0'

    def test_pc_i440fx(self):
        """
        :avocado: tags=machine:pc
        :avocado: tags=accel:tcg
        """
        self.run_rr(shift=1)

    def test_pc_q35(self):
        """
        :avocado: tags=machine:q35
        :avocado: tags=accel:tcg
        """
        self.run_rr(shift=3)
