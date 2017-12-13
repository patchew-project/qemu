# QEMU qtest library
#
# Copyright (C) 2015 Red Hat Inc.
#
# Authors:
#  Fam Zheng <famz@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.
#
# Based on qmp.py.
#

import socket
import sys
import os
import qemu
import re
import unittest
import logging
import tempfile


qemu_prog = os.environ.get('QEMU_PROG', 'qemu')
qemu_opts = os.environ.get('QEMU_OPTIONS', '').strip().split(' ')

test_dir = os.environ.get('TEST_DIR', tempfile.gettempdir())
output_dir = os.environ.get('OUTPUT_DIR', '.')
qemu_default_machine = os.environ.get('QEMU_DEFAULT_MACHINE')

socket_scm_helper = os.environ.get('SOCKET_SCM_HELPER', 'socket_scm_helper')
debug = False


def filter_qmp_event(event):
    '''Filter a QMP event dict'''
    event = dict(event)
    if 'timestamp' in event:
        event['timestamp']['seconds'] = 'SECS'
        event['timestamp']['microseconds'] = 'USECS'
    return event

class QEMUQtestProtocol(object):
    def __init__(self, address, server=False):
        """
        Create a QEMUQtestProtocol object.

        @param address: QEMU address, can be either a unix socket path (string)
                        or a tuple in the form ( address, port ) for a TCP
                        connection
        @param server: server mode, listens on the socket (bool)
        @raise socket.error on socket connection errors
        @note No connection is established, this is done by the connect() or
              accept() methods
        """
        self._address = address
        self._sock = self._get_sock()
        if server:
            self._sock.bind(self._address)
            self._sock.listen(1)

    def _get_sock(self):
        if isinstance(self._address, tuple):
            family = socket.AF_INET
        else:
            family = socket.AF_UNIX
        return socket.socket(family, socket.SOCK_STREAM)

    def connect(self):
        """
        Connect to the qtest socket.

        @raise socket.error on socket connection errors
        """
        self._sock.connect(self._address)

    def accept(self):
        """
        Await connection from QEMU.

        @raise socket.error on socket connection errors
        """
        self._sock, _ = self._sock.accept()

    def cmd(self, qtest_cmd):
        """
        Send a qtest command on the wire.

        @param qtest_cmd: qtest command text to be sent
        """
        self._sock.sendall(qtest_cmd + "\n")

    def close(self):
        self._sock.close()

    def settimeout(self, timeout):
        self._sock.settimeout(timeout)


class QEMUQtestMachine(qemu.QEMUMachine):
    '''A QEMU VM'''

    def __init__(self, binary, args=None, name=None, test_dir="/var/tmp",
                 socket_scm_helper=None):
        if name is None:
            name = "qemu-%d" % os.getpid()
        super(QEMUQtestMachine,
              self).__init__(binary, args, name=name, test_dir=test_dir,
                             socket_scm_helper=socket_scm_helper)
        self._qtest = None
        self._qtest_path = os.path.join(test_dir, name + "-qtest.sock")

    def _base_args(self):
        args = super(QEMUQtestMachine, self)._base_args()
        args.extend(['-qtest', 'unix:path=' + self._qtest_path,
                     '-machine', 'accel=qtest'])
        return args

    def _pre_launch(self):
        super(QEMUQtestMachine, self)._pre_launch()
        self._qtest = QEMUQtestProtocol(self._qtest_path, server=True)

    def _post_launch(self):
        super(QEMUQtestMachine, self)._post_launch()
        self._qtest.accept()

    def _post_shutdown(self):
        super(QEMUQtestMachine, self)._post_shutdown()
        self._remove_if_exists(self._qtest_path)

    def qtest(self, cmd):
        '''Send a qtest command to guest'''
        return self._qtest.cmd(cmd)


def createQtestMachine(path_suffix=''):
    name = "qemu%s-%d" % (path_suffix, os.getpid())
    return QEMUQtestMachine(qemu_prog, qemu_opts, name=name,
                             test_dir=test_dir,
                             socket_scm_helper=socket_scm_helper)

class QMPTestCase(unittest.TestCase):
    '''Abstract base class for QMP test cases'''

    index_re = re.compile(r'([^\[]+)\[([^\]]+)\]')

    def dictpath(self, d, path):
        '''Traverse a path in a nested dict'''
        for component in path.split('/'):
            m = QMPTestCase.index_re.match(component)
            if m:
                component, idx = m.groups()
                idx = int(idx)

            if not isinstance(d, dict) or component not in d:
                self.fail('failed path traversal for "%s" in "%s"' % (path,
                                                                      str(d)))
            d = d[component]

            if m:
                if not isinstance(d, list):
                    self.fail(('path component "%s" in "%s" is not a list ' +
                              'in "%s"') % (component, path, str(d)))
                try:
                    d = d[idx]
                except IndexError:
                    self.fail(('invalid index "%s" in path "%s" ' +
                              'in "%s"') % (idx, path, str(d)))
        return d

    def flatten_qmp_object(self, obj, output=None, basestr=''):
        if output is None:
            output = dict()
        if isinstance(obj, list):
            for i in range(len(obj)):
                self.flatten_qmp_object(obj[i], output, basestr + str(i) + '.')
        elif isinstance(obj, dict):
            for key in obj:
                self.flatten_qmp_object(obj[key], output, basestr + key + '.')
        else:
            output[basestr[:-1]] = obj # Strip trailing '.'
        return output

    def qmp_to_opts(self, obj):
        obj = self.flatten_qmp_object(obj)
        output_list = list()
        for key in obj:
            output_list += [key + '=' + obj[key]]
        return ','.join(output_list)

    def assert_qmp_absent(self, d, path):
        try:
            result = self.dictpath(d, path)
        except AssertionError:
            return
        self.fail('path "%s" has value "%s"' % (path, str(result)))

    def assert_qmp(self, d, path, value):
        '''Assert that the value for a specific path in a QMP dict matches'''
        result = self.dictpath(d, path)
        self.assertEqual(result, value, ('values not equal "%s" ' +
                         'and "%s"') % (str(result), str(value)))


def notrun(reason):
    '''Skip this test suite'''
    print '%s not run: %s' % (seq, reason)
    sys.exit(0)

def verify_platform(supported_oses=['linux']):
    if True not in [sys.platform.startswith(x) for x in supported_oses]:
        notrun('not suitable for this OS: %s' % sys.platform)
