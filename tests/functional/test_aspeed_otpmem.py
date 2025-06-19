
import os
import time
import tempfile
import subprocess

from qemu_test import LinuxKernelTest, Asset
from aspeed import AspeedTest
from qemu_test import exec_command_and_wait_for_pattern, skipIfMissingCommands

class AspeedOtpMemoryTest(AspeedTest):
    # AST2600 SDK image
    ASSET_SDK_V906_AST2600 = Asset(
        'https://github.com/AspeedTech-BMC/openbmc/releases/download/v09.06/ast2600-default-obmc.tar.gz',
        '768d76e247896ad78c154b9cff4f766da2ce65f217d620b286a4a03a8a4f68f5')

    # AST1030 Zephyr image
    ASSET_ZEPHYR_3_00 = Asset(
        ('https://github.com/AspeedTech-BMC'
         '/zephyr/releases/download/v00.03.00/ast1030-evb-demo.zip'),
        '37fe3ecd4a1b9d620971a15b96492a81093435396eeac69b6f3e384262ff555f')
    def generate_otpmem_image(self):
        path = self.scratch_file("otpmem.img")
        pattern = b'\x00\x00\x00\x00\xff\xff\xff\xff' * (16 * 1024 // 8)
        with open(path, "wb") as f:
            f.write(pattern)
        return path

    def test_ast2600_otp_fallback(self):
        image_path = self.archive_extract(self.ASSET_SDK_V906_AST2600)
        bmc_image = self.scratch_file("ast2600-default", "image-bmc")
        self.vm.set_machine("ast2600-evb")
        self.vm.set_console()
        self.do_test_arm_aspeed_sdk_start(
            self.scratch_file("ast2600-default", "image-bmc"))
        self.wait_for_console_pattern('ast2600-default login:')

    def test_ast2600_otp_blockdev_device(self):
        image_path = self.archive_extract(self.ASSET_SDK_V906_AST2600)
        otp_img = self.generate_otpmem_image()
        self.vm.set_console()
        self.vm.add_args(
            "-blockdev", f"node-name=otpmem,driver=file,filename={otp_img}",
            "-device", "aspeed.otpmem,drive=otpmem,id=otpmem-drive",
            "-machine", "ast2600-evb,otpmem=otpmem-drive"
        )
        self.do_test_arm_aspeed_sdk_start(self.scratch_file("ast2600-default", "image-bmc"))
        self.wait_for_console_pattern("ast2600-default login:")

    def test_ast2600_otp_only_blockdev(self):
        image_path = self.archive_extract(self.ASSET_SDK_V906_AST2600)
        otp_img = self.generate_otpmem_image()
        self.vm.set_machine("ast2600-evb")
        self.vm.set_console()
        self.vm.add_args(
            "-blockdev", f"node-name=otpmem,driver=file,filename={otp_img}",
            "-device", "aspeed.otpmem,drive=otpmem,id=otpmem-drive",
        )
        self.do_test_arm_aspeed_sdk_start(self.scratch_file("ast2600-default", "image-bmc"))
        self.wait_for_console_pattern("ast2600-default login:")

    def test_ast2600_otp_only_machine_param(self):
        image_path = self.archive_extract(self.ASSET_SDK_V906_AST2600)
        self.vm.set_console()
        self.vm.add_args(
            "-machine", "ast2600-evb,otpmem=otpmem-drive"
        )
        self.do_test_arm_aspeed_sdk_start(self.scratch_file("ast2600-default", "image-bmc"))
        self.wait_for_console_pattern("ast2600-default login:")

    def test_ast1030_otp_fallback(self):
        kernel_name = "ast1030-evb-demo/zephyr.elf"
        kernel_file = self.archive_extract(self.ASSET_ZEPHYR_3_00, member=kernel_name)

        self.vm.set_machine("ast1030-evb")
        self.vm.set_console()
        self.vm.add_args("-kernel", kernel_file)
        self.vm.launch()
        self.wait_for_console_pattern("Booting Zephyr OS")

if __name__ == '__main__':
    AspeedTest.main()