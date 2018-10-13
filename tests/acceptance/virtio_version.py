"""
Check compatibility of virtio device types
"""
# Copyright (c) 2018 Red Hat, Inc.
#
# Author:
#  Eduardo Habkost <ehabkost@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.
import sys
import os

sys.path.append(os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))
from qemu import QEMUMachine
from avocado_qemu import Test

# Virtio Device IDs:
VIRTIO_NET = 1
VIRTIO_BLOCK = 2
VIRTIO_CONSOLE = 3
VIRTIO_RNG = 4
VIRTIO_BALLOON = 5
VIRTIO_RPMSG = 7
VIRTIO_SCSI = 8
VIRTIO_9P = 9
VIRTIO_RPROC_SERIAL = 11
VIRTIO_CAIF = 12
VIRTIO_GPU = 16
VIRTIO_INPUT = 18
VIRTIO_VSOCK = 19
VIRTIO_CRYPTO = 20

PCI_VENDOR_ID_REDHAT_QUMRANET = 0x1af4

# Device IDs for legacy/transitional devices:
PCI_LEGACY_DEVICE_IDS = {
    VIRTIO_NET:     0x1000,
    VIRTIO_BLOCK:   0x1001,
    VIRTIO_BALLOON: 0x1002,
    VIRTIO_CONSOLE: 0x1003,
    VIRTIO_SCSI:    0x1004,
    VIRTIO_RNG:     0x1005,
    VIRTIO_9P:      0x1009,
    VIRTIO_VSOCK:   0x1012,
}

def pci_modern_device_id(virtio_devid):
    return virtio_devid + 0x1040

class VirtioVersionCheck(Test):
    """
    Check if virtio-version-specific device types result in the
    same device tree created by `disable-modern` and
    `disable-legacy`.

    :avocado: enable
    :avocado: tags=x86_64
    """

    # just in case there are failures, show larger diff:
    maxDiff = 4096

    def run_device(self, devtype, opts=None):
        """
        Run QEMU with `-device DEVTYPE`, return device info from `query-pci`
        """
        with QEMUMachine(self.qemu_bin) as vm:
            vm.set_machine('pc')
            if opts:
                devtype += ',' + opts
            vm.add_args('-device', '%s,id=devfortest' % (devtype))
            vm.add_args('-S')
            vm.launch()

            pcibuses = vm.command('query-pci')
            alldevs = [dev for bus in pcibuses for dev in bus['devices']]
            devfortest = [dev for dev in alldevs
                          if dev['qdev_id'] == 'devfortest']
            return devfortest[0]


    def assert_devids(self, dev, devid, non_transitional=False):
        self.assertEqual(dev['id']['vendor'], PCI_VENDOR_ID_REDHAT_QUMRANET)
        self.assertEqual(dev['id']['device'], devid)
        if non_transitional:
            self.assertTrue(0x1040 <= dev['id']['device'] <= 0x107f)
            self.assertGreaterEqual(dev['id']['subsystem'], 0x40)

    def check_modern_variants(self, qemu_devtype, virtio_devid):
        # Force modern mode:
        dev_modern = self.run_device(qemu_devtype,
                                     'disable-modern=off,disable-legacy=on')
        self.assert_devids(dev_modern, pci_modern_device_id(virtio_devid),
                           non_transitional=True)

        dev_1_0 = self.run_device('%s-1.0' % (qemu_devtype))
        self.assertEqual(dev_modern, dev_1_0)

    def check_all_variants(self, qemu_devtype, virtio_devid):
        self.check_modern_variants(qemu_devtype, virtio_devid)

        # Force transitional mode:
        dev_trans = self.run_device(qemu_devtype,
                                    'disable-modern=off,disable-legacy=off')
        self.assert_devids(dev_trans, PCI_LEGACY_DEVICE_IDS[virtio_devid])

        # Force legacy mode:
        dev_legacy = self.run_device(qemu_devtype,
                                     'disable-modern=on,disable-legacy=off')
        self.assert_devids(dev_legacy, PCI_LEGACY_DEVICE_IDS[virtio_devid])

        # No options: default to transitional on PC machine-type:
        dev_no_opts = self.run_device(qemu_devtype)
        self.assertEqual(dev_trans, dev_no_opts)

        # <prefix>-0.9 and <prefix>-1.0-transitional device types:
        dev_0_9 = self.run_device('%s-0.9' % (qemu_devtype))
        self.assertEqual(dev_legacy, dev_0_9)
        dev_1_0_trans = self.run_device('%s-1.0-transitional' % (qemu_devtype))
        self.assertEqual(dev_trans, dev_1_0_trans)

    def testConventionalDevs(self):
        self.check_all_variants('virtio-net-pci', VIRTIO_NET)
        # virtio-blk requires 'driver' parameter
        #self.check_all_variants('virtio-blk-pci', VIRTIO_BLOCK)
        self.check_all_variants('virtio-serial-pci', VIRTIO_CONSOLE)
        self.check_all_variants('virtio-rng-pci', VIRTIO_RNG)
        self.check_all_variants('virtio-balloon-pci', VIRTIO_BALLOON)
        self.check_all_variants('virtio-scsi-pci', VIRTIO_SCSI)

        # virtio-9p requires 'fsdev' parameter
        #self.check_all_variants('virtio-9p-pci', VIRTIO_9P)
        self.check_modern_variants('virtio-vga', VIRTIO_GPU)
        self.check_modern_variants('virtio-gpu-pci', VIRTIO_GPU)
        self.check_modern_variants('virtio-mouse-pci', VIRTIO_INPUT)
        self.check_modern_variants('virtio-tablet-pci', VIRTIO_INPUT)
        self.check_modern_variants('virtio-keyboard-pci', VIRTIO_INPUT)
