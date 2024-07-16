# Test class and utilities for functional tests
#
# Copyright 2018, 2024 Red Hat, Inc.
#
# Original Author (Avocado-based tests):
#  Cleber Rosa <crosa@redhat.com>
#
# Adaption for standalone version:
#  Thomas Huth <thuth@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import logging
import os
import pycotap
import shutil
import subprocess
import sys
import tempfile
import time
import uuid
import unittest

from pathlib import Path
from qemu.machine import QEMUMachine
from qemu.utils import (get_info_usernet_hostfwd_port, kvm_available,
                        tcg_available)

def _source_dir():
    # Determine top-level directory of the QEMU sources
    return Path(__file__).parent.parent.parent.parent

def _build_dir():
    root = os.getenv('QEMU_BUILD_ROOT')
    if root is not None:
        return Path(root)
    # Makefile.mtest only exists in build dir, so if it is available, use CWD
    if os.path.exists('Makefile.mtest'):
        return Path(os.getcwd())

    root = os.path.join(_source_dir(), 'build')
    if os.path.exists(root):
        return Path(root)

    raise Exception("Cannot identify build dir, set QEMU_BUILD_ROOT")

def has_cmd(name, args=None):
    """
    This function is for use in a @skipUnless decorator, e.g.:

        @skipUnless(*has_cmd('sudo -n', ('sudo', '-n', 'true')))
        def test_something_that_needs_sudo(self):
            ...
    """

    if args is None:
        args = ('which', name)

    try:
        _, stderr, exitcode = run_cmd(args)
    except Exception as e:
        exitcode = -1
        stderr = str(e)

    if exitcode != 0:
        cmd_line = ' '.join(args)
        err = f'{name} required, but "{cmd_line}" failed: {stderr.strip()}'
        return (False, err)
    else:
        return (True, '')

def has_cmds(*cmds):
    """
    This function is for use in a @skipUnless decorator and
    allows checking for the availability of multiple commands, e.g.:

        @skipUnless(*has_cmds(('cmd1', ('cmd1', '--some-parameter')),
                              'cmd2', 'cmd3'))
        def test_something_that_needs_cmd1_and_cmd2(self):
            ...
    """

    for cmd in cmds:
        if isinstance(cmd, str):
            cmd = (cmd,)

        ok, errstr = has_cmd(*cmd)
        if not ok:
            return (False, errstr)

    return (True, '')

def run_cmd(args):
    subp = subprocess.Popen(args,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE,
                            universal_newlines=True)
    stdout, stderr = subp.communicate()
    ret = subp.returncode

    return (stdout, stderr, ret)

def is_readable_executable_file(path):
    return os.path.isfile(path) and os.access(path, os.R_OK | os.X_OK)

def _console_interaction(test, success_message, failure_message,
                         send_string, keep_sending=False, vm=None):
    assert not keep_sending or send_string
    if vm is None:
        vm = test.vm
    console = vm.console_file
    console_logger = logging.getLogger('console')
    while True:
        if send_string:
            vm.console_socket.sendall(send_string.encode())
            if not keep_sending:
                send_string = None # send only once
        try:
            msg = console.readline().decode().strip()
        except UnicodeDecodeError:
            msg = None
        if not msg:
            continue
        console_logger.debug(msg)
        if success_message is None or success_message in msg:
            break
        if failure_message and failure_message in msg:
            console.close()
            fail = 'Failure message found in console: "%s". Expected: "%s"' % \
                    (failure_message, success_message)
            test.fail(fail)

def interrupt_interactive_console_until_pattern(test, success_message,
                                                failure_message=None,
                                                interrupt_string='\r'):
    """
    Keep sending a string to interrupt a console prompt, while logging the
    console output. Typical use case is to break a boot loader prompt, such:

        Press a key within 5 seconds to interrupt boot process.
        5
        4
        3
        2
        1
        Booting default image...

    :param test: a  test containing a VM that will have its console
                 read and probed for a success or failure message
    :type test: :class:`qemu_test.QemuSystemTest`
    :param success_message: if this message appears, test succeeds
    :param failure_message: if this message appears, test fails
    :param interrupt_string: a string to send to the console before trying
                             to read a new line
    """
    _console_interaction(test, success_message, failure_message,
                         interrupt_string, True)

def wait_for_console_pattern(test, success_message, failure_message=None,
                             vm=None):
    """
    Waits for messages to appear on the console, while logging the content

    :param test: a test containing a VM that will have its console
                 read and probed for a success or failure message
    :type test: :class:`qemu_test.QemuSystemTest`
    :param success_message: if this message appears, test succeeds
    :param failure_message: if this message appears, test fails
    """
    _console_interaction(test, success_message, failure_message, None, vm=vm)

def exec_command(test, command):
    """
    Send a command to a console (appending CRLF characters), while logging
    the content.

    :param test: a test containing a VM.
    :type test: :class:`qemu_test.QemuSystemTest`
    :param command: the command to send
    :type command: str
    """
    _console_interaction(test, None, None, command + '\r')

def exec_command_and_wait_for_pattern(test, command,
                                      success_message, failure_message=None):
    """
    Send a command to a console (appending CRLF characters), then wait
    for success_message to appear on the console, while logging the.
    content. Mark the test as failed if failure_message is found instead.

    :param test: a test containing a VM that will have its console
                 read and probed for a success or failure message
    :type test: :class:`qemu_test.QemuSystemTest`
    :param command: the command to send
    :param success_message: if this message appears, test succeeds
    :param failure_message: if this message appears, test fails
    """
    _console_interaction(test, success_message, failure_message, command + '\r')

class QemuBaseTest(unittest.TestCase):

    qemu_bin = os.getenv('QEMU_TEST_QEMU_BINARY')

    BUILD_DIR = _build_dir()

    workdir = None
    log = logging.getLogger('qemu-test')

    def setUp(self, bin_prefix):
        self.assertIsNotNone(self.qemu_bin, 'QEMU_TEST_QEMU_BINARY must be set')

        self.workdir = os.path.join(self.BUILD_DIR, 'tests/functional')
        self.workdir = os.path.join(self.workdir, self.id())
        if not os.path.exists(self.workdir):
            os.makedirs(self.workdir)

    def main():
        tr = pycotap.TAPTestRunner(message_log = pycotap.LogMode.LogToError,
                                   test_output_log = pycotap.LogMode.LogToError)
        unittest.main(testRunner = tr)


class QemuSystemTest(QemuBaseTest):
    """Facilitates system emulation tests."""

    cpu = None
    machine = None
    _machinehelp = None

    def setUp(self):
        self._vms = {}

        super().setUp('qemu-system-')

    def set_machine(self, machinename):
        # TODO: We should use QMP to get the list of available machines
        if not self._machinehelp:
            self._machinehelp = run_cmd([self.qemu_bin, '-M', 'help'])[0];
        if self._machinehelp.find(machinename) < 0:
            self.skipTest('no support for machine ' + machinename)
        self.machine = machinename

    def require_accelerator(self, accelerator):
        """
        Requires an accelerator to be available for the test to continue

        It takes into account the currently set qemu binary.

        If the check fails, the test is canceled.  If the check itself
        for the given accelerator is not available, the test is also
        canceled.

        :param accelerator: name of the accelerator, such as "kvm" or "tcg"
        :type accelerator: str
        """
        checker = {'tcg': tcg_available,
                   'kvm': kvm_available}.get(accelerator)
        if checker is None:
            self.skipTest("Don't know how to check for the presence "
                          "of accelerator %s" % accelerator)
        if not checker(qemu_bin=self.qemu_bin):
            self.skipTest("%s accelerator does not seem to be "
                          "available" % accelerator)

    def require_netdev(self, netdevname):
        netdevhelp = run_cmd([self.qemu_bin,
                             '-M', 'none', '-netdev', 'help'])[0];
        if netdevhelp.find('\n' + netdevname + '\n') < 0:
            self.skipTest('no support for " + netdevname + " networking')

    def require_device(self, devicename):
        devhelp = run_cmd([self.qemu_bin,
                           '-M', 'none', '-device', 'help'])[0];
        if devhelp.find(devicename) < 0:
            self.skipTest('no support for device ' + devicename)

    def _new_vm(self, name, *args):
        vm = QEMUMachine(self.qemu_bin, base_temp_dir=self.workdir)
        self.log.debug('QEMUMachine "%s" created', name)
        self.log.debug('QEMUMachine "%s" temp_dir: %s', name, vm.temp_dir)
        self.log.debug('QEMUMachine "%s" log_dir: %s', name, vm.log_dir)
        if args:
            vm.add_args(*args)
        return vm

    @property
    def vm(self):
        return self.get_vm(name='default')

    def get_vm(self, *args, name=None):
        if not name:
            name = str(uuid.uuid4())
        if self._vms.get(name) is None:
            self._vms[name] = self._new_vm(name, *args)
            if self.cpu is not None:
                self._vms[name].add_args('-cpu', self.cpu)
            if self.machine is not None:
                self._vms[name].set_machine(self.machine)
        return self._vms[name]

    def set_vm_arg(self, arg, value):
        """
        Set an argument to list of extra arguments to be given to the QEMU
        binary. If the argument already exists then its value is replaced.

        :param arg: the QEMU argument, such as "-cpu" in "-cpu host"
        :type arg: str
        :param value: the argument value, such as "host" in "-cpu host"
        :type value: str
        """
        if not arg or not value:
            return
        if arg not in self.vm.args:
            self.vm.args.extend([arg, value])
        else:
            idx = self.vm.args.index(arg) + 1
            if idx < len(self.vm.args):
                self.vm.args[idx] = value
            else:
                self.vm.args.append(value)

    def tearDown(self):
        for vm in self._vms.values():
            vm.shutdown()
        super().tearDown()
