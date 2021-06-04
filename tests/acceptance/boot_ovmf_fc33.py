# Functional test that boots OVMF firmware with cpu host.
#
# This test was added to capture x86 "host" cpu initialization and realization
# ordering problems.
#
# Copyright (c) 2021 SUSE LLC
#
# Author:
#  Claudio Fontana <cfontana@suse.de>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import os
import time

from avocado_qemu import Test
from avocado_qemu import extract_from_rpm
from avocado_qemu import wait_for_console_pattern
from avocado.utils import process
from avocado.utils.path import find_command, CmdNotFoundError

class FirmwareTest(Test):
    def wait_for_firmware_message(self, success_message):
        wait_for_console_pattern(self, success_message, failure_message=None)

class BootOVMF(FirmwareTest):
    """
    Boots OVMF secureboot and checks for a specific message.
    If we do not see the message, it's an ERROR that we express via a timeout.
    """
    timeout = 10

    def test_cpu_host_x86(self):
        """
        :avocado: tags=arch:x86_64
        :avocado: tags=machine:q35
        :avocado: tags=cpu:host
        :avocado: tags=accel:kvm
        """
        self.require_accelerator("kvm")

        rpm_url = ('https://download-ib01.fedoraproject.org/'
                   'pub/fedora/linux/updates/33/Everything/x86_64/Packages/e/'
                   'edk2-ovmf-20200801stable-3.fc33.noarch.rpm')
        rpm_hash = '45e1001313dc2deed9b41a532ef090682a11ccd1'
        rpm_path = self.fetch_asset(rpm_url, asset_hash=rpm_hash)

        # Note the use of "./" at the beginning of the paths in the rpm,
        # it is not an accident, see extract_from_rpm in avocado_qemu/

        ovmf_code_sec = extract_from_rpm(self, rpm_path,
                                  './usr/share/edk2/ovmf/OVMF_CODE.secboot.fd')
        ovmf_vars_sec = extract_from_rpm(self, rpm_path,
                                  './usr/share/edk2/ovmf/OVMF_VARS.secboot.fd')

        # at this point the ovmf code should be reachable in the tmp dir; we
        # can use this sleep to debug issues with the extraction above.
        #time.sleep(3600)

        self.vm.set_console()
        self.vm.add_args(
            '-accel', 'kvm',
            '-cpu', 'host',
            '-machine', 'q35,smm=on',
            '-m', '4G',
            '-drive',
               'if=pflash,format=raw,readonly=on,unit=0,file=' + ovmf_code_sec,
            '-drive',
               'if=pflash,format=raw,unit=1,file=' + ovmf_vars_sec,
            '-display', 'none',
            '-serial', 'stdio')
        self.vm.launch()
        console_pattern = 'BdsDxe: failed to load Boot0001'
        self.wait_for_firmware_message(success_message=console_pattern);
