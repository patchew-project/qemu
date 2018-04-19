# -*- coding: utf-8 -*-
#
# Boot a Linux kernel on the Malta board and check the serial console output
#
# Copyright (C) 2018 Philippe Mathieu-Daudé
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Run with:
#
#   avocado run test_linux-boot-console.py \
#     --mux-yaml test_linux-boot-console.py.data/parameters.yaml
#     [--filter-by-tags arch_alpha ...]

import os
import tempfile
import hashlib
import urllib2
import gzip
import shutil

from avocado import skipUnless
from avocado_qemu import test

# XXX Use a cleaner CacheStorage or avocado.utils.[archive|kernel]?
def cachedfile(url, cachedir='/tmp/mycachedir', gzip_compressed=False):
    if not os.path.isdir(cachedir):
        os.mkdir(cachedir)
    key = hashlib.md5(url).hexdigest()
    fbin = cachedir + "/" + key + ".bin"
    if not os.path.isfile(fbin):
        with open(cachedir + "/" + key + ".url", "w") as f:
            f.write(url + '\n')
        with open(fbin, "wb") as f:
            content = urllib2.urlopen(url).read()
            f.write(content)
        if gzip_compressed:
            fgz = cachedir + "/" + key + ".gz"
            shutil.move(fbin, fgz)
            with gzip.open(fgz, 'rb') as f_in, open(fbin, 'wb') as f_out:
                shutil.copyfileobj(f_in, f_out)
    return fbin


class TestAlphaClipperBoot2_6(test.QemuTest):
    """
    :avocado: enable
    :avocado: tags=arch_alpha
    """
    ARCH = "alpha"

    def kernel_url(self):
        return 'http://archive.debian.org/debian/dists/lenny/main/installer-alpha/current/images/cdrom/vmlinuz'

    def setUp(self):
        self.console_path = tempfile.mkstemp()[1]
        kernel_path = cachedfile(self.kernel_url(), gzip_compressed=True)
        self.vm._args.extend(['-machine', 'clipper'])
        self.vm._args.extend(['-m', '64'])
        self.vm._args.extend(['-kernel', kernel_path])
        self.vm._args.extend(['-append', '"console=ttyS0 printk.time=0"'])
        self.vm._args.extend(['-chardev', 'socket,id=srm,server,nowait,path=' + self.console_path])
        self.vm._args.extend(['-serial', 'chardev:srm'])
        # This kernel crashes without VGA display
        self.vm._args.extend(['-vga', 'std'])

    def test_boot_console(self):
        """
        :avocado: tags=uart,printk
        """
        # TODO use skipUnless()
        if self.params.get('arch') != self.ARCH:
            return

        self.vm.launch(self.console_path)
        console = self.vm.get_console(console_address=self.console_path, login=False)
        # no filesystem provided on purpose, wait for the Kernel panic
        bootlog = console.read_until_any_line_matches(["Kernel panic - not syncing: VFS: Unable to mount root fs"], timeout=30.0)[1]
        console.close()
        # check Super I/O
        self.assertIn(u'ttyS0 at I/O 0x3f8 (irq = 4) is a 16550A', bootlog)
        self.assertIn(u'ttyS1 at I/O 0x2f8 (irq = 3) is a 16550A', bootlog)
        self.assertIn(u'i8042 KBD port at 0x60,0x64 irq 1', bootlog)
        self.assertIn(u'i8042 AUX port at 0x60,0x64 irq 12', bootlog)
        self.vm.shutdown()

    def tearDown(self):
        os.remove(self.console_path)
