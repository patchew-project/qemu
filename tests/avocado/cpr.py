# cpr test

# Copyright (c) 2021, 2022 Oracle and/or its affiliates.
#
# This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.

import tempfile
from avocado_qemu import QemuSystemTest
from avocado.utils import wait

class Cpr(QemuSystemTest):
    """
    :avocado: tags=cpr
    """

    timeout = 5
    fast_timeout = 1

    @staticmethod
    def has_status(vm, status):
        return vm.command('query-status')['status'] == status

    def wait_for_status(self, vm, status):
        wait.wait_for(self.has_status,
                      timeout=self.timeout,
                      step=0.1,
                      args=(vm,status,))

    def run_and_fail(self, vm, msg):
        # Qemu will fail fast, so disable monitor to avoid timeout in accept
        vm.set_qmp_monitor(False)
        vm.launch()
        vm.wait(self.timeout)
        self.assertRegex(vm.get_log(), msg)

    def do_cpr_restart(self, vmstate_name):
        vm = self.get_vm('-nodefaults',
                         '-cpr-enable', 'restart',
                         '-object', 'memory-backend-memfd,id=pc.ram,size=8M',
                         '-machine', 'memory-backend=pc.ram')

        vm.launch()

        vm.qmp('cpr-save', filename=vmstate_name, mode='restart')
        vm.event_wait(name='STOP', timeout=self.fast_timeout)

        args = vm.full_args + ['-S']
        vm.qmp('cpr-exec', argv=args)

        # exec closes the monitor socket, so reopen it.
        vm.reopen_qmp_connection()

        self.wait_for_status(vm, 'prelaunch')
        vm.qmp('cpr-load', filename=vmstate_name, mode='restart')
        vm.event_wait(name='RESUME', timeout=self.fast_timeout)

        self.assertEqual(vm.command('query-status')['status'], 'running')

    def do_cpr_reboot(self, vmstate_name):
        old_vm = self.get_vm('-nodefaults',
                             '-cpr-enable', 'reboot')
        old_vm.launch()

        old_vm.qmp('cpr-save', filename=vmstate_name, mode='reboot')
        old_vm.event_wait(name='STOP', timeout=self.fast_timeout)

        new_vm = self.get_vm('-nodefaults',
                             '-cpr-enable', 'reboot',
                             '-S')
        new_vm.launch()
        self.wait_for_status(new_vm, 'prelaunch')

        new_vm.qmp('cpr-load', filename=vmstate_name, mode='reboot')
        new_vm.event_wait(name='RESUME', timeout=self.fast_timeout)

        self.assertEqual(new_vm.command('query-status')['status'], 'running')

    def test_cpr_restart(self):
        """
        Verify that cpr restart mode works
        """
        with tempfile.NamedTemporaryFile() as vmstate_file:
            self.do_cpr_restart(vmstate_file.name)

    def test_cpr_reboot(self):
        """
        Verify that cpr reboot mode works
        """
        with tempfile.NamedTemporaryFile() as vmstate_file:
            self.do_cpr_reboot(vmstate_file.name)

    def test_cpr_block_cpr_save(self):

        """
        Verify that qemu rejects cpr-save for volatile memory
        """
        vm = self.get_vm('-nodefaults',
                         '-cpr-enable', 'restart')
        vm.launch()
        rsp = vm.qmp('cpr-save', filename='/dev/null', mode='restart')
        vm.qmp('quit')

        expect = r'Memory region .* is volatile'
        self.assertRegex(rsp['error']['desc'], expect)

    def test_cpr_block_memfd(self):

        """
        Verify that qemu complains for only-cpr-capable and volatile memory
        """
        vm = self.get_vm('-nodefaults',
                         '-cpr-enable', 'restart',
                         '-only-cpr-capable')
        self.run_and_fail(vm, r'only-cpr-capable specified.* Memory ')

    def test_cpr_block_replay(self):
        """
        Verify that qemu complains for only-cpr-capable and replay
        """
        vm = self.get_vm('-nodefaults',
                         '-cpr-enable', 'restart',
                         '-object', 'memory-backend-memfd,id=pc.ram,size=8M',
                         '-machine', 'memory-backend=pc.ram',
                         '-only-cpr-capable',
                         '-icount', 'shift=10,rr=record,rrfile=/dev/null')
        self.run_and_fail(vm, r'only-cpr-capable specified.* replay ')

    def test_cpr_block_chardev(self):
        """
        Verify that qemu complains for only-cpr-capable and unsupported chardev
        """
        vm = self.get_vm('-nodefaults',
                         '-cpr-enable', 'restart',
                         '-object', 'memory-backend-memfd,id=pc.ram,size=8M',
                         '-machine', 'memory-backend=pc.ram',
                         '-only-cpr-capable',
                         '-chardev', 'vc,id=vc1')
        self.run_and_fail(vm, r'only-cpr-capable specified.* vc1 ')

    def test_cpr_allow_chardev(self):
        """
        Verify that qemu allows unsupported chardev with reopen-on-cpr
        """
        vm = self.get_vm('-nodefaults',
                         '-cpr-enable', 'restart',
                         '-object', 'memory-backend-memfd,id=pc.ram,size=8M',
                         '-machine', 'memory-backend=pc.ram',
                         '-only-cpr-capable',
                         '-chardev', 'vc,id=vc1,reopen-on-cpr=on')
        vm.launch()
        self.wait_for_status(vm, 'running')
