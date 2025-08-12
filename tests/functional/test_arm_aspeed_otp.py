import os
import time
import tempfile
import subprocess

from qemu_test import LinuxKernelTest, Asset
from aspeed import AspeedTest
from qemu_test import exec_command_and_wait_for_pattern, skipIfMissingCommands

class AspeedOtpMemoryTest(AspeedTest):
    # AST2600 SDK image
    ASSET_SDK_V907_AST2600 = Asset(
        'https://github.com/AspeedTech-BMC/openbmc/releases/download/v09.07/ast2600-default-obmc.tar.gz',
        'cb6c08595bcbba1672ce716b068ba4e48eda1ed9abe78a07b30392ba2278feba')

    # AST1030 Zephyr image
    ASSET_ZEPHYR_3_02 = Asset(
        'https://github.com/AspeedTech-BMC/zephyr/releases/download/v00.03.02/ast1030-evb-demo.zip',
        '1ec83caab3ddd5d09481772801be7210e222cb015ce22ec6fffb8a76956dcd4f')

    def generate_otpmem_image(self):
        path = self.scratch_file("otpmem.img")
        pattern = b'\x00\x00\x00\x00\xff\xff\xff\xff' * (16 * 1024 // 8)
        with open(path, "wb") as f:
            f.write(pattern)
        return path

    def test_ast2600_otp_blockdev_device(self):
        image_path = self.archive_extract(self.ASSET_SDK_V907_AST2600)
        otp_img = self.generate_otpmem_image()
        self.vm.set_machine("ast2600-evb")
        self.vm.set_console()
        self.vm.add_args(
            "-blockdev", f"driver=file,filename={otp_img},node-name=otp",
            "-global", "aspeed-otp.drive=otp",
        )
        self.do_test_arm_aspeed_sdk_start(self.scratch_file("ast2600-default", "image-bmc"))
        self.wait_for_console_pattern("ast2600-default login:")

    def test_ast1030_otp_blockdev_device(self):
        kernel_name = "ast1030-evb-demo-3/zephyr.elf"
        kernel_file = self.archive_extract(self.ASSET_ZEPHYR_3_02, member=kernel_name)
        otp_img = self.generate_otpmem_image()
        self.vm.set_machine("ast1030-evb")
        self.vm.set_console()
        self.vm.add_args(
            "-kernel", kernel_file,
            "-blockdev", f"driver=file,filename={otp_img},node-name=otp",
            "-global", "aspeed-otp.drive=otp",
        )
        self.vm.launch()
        self.wait_for_console_pattern("Booting Zephyr OS")

if __name__ == '__main__':
    AspeedTest.main()
