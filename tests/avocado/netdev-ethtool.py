# ethtool tests for emulated network devices
#
# This test leverages ethtool's --test sequence to validate network
# device behaviour.
#
# SPDX-License-Identifier: GPL-2.0-or-later

import time

from avocado import skip

from avocado_qemu import QemuSystemTest
from avocado_qemu import exec_command, exec_command_and_wait_for_pattern
from avocado_qemu import wait_for_console_pattern

class NetDevEthtool(QemuSystemTest):
    """
    :avocado: tags=arch:x86_64
    :avocado: tags=machine:q35
    """

    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 root=/dev/sda console=ttyS0 '
    # Runs in about 20s under KVM, 26s under TCG, 37s under GCOV
    timeout = 45

    def common_test_code(self, netdev, extra_args=None):
        base_url = ('https://fileserver.linaro.org/s/'
                    'kE4nCFLdQcoBF9t/download?'
                    'path=%2Figb-net-test&files=' )

        # This custom kernel has drivers for all the supported network
        # devices we can emulate in QEMU
        kernel_url = base_url + 'bzImage'
        kernel_hash = '784daede6dab993597f36efbf01f69f184c55152'
        kernel_path = self.fetch_asset(name="bzImage",
                                       locations=(kernel_url), asset_hash=kernel_hash)

        rootfs_url = base_url + 'rootfs.ext4'
        rootfs_hash = '7d28c1bf429de3b441a63756a51f163442ea574b'
        rootfs_path = self.fetch_asset(name="rootfs.ext4",
                                       locations=(rootfs_url),
                                       asset_hash=rootfs_hash)

        kernel_params = self.KERNEL_COMMON_COMMAND_LINE
        if extra_args:
            kernel_params += extra_args

        self.vm.add_args('-kernel', kernel_path,
                         '-append', kernel_params,
                         '-blockdev',
                         f"driver=raw,file.driver=file,file.filename={rootfs_path},node-name=hd0",
                         '-device', 'driver=ide-hd,bus=ide.0,unit=0,drive=hd0',
                         '-device', netdev)

        self.vm.set_console(console_index=0)
        self.vm.launch()

        wait_for_console_pattern(self, "Welcome to Buildroot", vm=None)
        time.sleep(0.2)
        exec_command(self, 'root')
        time.sleep(0.2)
        exec_command_and_wait_for_pattern(self,
                                          "ethtool -t eth1 offline",
                                          "The test result is PASS",
                                          "The test result is FAIL")
        time.sleep(0.2)
        exec_command_and_wait_for_pattern(self, 'halt', "reboot: System halted")

    # Skip testing for MSI for now. Allegedly it was fixed by:
    #   28e96556ba (igb: Allocate MSI-X vector when testing)
    # but I'm seeing oops in the kernel
    @skip("Kernel bug with MSI enabled")
    def test_igb(self):
        self.common_test_code("igb")

    def test_igb_nomsi(self):
        self.common_test_code("igb", "pci=nomsi")


    # It seems the other popular cards we model in QEMU currently fail
    # the pattern test with:
    #
    #   pattern test failed (reg 0x00178): got 0x00000000 expected 0x00005A5A
    #
    # So for now we skip them.

    @skip("Incomplete reg 0x00178 support")
    def test_e1000(self):
        self.common_test_code("e1000")

    @skip("Incomplete reg 0x00178 support")
    def test_i82550(self):
        self.common_test_code("i82550")
