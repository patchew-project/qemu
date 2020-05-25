# Record/replay test that boots a Linux kernel
#
# Copyright (c) 2020 ISP RAS
#
# Author:
#  Pavel Dovgalyuk <Pavel.Dovgaluk@ispras.ru>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import os
import gzip

from avocado_qemu import Test
from avocado_qemu import wait_for_console_pattern
from avocado.utils import process
from avocado.utils import archive

class ReplayKernel(Test):
    """
    Boots a Linux kernel in record mode and checks that the console
    is operational and the kernel command line is properly passed
    from QEMU to the kernel.
    Then replays the same scenario and verifies, that QEMU correctly
    terminates.
    """

    timeout = 90

    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '

    def wait_for_console_pattern(self, success_message, vm):
        wait_for_console_pattern(self, success_message,
                                 failure_message='Kernel panic - not syncing',
                                 vm=vm)

    def extract_from_deb(self, deb, path):
        """
        Extracts a file from a deb package into the test workdir

        :param deb: path to the deb archive
        :param path: path within the deb archive of the file to be extracted
        :returns: path of the extracted file
        """
        cwd = os.getcwd()
        os.chdir(self.workdir)
        file_path = process.run("ar t %s" % deb).stdout_text.split()[2]
        process.run("ar x %s %s" % (deb, file_path))
        archive.extract(file_path, self.workdir)
        os.chdir(cwd)
        # Return complete path to extracted file.  Because callers to
        # extract_from_deb() specify 'path' with a leading slash, it is
        # necessary to use os.path.relpath() as otherwise os.path.join()
        # interprets it as an absolute path and drops the self.workdir part.
        return os.path.normpath(os.path.join(self.workdir,
                                             os.path.relpath(path, '/')))

    def run_vm(self, kernel_path, kernel_command_line, console_pattern, record, shift, args):
        vm = self.get_vm()
        vm.set_console()
        if record:
            mode = 'record'
        else:
            mode = 'replay'
        vm.add_args('-icount', 'shift=%s,rr=%s,rrfile=%s' %
                    (shift, mode, os.path.join(self.workdir, 'replay.bin')),
                    '-kernel', kernel_path,
                    '-append', kernel_command_line,
                    '-net', 'none',
                    *args)
        vm.launch()
        self.wait_for_console_pattern(console_pattern, vm)
        if record:
            vm.shutdown()
        else:
            vm.wait()

    def run_rr(self, kernel_path, kernel_command_line, console_pattern, shift=7, args=()):
        self.run_vm(kernel_path, kernel_command_line, console_pattern, True, shift, args)
        self.run_vm(kernel_path, kernel_command_line, console_pattern, False, shift, args)

    def test_x86_64_pc(self):
        """
        :avocado: tags=arch:x86_64
        :avocado: tags=machine:pc
        """
        kernel_url = ('https://archives.fedoraproject.org/pub/archive/fedora'
                      '/linux/releases/29/Everything/x86_64/os/images/pxeboot'
                      '/vmlinuz')
        kernel_hash = '23bebd2680757891cf7adedb033532163a792495'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'console=ttyS0'
        console_pattern = 'Kernel command line: %s' % kernel_command_line

        self.run_rr(kernel_path, kernel_command_line, console_pattern)
