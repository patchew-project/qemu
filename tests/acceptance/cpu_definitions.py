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
        self.vm.add_args("-machine", "q35,accel=kvm")
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

    @skip("Have assumptions about the host CPU")
    def test_unavailable_features_manual(self):
        """
        This test is meant for manual testing only because the list of expected
        unavailable features depend on the actual host CPU knowledge.
        """
        expected_cpu = 'Cascadelake-Server'
        expected_unavailable_features = ["hle", "rtm", "mpx", "avx512f",
                                         "avx512dq", "rdseed", "adx", "smap",
                                         "clflushopt", "clwb", "intel-pt",
                                         "avx512cd", "avx512bw", "avx512vl",
                                         "pku", "avx512vnni", "spec-ctrl",
                                         "ssbd", "3dnowprefetch", "xsavec",
                                         "xgetbv1"]

        self.vm.add_args("-machine", "q35,accel=kvm")
        self.vm.launch()
        cpu_definitions = self.vm.command('query-cpu-definitions')
        self.assertTrue(len(cpu_definitions) > 0)

        cpus = [cpu_model for cpu_model in cpu_definitions
                if cpu_model['name'] == expected_cpu]
        actual_unavailable_features = cpus[0]['unavailable-features']
        self.assertCountEqual(expected_unavailable_features,
                              actual_unavailable_features)
