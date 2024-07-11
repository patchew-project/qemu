# Test class and utilities for functional tests
#
# Copyright 2018, 2024 Red Hat, Inc.
#
# Original Author (Avocado-based tests):
#  Cleber Rosa <crosa@redhat.com>
#
# Adaption for pytest based version:
#  Thomas Huth <thuth@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import hashlib
import logging
import os
import shutil
import subprocess
import sys
import tempfile
import time
import uuid
import unittest

from qemu.machine import QEMUMachine
from qemu.utils import (get_info_usernet_hostfwd_port, kvm_available,
                        tcg_available)

BUILD_DIR = os.getenv('PYTEST_BUILD_ROOT')
SOURCE_DIR = os.getenv('PYTEST_SOURCE_ROOT')

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
    :type test: :class:`qemu_pytest.QemuSystemTest`
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
    :type test: :class:`qemu_pytest.QemuSystemTest`
    :param success_message: if this message appears, test succeeds
    :param failure_message: if this message appears, test fails
    """
    _console_interaction(test, success_message, failure_message, None, vm=vm)

def exec_command(test, command):
    """
    Send a command to a console (appending CRLF characters), while logging
    the content.

    :param test: a test containing a VM.
    :type test: :class:`qemu_pytest.QemuSystemTest`
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
    :type test: :class:`qemu_pytest.QemuSystemTest`
    :param command: the command to send
    :param success_message: if this message appears, test succeeds
    :param failure_message: if this message appears, test fails
    """
    _console_interaction(test, success_message, failure_message, command + '\r')

class QemuBaseTest(unittest.TestCase):

    # default timeout for all tests, can be overridden
    timeout = 120

    qemu_bin = os.getenv('PYTEST_QEMU_BINARY')

    workdir = os.path.join(BUILD_DIR, 'tests/pytest')
    logdir = os.path.join(BUILD_DIR, 'tests/pytest')

    cpu = None
    machine = None

    log = logging.getLogger('qemu-pytest')

    def setUp(self, bin_prefix):
        self.assertIsNotNone(BUILD_DIR, 'PYTEST_BUILD_ROOT must be set')
        self.assertIsNotNone(SOURCE_DIR,'PYTEST_SOURCE_ROOT must be set')
        self.assertIsNotNone(self.qemu_bin, 'PYTEST_QEMU_BINARY must be set')

    def check_hash(self, file_name, expected_hash):
        if not expected_hash:
            return True
        if len(expected_hash) == 32:
            sum_prog = 'md5sum'
        elif len(expected_hash) == 40:
            sum_prog = 'sha1sum'
        elif len(expected_hash) == 64:
            sum_prog = 'sha256sum'
        elif len(expected_hash) == 128:
            sum_prog = 'sha512sum'
        else:
            raise Exception("unknown hash type")
        checksum = subprocess.check_output([sum_prog, file_name]).split()[0]
        return expected_hash == checksum.decode("utf-8")

    def fetch_asset(self, url, asset_hash):
        cache_dir = os.path.expanduser("~/.cache/qemu/download")
        if not os.path.exists(cache_dir):
            os.makedirs(cache_dir)
        fname = os.path.join(cache_dir,
                             hashlib.sha1(url.encode("utf-8")).hexdigest())
        if os.path.exists(fname) and self.check_hash(fname, asset_hash):
            return fname
        logging.debug("Downloading %s to %s...", url, fname)
        subprocess.check_call(["wget", "-c", url, "-O", fname + ".download"])
        os.rename(fname + ".download", fname)
        return fname


class QemuSystemTest(QemuBaseTest):
    """Facilitates system emulation tests."""

    def setUp(self):
        self._vms = {}

        super().setUp('qemu-system-')

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
            self.cancel("Don't know how to check for the presence "
                        "of accelerator %s" % accelerator)
        if not checker(qemu_bin=self.qemu_bin):
            self.cancel("%s accelerator does not seem to be "
                        "available" % accelerator)

    def require_netdev(self, netdevname):
        netdevhelp = run_cmd([self.qemu_bin,
                             '-M', 'none', '-netdev', 'help'])[0];
        if netdevhelp.find('\n' + netdevname + '\n') < 0:
            self.cancel('no support for user networking')

    def require_multiprocess(self):
        """
        Test for the presence of the x-pci-proxy-dev which is required
        to support multiprocess.
        """
        devhelp = run_cmd([self.qemu_bin,
                           '-M', 'none', '-device', 'help'])[0];
        if devhelp.find('x-pci-proxy-dev') < 0:
            self.cancel('no support for multiprocess device emulation')

    def _new_vm(self, name, *args):
        self._sd = tempfile.TemporaryDirectory(prefix="qemu_")
        vm = QEMUMachine(self.qemu_bin, base_temp_dir=self.workdir,
                         log_dir=self.logdir)
        self.log.debug('QEMUMachine "%s" created', name)
        self.log.debug('QEMUMachine "%s" temp_dir: %s', name, vm.temp_dir)
        self.log.debug('QEMUMachine "%s" log_dir: %s', name, vm.log_dir)
        if args:
            vm.add_args(*args)
        return vm

    def get_qemu_img(self):
        self.log.debug('Looking for and selecting a qemu-img binary')

        # If qemu-img has been built, use it, otherwise the system wide one
        # will be used.
        qemu_img = os.path.join(BUILD_DIR, 'qemu-img')
        if not os.path.exists(qemu_img):
            qemu_img = find_command('qemu-img', False)
        if qemu_img is False:
            self.cancel('Could not find "qemu-img"')

        return qemu_img

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
        self._sd = None
        super().tearDown()


class QemuUserTest(QemuBaseTest):
    """Facilitates user-mode emulation tests."""

    def setUp(self):
        self._ldpath = []
        super().setUp('qemu-')

    def add_ldpath(self, ldpath):
        self._ldpath.append(os.path.abspath(ldpath))

    def run(self, bin_path, args=[]):
        qemu_args = " ".join(["-L %s" % ldpath for ldpath in self._ldpath])
        bin_args = " ".join(args)
        return process.run("%s %s %s %s" % (self.qemu_bin, qemu_args,
                                            bin_path, bin_args))
