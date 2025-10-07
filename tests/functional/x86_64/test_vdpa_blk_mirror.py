#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Copyright Red Hat, Inc.
#
# vdpa-blk mirror blockjob tests


import glob
import os
import subprocess
from qemu_test import LinuxKernelTest, Asset
from qemu_test import exec_command_and_wait_for_pattern


def run(cmd: str) -> None:
    '''
    Run a shell command without capturing stdout/stderr and raise
    subprocess.CalledProcessError on failure.
    '''
    subprocess.check_call(cmd, shell=True,
                          stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL)


class VdpaBlk(LinuxKernelTest):

    KERNEL_COMMAND_LINE = 'printk.time=0 console=ttyS0 rd.rescue'
    ASSET_KERNEL = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora/linux/releases'
         '/31/Server/x86_64/os/images/pxeboot/vmlinuz'),
        'd4738d03dbbe083ca610d0821d0a8f1488bebbdccef54ce33e3adb35fda00129')
    ASSET_INITRD = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora/linux/releases'
         '/31/Server/x86_64/os/images/pxeboot/initrd.img'),
        '277cd6c7adf77c7e63d73bbb2cded8ef9e2d3a2f100000e92ff1f8396513cd8b')
    VDPA_DEV_1 = f'vdpa-{os.getpid()}-1'
    VDPA_DEV_2 = f'vdpa-{os.getpid()}-2'

    def setUp(self) -> None:
        def create_vdpa_dev(name):
            '''
            Create a new vdpasim_blk device and return its vhost_vdpa device
            path.
            '''
            run(f'sudo -n vdpa dev add mgmtdev vdpasim_blk name {name}')
            sysfs_vhost_vdpa_dev_dir = \
                glob.glob(f'/sys/bus/vdpa/devices/{name}/vhost-vdpa-*')[0]
            vhost_dev_basename = os.path.basename(sysfs_vhost_vdpa_dev_dir)
            vhost_dev_path = f'/dev/{vhost_dev_basename}'
            run(f'sudo -n chown {os.getuid()}:{os.getgid()} {vhost_dev_path}')
            return vhost_dev_path

        try:
            run('sudo -n modprobe vhost_vdpa')
            run('sudo -n modprobe vdpa_sim_blk')

            self.vhost_dev_1_path = create_vdpa_dev(self.VDPA_DEV_1)
            self.vhost_dev_2_path = create_vdpa_dev(self.VDPA_DEV_2)
        except subprocess.CalledProcessError:
            self.skipTest('Failed to set up vdpa_blk device')

        super().setUp()

    def tearDown(self) -> None:
        super().tearDown()

        try:
            run(f'sudo -n vdpa dev del {self.VDPA_DEV_2}')
            run(f'sudo -n vdpa dev del {self.VDPA_DEV_1}')
            run('sudo -n modprobe --remove vdpa_sim_blk')
            run('sudo -n modprobe --remove vhost_vdpa')
        except subprocess.CalledProcessError:
            pass # ignore failures

    def test_mirror(self) -> None:
        '''
        Check that I/O works after a mirror blockjob pivots. See
        https://issues.redhat.com/browse/RHEL-88175.
        '''
        kernel_path = self.ASSET_KERNEL.fetch()
        initrd_path = self.ASSET_INITRD.fetch()

        self.vm.add_args('-m', '1G')
        self.vm.add_args('-object', 'memory-backend-memfd,id=mem,size=1G')
        self.vm.add_args('-machine', 'pc,accel=kvm:tcg,memory-backend=mem')
        self.vm.add_args('-append', self.KERNEL_COMMAND_LINE)
        self.vm.add_args('-blockdev',
            'virtio-blk-vhost-vdpa,node-name=vdpa-blk-0,' +
            f'path={self.vhost_dev_1_path},cache.direct=on')
        self.vm.add_args('-device', 'virtio-blk-pci,drive=vdpa-blk-0')

        self.launch_kernel(kernel_path, initrd_path,
                           wait_for='# ')

        self.vm.cmd('blockdev-add',
                    driver='virtio-blk-vhost-vdpa',
                    node_name='vdpa-blk-1',
                    path=self.vhost_dev_2_path,
                    cache={'direct': True})
        self.vm.cmd('blockdev-mirror',
                    device='vdpa-blk-0',
                    job_id='mirror0',
                    target='vdpa-blk-1',
                    sync='full',
                    target_is_zero=True)
        self.vm.event_wait('BLOCK_JOB_READY')
        self.vm.cmd('block-job-complete',
                    device='mirror0')

        exec_command_and_wait_for_pattern(self,
            'dd if=/dev/vda of=/dev/null iflag=direct bs=4k count=1',
            '4096 bytes (4.1 kB, 4.0 KiB) copied')


if __name__ == '__main__':
    LinuxKernelTest.main()
