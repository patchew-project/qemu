# Linux initrd acceptance test.
#
# Copyright (c) 2018 Red Hat, Inc.
#
# Author:
#  Wainer dos Santos Moschetta <wainersm@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import tempfile

from avocado_qemu import Test
from avocado.utils.process import run


class LinuxInitrd(Test):
    """
    Checks QEMU evaluates correctly the initrd file passed as -initrd option.

    :avocado: enable
    :avocado: tags=x86_64
    """

    timeout = 60

    def test_with_2GB_file_should_exit_error_msg(self):
        """
        Pretends to boot QEMU with an initrd file with size of 2GB
        and expect it exits with error message.
        Regression test for bug fixed on commit f3839fda5771596152.
        """
        kernel_url = ('https://mirrors.kernel.org/fedora/releases/28/'
                      'Everything/x86_64/os/images/pxeboot/vmlinuz')
        kernel_hash = '238e083e114c48200f80d889f7e32eeb2793e02a'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        with tempfile.NamedTemporaryFile() as initrd:
            initrd.seek(2048*(1024**2) -1)
            initrd.write(b'\0')
            initrd.flush()
            cmd = "%s -kernel %s -initrd %s" % (self.qemu_bin, kernel_path,
                                                initrd.name)
            res = run(cmd, ignore_status=True)
            self.assertNotEqual(res.exit_status, 0)
            expected_msg = r'.*initrd is too large.*max: \d+, need \d+.*'
            self.assertRegex(res.stderr_text, expected_msg)
