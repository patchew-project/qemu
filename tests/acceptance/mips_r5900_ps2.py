# Test the MIPS R5900 CPU
#
# Copyright (C) 2021 Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import lzma
import shutil

from avocado import skipUnless

from avocado.utils import archive
from avocado_qemu import QemuUserTest

class R5900(QemuUserTest):

    @skipUnless(os.getenv('AVOCADO_ALLOW_UNTRUSTED_CODE'), 'untrusted code')
    def test_gentoo_busybox_32bit(self):
        """
        :avocado: tags=arch:r5900o32el
        :avocado: tags=linux_user
        :avocado: tags=quick
        """
        busybox_url = ('https://raw.githubusercontent.com/philmd'
                       '/qemu-testing-blob/bf6a300cf0bc56e4a2c5b'
                       '/mips/ps2/busybox-gentoo-v1.32.0.xz')
        busybox_hash = 'd2a0abc2c7c3adb6b2fdd63356be78bc5e99f324'
        busybox_path_xz = self.fetch_asset(busybox_url, asset_hash=busybox_hash)

        busybox_path = os.path.join(self.workdir, "busybox")
        with lzma.open(busybox_path_xz, 'rb') as f_in:
            with open(busybox_path, 'wb') as f_out:
                shutil.copyfileobj(f_in, f_out)
        os.chmod(busybox_path, 0o755)

        res = self.run(busybox_path)
        ver = 'BusyBox v1.32.0 (2021-02-09 15:13:23 -00) multi-call binary'
        self.assertIn(ver, res.stdout_text)

        res = self.run(busybox_path, ['uname', '-a'])
        unm = 'mips64 mips64 mips64 GNU/Linux'
        self.assertIn(unm, res.stdout_text)

    @skipUnless(os.getenv('AVOCADO_ALLOW_UNTRUSTED_CODE'), 'untrusted code')
    def test_ps2_busybox_64bit(self):
        """
        :avocado: tags=arch:r5900o32el
        :avocado: tags=linux_user
        :avocado: tags=quick
        """
        rootfs_url = ('https://raw.githubusercontent.com/philmd'
                       '/qemu-testing-blob/bf6a300cf0bc56e4a2c5b/mips'
                       '/ps2/ps2linux_live_v5_pal_netsurf_usb_busybox.tar.gz')
        rootfs_hash = '9aa8fdd974cd3332c7167bceb6dd7137853d3a10'
        rootfs_path_tgz = self.fetch_asset(rootfs_url, asset_hash=rootfs_hash)

        archive.extract(rootfs_path_tgz, self.workdir)
        busybox_path = self.workdir + "/bin/busybox"

        self.add_ldpath(self.workdir)

        res = self.run(busybox_path)
        ver = 'BusyBox v0.60.5 (2010.06.06-16:16+0000) multi-call binary'
        self.assertIn(ver, res.stderr_text)

        res = self.run(busybox_path, ['uname', '-a'])
        unm = 'mips64 unknown'
        self.assertIn(unm, res.stdout_text)
