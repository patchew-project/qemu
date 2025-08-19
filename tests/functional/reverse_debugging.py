# SPDX-License-Identifier: GPL-2.0-or-later
#
# Reverse debugging test
#
# Copyright (c) 2020 ISP RAS
# Copyright (c) 2025 Linaro Limited
#
# Author:
#  Pavel Dovgalyuk <Pavel.Dovgalyuk@ispras.ru>
#  Gustavo Romero <gustavo.romero@linaro.org> (Run without Avocado)
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

try:
    import gdb
except ModuleNotFoundError:
    from sys import exit
    exit("This script must be launched via tests/guest-debug/run-test.py!")
import logging
import os
import subprocess


from qemu_test import LinuxKernelTest, get_qemu_img
from qemu_test.ports import Ports


class ReverseDebugging(LinuxKernelTest):
    """
    Test GDB reverse debugging commands: reverse step and reverse continue.
    Recording saves the execution of some instructions and makes an initial
    VM snapshot to allow reverse execution.
    Replay saves the order of the first instructions and then checks that they
    are executed backwards in the correct order.
    After that the execution is replayed to the end, and reverse continue
    command is checked by setting several breakpoints, and asserting
    that the execution is stopped at the last of them.
    """

    STEPS = 10

    def run_vm(self, record, shift, args, replay_path, image_path, port):
        logger = logging.getLogger('replay')
        vm = self.get_vm(name='record' if record else 'replay')
        vm.set_console()
        if record:
            logger.info('recording the execution...')
            mode = 'record'
        else:
            logger.info('replaying the execution...')
            mode = 'replay'
            vm.add_args('-gdb', 'tcp::%d' % port, '-S')
        vm.add_args('-icount', 'shift=%s,rr=%s,rrfile=%s,rrsnapshot=init' %
                    (shift, mode, replay_path),
                    '-net', 'none')
        vm.add_args('-drive', 'file=%s,if=none' % image_path)
        if args:
            vm.add_args(*args)
        vm.launch()

        return vm

    @staticmethod
    def gdb_connect(host, port):
        # Set debug on connection to get the qSupport string.
        gdb.execute("set debug remote 1")
        r = gdb.execute(f"target remote {host}:{port}", False, True)
        gdb.execute("set debug remote 0")

        return r

    @staticmethod
    def get_pc():
        val = gdb.parse_and_eval("$pc")
        pc = int(val)

        return pc

    def check_pc(self, addr):
        pc = self.get_pc()
        if pc != addr:
            self.fail('Invalid PC (read %x instead of %x)' % (pc, addr))

    @staticmethod
    def gdb_step():
        gdb.execute("stepi")

    @staticmethod
    def gdb_bstep():
        gdb.execute("reverse-stepi")

    @staticmethod
    def vm_get_icount(vm):
        return vm.qmp('query-replay')['return']['icount']

    def reverse_debugging(self, shift=7, args=None):
        logger = logging.getLogger('replay')

        # Create qcow2 for snapshots
        logger.info('creating qcow2 image for VM snapshots')
        image_path = os.path.join(self.workdir, 'disk.qcow2')
        qemu_img = get_qemu_img(self)
        if qemu_img is None:
            self.skipTest('Could not find "qemu-img", which is required to '
                          'create the temporary qcow2 image')
        cmd = '%s create -f qcow2 %s 128M' % (qemu_img, image_path)
        subprocess.run(cmd, shell=True)

        replay_path = os.path.join(self.workdir, 'replay.bin')

        # Record the log.
        vm = self.run_vm(True, shift, args, replay_path, image_path, -1)
        while self.vm_get_icount(vm) <= self.STEPS:
            pass
        last_icount = self.vm_get_icount(vm)
        vm.shutdown()

        logger.info("recorded log with %s+ steps" % last_icount)

        # Replay and run debug commands.
        with Ports() as ports:
            port = ports.find_free_port()
            vm = self.run_vm(False, shift, args, replay_path, image_path, port)
        logger.info('Connecting to gdbstub')
        r = self.gdb_connect('127.0.0.1', port)
        if 'ReverseStep+' not in r:
            self.fail('Reverse step is not supported by QEMU')
        if 'ReverseContinue+' not in r:
            self.fail('Reverse continue is not supported by QEMU')

        logger.info('Stepping forward')
        steps = []
        # Record first instruction addresses.
        for _ in range(self.STEPS):
            pc = self.get_pc()
            logger.info('Saving position %x' % pc)
            steps.append(pc)
            self.gdb_step()

        # Visit the recorded instruction in reverse order.
        logger.info('Stepping backward')
        for addr in steps[::-1]:
            self.gdb_bstep()
            self.check_pc(addr)
            logger.info('Found position %x' % addr)

        # Visit the recorded instruction in forward order.
        logger.info('Stepping forward')
        for addr in steps:
            self.check_pc(addr)
            self.gdb_step()
            logger.info('Found position %x' % addr)

        # Set breakpoints for the instructions just stepped over.
        logger.info('Setting breakpoints')
        for addr in steps:
            # hardware breakpoint at addr with len=1
            gdb.execute(f"break *{hex(addr)}")

        # This may hit a breakpoint if first instructions are executed again.
        logger.info('Continuing execution')
        vm.qmp('replay-break', icount=last_icount - 1)
        # continue - will return after pausing.
        # This could stop at the end and get a T02 return, or by
        # re-executing one of the breakpoints and get a T05 return.
        gdb.execute("continue")
        if self.vm_get_icount(vm) == last_icount - 1:
            logger.info('Reached the end (icount %s)' % (last_icount - 1))
        else:
            logger.info('Hit a breakpoint again at %x (icount %s)' %
                        (self.get_pc(g), self.vm_get_icount(vm)))

        logger.info('Running reverse continue to reach %x' % steps[-1])
        # reverse continue - will return after stopping at the breakpoint.
        gdb.execute("reverse-continue")

        # Assume that none of the first instructions is executed again
        # breaking the order of the breakpoints.
        # steps[-1] is the first saved $pc in reverse order.
        self.check_pc(steps[-1])
        logger.info('Successfully reached %x' % steps[-1])

        logger.info('Exiting GDB and QEMU')

        vm.shutdown()

        self.assertTrue(True, "Test passed")
