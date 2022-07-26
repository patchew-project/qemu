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
    def has_status(vm, status, command):
        return vm.command(command)['status'] in status

    def wait_for_status(self, vm, status, command):
        wait.wait_for(self.has_status,
                      timeout=self.timeout,
                      step=0.1,
                      args=(vm,status,command,))

    def wait_for_runstate(self, vm, status):
        self.wait_for_status(vm, status, 'query-status')

    def wait_for_migration(self, vm, status):
        self.wait_for_status(vm, status, 'query-migrate')

    def run_and_fail(self, vm, msg):
        # Qemu will fail fast, so disable monitor to avoid timeout in accept
        vm.set_qmp_monitor(False)
        vm.launch()
        vm.wait(self.timeout)
        self.assertRegex(vm.get_log(), msg)

    def get_vm_for_restart(self):
        return self.get_vm('-nodefaults',
                           '-migrate-mode-enable', 'cpr-exec',
                           '-object', 'memory-backend-memfd,id=pc.ram,size=8M',
                           '-machine', 'memory-backend=pc.ram')

    def do_cpr_exec(self, vmstate_name):
        vm = self.get_vm_for_restart()
        vm.launch()

        uri = 'file:' + vmstate_name
        args = vm.full_args + ['-incoming', 'defer']

        vm.command('migrate-set-parameters', cpr_exec_args=args)
        vm.command('migrate-set-parameters', mode='cpr-exec')
        vm.qmp('migrate', uri=uri)

        # Cannot poll for migration status, because qemu may call execv before
        # we see it. Wait for STOP instead.
        vm.event_wait(name='STOP', timeout=self.fast_timeout)

        # Migrate execs and closes the monitor socket, so reopen it.
        vm.reopen_qmp_connection()

        self.assertEqual(vm.command('query-status')['status'], 'inmigrate')
        resp = vm.command('migrate-incoming', uri=uri)
        self.wait_for_migration(vm, ('completed', 'failed'))
        self.assertEqual(vm.command('query-migrate')['status'], 'completed')

        resp = vm.command('cont')
        vm.event_wait(name='RESUME', timeout=self.fast_timeout)
        self.assertEqual(vm.command('query-status')['status'], 'running')

    def do_cpr_reboot(self, vmstate_name):
        args = ['-nodefaults', '-migrate-mode-enable', 'cpr-reboot' ]
        old_vm = self.get_vm(*args)
        old_vm.launch()

        uri = 'file:' + vmstate_name

        old_vm.command('migrate-set-capabilities', capabilities = [
                       { "capability": "x-ignore-shared", "state": True }])
        old_vm.command('migrate-set-parameters', mode='cpr-reboot')
        old_vm.qmp('migrate', uri=uri)
        self.wait_for_migration(old_vm, ('completed', 'failed'))
        self.assertEqual(old_vm.command('query-migrate')['status'],
                         'completed')
        self.assertEqual(old_vm.command('query-status')['status'],
                         'postmigrate')

        args = args + ['-incoming', 'defer']
        new_vm = self.get_vm(*args)
        new_vm.launch()
        self.assertEqual(new_vm.command('query-status')['status'], 'inmigrate')

        new_vm.command('migrate-set-capabilities', capabilities = [
                       { "capability": "x-ignore-shared", "state": True }])
        new_vm.command('migrate-set-parameters', mode='cpr-reboot')
        new_vm.command('migrate-incoming', uri=uri)
        self.wait_for_migration(new_vm, ('completed', 'failed'))
        self.assertEqual(new_vm.command('query-migrate')['status'], 'completed')

        new_vm.command('cont')
        new_vm.event_wait(name='RESUME', timeout=self.fast_timeout)
        self.assertEqual(new_vm.command('query-status')['status'], 'running')

    def test_cpr_exec(self):
        """
        Verify that cpr restart mode works
        """
        with tempfile.NamedTemporaryFile() as vmstate_file:
            self.do_cpr_exec(vmstate_file.name)

    def test_cpr_reboot(self):
        """
        Verify that cpr reboot mode works
        """
        with tempfile.NamedTemporaryFile() as vmstate_file:
            self.do_cpr_reboot(vmstate_file.name)

    def test_cpr_block_cpr_exec(self):
        """
        Verify that qemu rejects cpr restart mode for volatile memory
        """

        vm = self.get_vm('-nodefaults',
                         '-migrate-mode-enable', 'cpr-exec')
        vm.launch()
        uri='file:/dev/null'
        args = vm.full_args + ['-S']
        resp = vm.command('migrate-set-parameters', mode='cpr-exec')
        rsp = vm.qmp('migrate', uri=uri)
        vm.qmp('quit')

        expect = r'Memory region .* is volatile'
        self.assertRegex(rsp['error']['desc'], expect)

    def test_cpr_block_memfd(self):

        """
        Verify that qemu complains for only-cpr-capable and volatile memory
        """
        vm = self.get_vm('-nodefaults',
                         '-migrate-mode-enable', 'cpr-exec',
                         '-only-cpr-capable')
        self.run_and_fail(vm, r'only-cpr-capable specified.* Memory ')

    def test_cpr_block_replay(self):
        """
        Verify that qemu complains for only-cpr-capable and replay
        """
        vm = self.get_vm_for_restart()
        vm.add_args('-only-cpr-capable',
                    '-icount', 'shift=10,rr=record,rrfile=/dev/null')
        self.run_and_fail(vm, r'only-cpr-capable specified.* replay ')

    def test_cpr_block_chardev(self):
        """
        Verify that qemu complains for only-cpr-capable and unsupported chardev
        """
        vm = self.get_vm_for_restart()
        vm.add_args('-only-cpr-capable',
                    '-chardev', 'vc,id=vc1')
        self.run_and_fail(vm, r'only-cpr-capable specified.* vc1 ')

    def test_cpr_allow_chardev(self):
        """
        Verify that qemu allows unsupported chardev with reopen-on-cpr
        """
        vm = self.get_vm_for_restart()
        vm.add_args('-only-cpr-capable',
                    '-chardev', 'vc,id=vc1,reopen-on-cpr=on')
        vm.launch()
        self.wait_for_runstate(vm, ('running'))
