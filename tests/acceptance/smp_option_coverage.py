# QEMU -smp option coverage test.
#
# Copyright (c) 2018 Red Hat, Inc.
#
# Author:
#  Wainer dos Santos Moschetta <wainersm@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from functools import reduce
from avocado.utils.process import run

from avocado_qemu import Test


class SmpOption(Test):
    """
    Launches QEMU with various cpus, cores, threads, sockets, and maxcpus
    combination through -smp option, to check it does not accept invalid SMP
    topologies as well as it is able to calculate correctly any missing values.

    :avocado: enable
    :avocado: tags=slow,coverage
    """
    def setUp(self):
        super().setUp()
        self.cores = self.params.get('cores', default=2)
        self.threads = self.params.get('threads', default=2)
        self.sockets = self.params.get('sockets', default=2)
        self.cpus = self.params.get('cpus', default=8)

    def get_smp_topology(self):
        """
        Returns a dict with the id of cores, threads and sockets.
        """
        res = self.vm.qmp('query-hotpluggable-cpus')['return']
        cpus = [x['props'] for x in res]

        return reduce(lambda x, y: {'core-id': x['core-id'].union([y['core-id']]),
                                    'thread-id': x['thread-id'].union([y['thread-id']]),
                                    'socket-id': x['socket-id'].union([y['socket-id']])},
                      cpus, {'core-id': set(), 'thread-id': set(), 'socket-id': set()})

    @staticmethod
    def build_option(**kwargs):
        """
        Builds string for the -smp option.
        Use cpus, cores, threads, sockets, maxcpus keys.
        """
        option_list = []
        if kwargs.get('cpus', None) is not None:
            option_list.append(str(kwargs.get('cpus')))
        for key, val in kwargs.items():
            if key == 'cpus':
                continue
            option_list.append('%s=%s' % (key, val))

        return ",".join(option_list)

    def launch_and_check(self, expect_cores=1, expect_threads=1,
                         expect_sockets=1, **kwargs):
        """
        Launches VM and check its SMP topology was correctly set.
        Use cpus, cores, threads, sockets, and maxcpus keys to specify the
        topology.
        """
        option = self.build_option(**{key: val for key, val in kwargs.items()
                                      if key in ['cpus', 'cores', 'threads',
                                                 'sockets', 'maxcpus']})
        self.vm.add_args('-smp', option)
        self.vm.launch()
        smp = self.get_smp_topology()
        self.assertEqual(smp['core-id'], set(range(0, expect_cores)))
        self.assertEqual(smp['thread-id'], set(range(0, expect_threads)))
        self.assertEqual(smp['socket-id'], set(range(0, expect_sockets)))

    def launch_and_check_fail(self, **kwargs):
        """
        Launches VM and check the process exits with expected error code, for
        cases where the topology is expected not valid.
        """
        option = self.build_option(**kwargs)
        res = run("%s -smp %s" % (self.qemu_bin, option), timeout=10,
                  ignore_status=True)
        self.assertNotEqual(res.exit_status, 0)

    # Passing cpus and maxcpus only.
    #

    def test_cpus_eq_maxcpus(self):
        self.launch_and_check(cpus=self.cpus,
                              maxcpus=self.cpus,
                              expect_sockets=self.cpus)

    def test_cpus_lt_maxcpus(self):
        maxcpus = self.cpus * 2
        self.launch_and_check(cpus=self.cpus,
                              maxcpus=maxcpus,
                              expect_sockets=maxcpus)

    def test_cpus_gt_maxcpus(self):
        self.launch_and_check_fail(cpus=self.cpus,
                                   maxcpus=self.cpus // 2)

    # Passing a combination of cores, threads and sockets only.
    #

    def test_no_cores_no_threads_no_sockets(self):
        self.launch_and_check(cpus=self.cpus,
                              expect_sockets=self.cpus)

    def test_no_cores_no_threads_sockets(self):
        self.launch_and_check(sockets=self.sockets,
                              expect_sockets=self.sockets)

    def test_no_cores_threads_no_sockets(self):
        self.launch_and_check(threads=self.threads,
                              expect_threads=self.threads)

    def test_no_cores_threads_sockets(self):
        self.launch_and_check(threads=self.threads,
                              sockets=self.sockets,
                              expect_threads=self.threads,
                              expect_sockets=self.sockets)

    def test_cores_no_threads_no_sockets(self):
        self.launch_and_check(cores=self.cores,
                              expect_cores=self.cores)

    def test_cores_no_threads_sockets(self):
        self.launch_and_check(cores=self.cores,
                              sockets=self.sockets,
                              expect_cores=self.cores,
                              expect_sockets=self.sockets)

    def test_cores_threads_no_sockets(self):
        self.launch_and_check(cores=self.cores,
                              threads=self.threads,
                              expect_cores=self.cores,
                              expect_threads=self.threads)

    def test_cores_threads_sockets(self):
        self.launch_and_check(cores=self.cores,
                              threads=self.threads,
                              sockets=self.sockets,
                              expect_cores=self.cores,
                              expect_threads=self.threads,
                              expect_sockets=self.sockets)

    # Passing cpus always and a combination of cores, threads and sockets.
    #

    def test_cpus_no_cores_no_threads_sockets(self):
        self.launch_and_check(cpus=self.cpus,
                              sockets=self.sockets,
                              expect_cores=self.cpus // self.sockets,
                              expect_sockets=self.sockets)

    def test_cpus_no_cores_threads_no_sockets(self):
        self.launch_and_check(cpus=self.cpus,
                              threads=self.threads,
                              expect_threads=self.threads,
                              expect_sockets=self.cpus // self.threads)

    def test_cpus_no_cores_threads_sockets(self):
        self.launch_and_check(cpus=self.cpus,
                              threads=self.threads,
                              sockets=self.sockets,
                              expect_threads=self.threads,
                              expect_sockets=self.sockets,
                              expect_cores=self.cpus // (self.threads * self.sockets))

    def test_cpus_cores_no_threads_no_sockets(self):
        self.launch_and_check(cpus=self.cpus,
                              cores=self.cores,
                              expect_cores=self.cores,
                              expect_sockets=self.cpus // self.cores)

    def test_cpus_cores_no_threads_sockets(self):
        self.launch_and_check(cpus=self.cpus,
                              cores=self.cores,
                              sockets=self.sockets,
                              expect_cores=self.cores,
                              expect_sockets=self.sockets,
                              expect_threads=self.cpus // (self.cores * self.sockets))

    def test_cpus_cores_threads_no_sockets(self):
        self.launch_and_check(cpus=self.cpus,
                              cores=self.cores,
                              threads=self.threads,
                              expect_cores=self.cores,
                              expect_threads=self.threads,
                              expect_sockets=self.cpus // (self.cores * self.threads))

    def test_cpus_cores_threads_sockets(self):
        self.launch_and_check(cpus=self.cores * self.threads * self.sockets,
                              cores=self.cores,
                              threads=self.threads,
                              sockets=self.sockets,
                              expect_cores=self.cores,
                              expect_threads=self.threads,
                              expect_sockets=self.sockets)

    # Passing cpus less (or greater) than cores, threads and sockets.
    #

    def test_cpus_lt_cores_threads_sockets(self):
        self.launch_and_check_fail(cpus=(self.cores * self.threads * self.sockets) // 2,
                                   cores=self.cores,
                                   threads=self.threads,
                                   sockets=self.sockets,)

    def test_cpus_gt_cores_threads_sockets(self):
        self.launch_and_check_fail(cpus=self.cores * self.threads * self.sockets * 2,
                                   cores=self.cores,
                                   threads=self.threads,
                                   sockets=self.sockets)
