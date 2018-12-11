# CPU definitions tests.
#
# Copyright (c) 2018 Red Hat, Inc.
#
# Author:
#  Wainer dos Santos Moschetta <wainersm@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from avocado import skip
from avocado_qemu import Test


class CPUDefinitions(Test):
    """
    Tests for the CPU definitions.

    :avocado: enable
    :avocado: tags=x86_64
    """
    def test_unavailable_features(self):
        self.vm.add_args("-machine", "q35")
        self.vm.launch()
        cpu_definitions = self.vm.command('query-cpu-definitions')
        self.assertTrue(len(cpu_definitions) > 0)
        for cpu_model in cpu_definitions:
            name = cpu_model.get('name')
            unavailable_features = cpu_model.get('unavailable-features')

            self.assertNotIn("", unavailable_features,
                             name + " has unamed feature")
            self.assertEqual(len(unavailable_features),
                             len(set(unavailable_features)),
                             name + " has duplicate feature")
