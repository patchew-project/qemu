# tests for QEMU's vfio subsystem
#
# Copyright (c) 2022 Yandex N.V.
#
# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.

from avocado.utils import wait
from avocado import skipUnless
from avocado_qemu import QemuSystemTest
from avocado_qemu import run_cmd
import os
import sys
import subprocess
from fcntl import ioctl
from ctypes import *
import struct


VFIO_CMD_PREFIX = ord(';') << (4*2)
VFIO_GET_API_VERSION = VFIO_CMD_PREFIX | 100
VFIO_CHECK_EXTENSION = VFIO_CMD_PREFIX | 101
VFIO_SET_IOMMU = VFIO_CMD_PREFIX | 102
VFIO_GROUP_GET_STATUS = VFIO_CMD_PREFIX | 103
VFIO_GROUP_SET_CONTAINER = VFIO_CMD_PREFIX | 104
VFIO_GROUP_GET_DEVICE_FD = VFIO_CMD_PREFIX | 106

VFIO_TYPE1_IOMMU = 1
VFIO_SPAPR_TCE_IOMMU = 2
VFIO_TYPE1v2_IOMMU = 3
VFIO_SPAPR_TCE_v2_IOMMU = 7

VFIO_API_VERSION = 0
VFIO_TYPE1_IOMMU = 1
PCI_VENDOR_ID=0x1b36
PCI_DEV_ID=0x0005

class vfio_group_status(Structure):
    _fields_ = [("argsz", c_uint32),
                ("flags", c_uint32)]

class vfio_container(object):
    def open(self):
        self.container_fd = os.open("/dev/vfio/vfio", os.O_RDWR)
        if ioctl(self.container_fd, VFIO_GET_API_VERSION) != VFIO_API_VERSION:
            raise Exception("VFIO_GET_API_VERSION: unexpected vfio api version")
        iommu_types = [ VFIO_TYPE1v2_IOMMU, VFIO_TYPE1_IOMMU,
                          VFIO_SPAPR_TCE_v2_IOMMU, VFIO_SPAPR_TCE_IOMMU ];
        for iommu_type in iommu_types:
            if ioctl(self.container_fd, VFIO_CHECK_EXTENSION, iommu_type):
                self.iommu_type = iommu_type
        if not self.iommu_type:
            raise Exception("No available IOMMU models");

    def set_iommu(self):
            ioctl(self.container_fd, VFIO_SET_IOMMU, self.iommu_type);

class vfio_group(object):
    def __init__(self, container, group_num):
        self.ct = container
        self.group_num = group_num
    def open(self):
        self.group_fd = os.open("/dev/vfio/%d" % self.group_num, os.O_RDWR)
        status = vfio_group_status(0, 0)
        ioctl(self.group_fd, VFIO_GROUP_GET_STATUS, pointer(status))
        ioctl(self.group_fd, VFIO_GROUP_SET_CONTAINER,
              c_int(self.ct.container_fd));

class vfio_device(object):
    def __init__(self, dev, group):
        self.dev = dev
        self.group = group

def pci_testdev_exists():
    for dev in next(os.walk('/sys/bus/pci/devices'))[1]:
        with open("/sys/bus/pci/devices/%s/vendor" % dev) as f:
            if f.read() != '0x%x\n' % PCI_VENDOR_ID:
                continue
        with open("/sys/bus/pci/devices/%s/device" % dev) as f:
            if f.read() != '0x%.4x\n' % PCI_DEV_ID:
                continue
        return True
    return False

class VFIOIOMMUTest(QemuSystemTest):
    devices = []
    groups = []
    timeout = 900
    ct = vfio_container()

    def add_group(self, dev):
        tmp = os.readlink("/sys/bus/pci/devices/%s/iommu_group" % dev)
        group_num = int(tmp.split('/')[-1])

        for g in self.groups:
            if g.group_num == group_num:
                return g
        group = vfio_group(self.ct, group_num)
        self.groups.append(group)
        return group

    def setUp(self):
        super().setUp()
        run_cmd(('modprobe', 'vfio-pci'))
        try:
            f = open("/sys/bus/pci/drivers/vfio-pci/new_id", "a")
            f.write("%x %x" % (PCI_VENDOR_ID, PCI_DEV_ID))
        except PermissionError:
            pass
        except FileExistsError:
            pass
        for dev in next(os.walk('/sys/bus/pci/devices'))[1]:
            with open("/sys/bus/pci/devices/%s/vendor" % dev) as f:
                if f.read() != '0x%x\n' % PCI_VENDOR_ID:
                    continue
            with open("/sys/bus/pci/devices/%s/device" % dev) as f:
                if f.read() != '0x%.4x\n' % PCI_DEV_ID:
                    continue

            self.devices.append(vfio_device(dev, self.add_group(dev)))

    def open_dev_fds(self):
        self.ct.open()
        for group in self.groups:
            group.open()
        self.ct.set_iommu()

    def close_fds(self):
        for group in self.groups:
            os.close(group.group_fd)
        os.close(self.ct.container_fd)

    def hotplug_devices(self, vm):
        vm._qmp.send_fd_scm(self.devices[0].group.ct.container_fd)
        vm.command("getfd", fdname="ct")
        vm.command("object-add", qom_type="vfio-container", id="ct", fd="ct")

        for group in self.groups:
            vm._qmp.send_fd_scm(group.group_fd)
            vm.command("getfd", fdname="group_fd")
            vm.command("object-add", qom_type="vfio-group",
                       id="group%d" % group.group_num,
                       fd="group_fd", container="ct")

        for i in range(len(self.devices)):
            vm.command("device_add", driver="vfio-pci",
                       host=self.devices[i].dev, id="dev%d" % i,
                       group="group%d" % self.devices[i].group.group_num)

    @skipUnless(pci_testdev_exists(), "no pci-testdev found")
    def test_vfio(self):
        self.open_dev_fds()
        self.vm.add_args('-nodefaults')
        self.vm.launch()
        self.hotplug_devices(self.vm)
        self.close_fds()
