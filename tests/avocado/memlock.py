# Functional test that check overcommit memlock options
#
# Copyright (c) Yandex Technologies LLC, 2025
#
# Author:
#  Alexandr Moshkov <dtalexundeer@yandex-team.ru>
#
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import re

from typing import List, Dict

from avocado_qemu.linuxtest import LinuxTest


SMAPS_HEADER_PATTERN = re.compile(r'^\w+-\w+', re.MULTILINE)
SMAPS_VALUE_PATTERN = re.compile(r'^(\w+):\s+(\d+) kB', re.MULTILINE)


class Memlock(LinuxTest):
    """
    Boots a Linux system with memlock options.
    Then verify, that this options is working correctly
    by checking the smaps of the QEMU proccess.
    """

    def common_vm_setup_with_memlock(self, memlock):
        self.vm.add_args('-overcommit', f'mem-lock={memlock}')
        self.launch_and_wait(set_up_ssh_connection=False)

    def get_anon_smaps_by_pid(self, pid):
        smaps_raw = self._get_raw_smaps_by_pid(pid)
        return self._parse_anonymous_smaps(smaps_raw)


    def test_memlock_off(self):
        self.common_vm_setup_with_memlock('off')

        anon_smaps = self.get_anon_smaps_by_pid(self.vm.get_pid())

        # locked = 0 on every smap
        for smap in anon_smaps:
            self.assertEqual(smap['Locked'], 0)

    def test_memlock_on(self):
        self.common_vm_setup_with_memlock('on')

        anon_smaps = self.get_anon_smaps_by_pid(self.vm.get_pid())

        # size = rss = locked on every smap where rss not 0
        for smap in anon_smaps:
            if smap['Rss'] == 0:
                continue
            self.assertTrue(smap['Size'] == smap['Rss'] == smap['Locked'])

    def test_memlock_onfault(self):
        self.common_vm_setup_with_memlock('on-fault')

        anon_smaps = self.get_anon_smaps_by_pid(self.vm.get_pid())

        # rss = locked on every smap and segment with rss < size exists
        exists = False
        for smap in anon_smaps:
            self.assertTrue(smap['Rss'] == smap['Locked'])
            if smap['Rss'] < smap['Size']:
                exists = True
        self.assertTrue(exists)


    def _parse_anonymous_smaps(self, smaps_raw: str) -> List[Dict[str, int]]:
        result_segments = []
        current_segment = {}
        is_anonymous = False

        for line in smaps_raw.split('\n'):
            if SMAPS_HEADER_PATTERN.match(line):
                if current_segment and is_anonymous:
                    result_segments.append(current_segment)
                current_segment = {}
                # anonymous segment header looks like this:
                # 7f3b8d3f0000-7f3b8d3f3000 rw-s 00000000 00:0f 1052
                # and non anonymous header looks like this:
                # 7f3b8d3f0000-7f3b8d3f3000 rw-s 00000000 00:0f 1052   [stack]
                is_anonymous = len(line.split()) == 5
            elif m := SMAPS_VALUE_PATTERN.match(line):
                current_segment[m.group(1)] = int(m.group(2))

        if current_segment and is_anonymous:
            result_segments.append(current_segment)

        return result_segments

    def _get_raw_smaps_by_pid(self, pid: int) -> str:
        with open(f'/proc/{pid}/smaps', 'r') as f:
            return f.read()
