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

import logging
import os
import re
import subprocess
from pygdbmi.gdbcontroller import GdbController
from pygdbmi.constants import GdbTimeoutError


from qemu_test import LinuxKernelTest, get_qemu_img
from qemu_test.ports import Ports


class GDB:
    def __init__(self, gdb_path, echo=True, suffix='# ', prompt="$ "):
        gdb_cmd = [gdb_path, "-q", "--interpreter=mi2"]
        self.gdbmi = GdbController(gdb_cmd)
        self.echo = echo
        self.suffix = suffix
        self.prompt = prompt


    def get_payload(self, response, kind):
        output = []
        for o in response:
            # Unpack payloads of the same type.
            _type, _, payload, *_ = o.values()
            if _type == kind:
                output += [payload]

        # Some output lines do not end with \n but begin with it,
        # so remove the leading \n and merge them with the next line
        # that ends with \n.
        lines = [line.lstrip('\n') for line in output]
        lines = "".join(lines)
        lines = lines.splitlines(keepends=True)

        return lines


    def cli(self, cmd, timeout=4.0):
        self.response = self.gdbmi.write(cmd, timeout_sec=timeout)
        self.cmd_output = self.get_payload(self.response, "console")
        if self.echo:
            print(self.suffix + self.prompt + cmd)

            if len(self.cmd_output) > 0:
                cmd_output = self.suffix.join(self.cmd_output)
                print(self.suffix + cmd_output, end="")

        return self


    def get_addr(self):
        pattern = r"0x[0-9A-Fa-f]+"
        cmd_output = "".join(self.cmd_output)
        match = re.search(pattern, cmd_output)

        return int(match[0], 16) if match else None


    def get_log(self):
        r = self.get_payload(self.response, kind="log")
        r = "".join(r)

        return r


    def get_console(self):
        r = "".join(self.cmd_output)

        return r


    def exit(self):
        self.gdbmi.exit()


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
            logger.info('Recording the execution...')
            mode = 'record'
        else:
            logger.info('Replaying the execution...')
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
    def vm_get_icount(vm):
        return vm.qmp('query-replay')['return']['icount']

    def reverse_debugging(self, shift=7, args=None):
        logger = logging.getLogger('replay')

        # Create qcow2 for snapshots
        logger.info('Creating qcow2 image for VM snapshots')
        image_path = os.path.join(self.workdir, 'disk.qcow2')
        qemu_img = get_qemu_img(self)
        if qemu_img is None:
            self.skipTest('Could not find "qemu-img", which is required to '
                          'create the temporary qcow2 image')
        cmd = '%s create -f qcow2 %s 128M' % (qemu_img, image_path)
        r = subprocess.run(cmd, capture_output=True, shell=True, text=True)
        logger.info(r.args)
        logger.info(r.stdout)

        replay_path = os.path.join(self.workdir, 'replay.bin')

        # Record the log.
        vm = self.run_vm(True, shift, args, replay_path, image_path, -1)
        while self.vm_get_icount(vm) <= self.STEPS:
            pass
        last_icount = self.vm_get_icount(vm)
        vm.shutdown()

        logger.info("Recorded log with %s+ steps" % last_icount)

        # Replay and run debug commands.
        gdb_cmd = os.getenv('QEMU_TEST_GDB')
        if not gdb_cmd:
            test.skipTest(f"Test skipped because there is no GDB available!")

        with Ports() as ports:
            port = ports.find_free_port()
            vm = self.run_vm(False, shift, args, replay_path, image_path, port)

        try:
            gdb = GDB(gdb_cmd)

            logger.info('Connecting to gdbstub...')

            gdb.cli("set debug remote 1")

            c = gdb.cli(f"target remote localhost:{port}").get_console()
            if not f"Remote debugging using localhost:{port}" in c:
                self.fail("Could not connect to gdbstub!")

            # Remote debug messages are in 'log' payloads.
            r = gdb.get_log()
            if 'ReverseStep+' not in r:
                self.fail('Reverse step is not supported by QEMU')
            if 'ReverseContinue+' not in r:
                self.fail('Reverse continue is not supported by QEMU')

            gdb.cli("set debug remote 0")

            logger.info('Stepping forward')
            steps = []
            # Record first instruction addresses.
            for _ in range(self.STEPS):
                pc = gdb.cli("print $pc").get_addr()
                logger.info('Saving position %x' % pc)
                steps.append(pc)

                gdb.cli("stepi")

            # Visit the recorded instructions in reverse order.
            logger.info('Stepping backward')
            for saved_pc in steps[::-1]:
                logger.info('Found position %x' % saved_pc)
                gdb.cli("reverse-stepi")
                pc = gdb.cli("print $pc").get_addr()
                if pc != saved_pc:
                    logger.info('Invalid PC (read %x instead of %x)' % (pc, saved_pc))
                    self.fail('Reverse stepping failed!')

            # Visit the recorded instructions in forward order.
            logger.info('Stepping forward')
            for saved_pc in steps:
                logger.info('Found position %x' % saved_pc)
                pc = gdb.cli("print $pc").get_addr()
                if pc != saved_pc:
                    logger.info('Invalid PC (read %x instead of %x)' % (pc, saved_pc))
                    self.fail('Forward stepping failed!')

                gdb.cli("stepi")

            # Set breakpoints for the instructions just stepped over.
            logger.info('Setting breakpoints')
            for saved_pc in steps:
                gdb.cli(f"break *{hex(saved_pc)}")

            # This may hit a breakpoint if first instructions are executed again.
            logger.info('Continuing execution')
            vm.qmp('replay-break', icount=last_icount - 1)
            # continue - will return after pausing.
            # This can stop at the end of the replay-break and gdb gets a SIGINT,
            # or by re-executing one of the breakpoints and gdb stops at a
            # breakpoint.
            gdb.cli("continue")

            pc = gdb.cli("print $pc").get_addr()
            current_icount = self.vm_get_icount(vm)
            if current_icount == last_icount - 1:
                print(f"# **** Hit replay-break at icount={current_icount}, pc={hex(pc)} ****")
                logger.info('Reached the end (icount %s)' % (current_icount))
            else:
                print(f"# **** Hit breakpoint at icount={current_icount}, pc={hex(pc)} ****")
                logger.info('Hit a breakpoint again at %x (icount %s)' %
                            (pc, current_icount))

            logger.info('Running reverse continue to reach %x' % steps[-1])
            # reverse-continue - will return after stopping at the breakpoint.
            gdb.cli("reverse-continue")

            # Assume that none of the first instructions are executed again
            # breaking the order of the breakpoints.
            # steps[-1] is the first saved $pc in reverse order.
            pc = gdb.cli("print $pc").get_addr()
            first_pc_in_rev_order = steps[-1]
            if pc == first_pc_in_rev_order:
                print(f"# **** Hit breakpoint at the first PC in reverse order ({hex(pc)}) ****")
                logger.info('Successfully reached breakpoint at %x' % first_pc_in_rev_order)
            else:
                logger.info('Failed to reach breakpoint at %x' % first_pc_in_rev_order)
                self.fail("'reverse-continue' did not hit the first PC in reverse order!")

            logger.info('Exiting GDB and QEMU...')
            gdb.exit()
            vm.shutdown()

            logger.info('Test passed.')

        except GdbTimeoutError:
            self.fail("Connection to gdbstub timeouted...")
