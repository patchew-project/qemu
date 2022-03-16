#
# Copyright (C) 2022 Greensocs
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.
#

"""
usage: qmp-send [-h] [-f FILE] [-s SOCKET] [-v] [-p] [--wrap ...]

Send raw qmp commands to qemu as long as they succeed. It either connects to a
remote qmp server using the provided socket or wrap the qemu process. It stops
sending the provided commands when a command fails (disconnection or error
response).

optional arguments:
  -h, --help            show this help message and exit
  -f FILE, --file FILE  Input file containing the commands
  -s SOCKET, --socket SOCKET
                        < UNIX socket path | TCP address:port >
  -v, --verbose         Verbose (echo commands sent and received)
  -p, --pretty          Pretty-print JSON
  --wrap ...            QEMU command line to invoke

When qemu wrap option is used, this script waits for qemu to terminate but
never send any quit or kill command. This needs to be done manually.
"""

import argparse
import contextlib
import json
import logging
import os
from subprocess import Popen
import sys
from typing import List, TextIO

from qemu.aqmp import ConnectError, QMPError, SocketAddrT
from qemu.aqmp.legacy import (
    QEMUMonitorProtocol,
    QMPBadPortError,
    QMPMessage,
)


LOG = logging.getLogger(__name__)


class QmpRawDecodeError(Exception):
    """
    Exception for raw qmp decoding

    msg: exception message
    lineno: input line of the error
    colno: input column of the error
    """
    def __init__(self, msg: str, lineno: int, colno: int):
        self.msg = msg
        self.lineno = lineno
        self.colno = colno
        super().__init__(f"{msg}: line {lineno} column {colno}")


class QMPSendError(QMPError):
    """
    QMP Send Base error class.
    """


class QMPSend(QEMUMonitorProtocol):
    """
    QMP Send class.
    """
    def __init__(self, address: SocketAddrT,
                 pretty: bool = False,
                 verbose: bool = False,
                 server: bool = False):
        super().__init__(address, server=server)
        self._verbose = verbose
        self._pretty = pretty
        self._server = server

    def setup_connection(self) -> None:
        """Setup the connetion with the remote client/server."""
        if self._server:
            self.accept()
        else:
            self.connect()

    def _print(self, qmp_message: object) -> None:
        jsobj = json.dumps(qmp_message,
                           indent=4 if self._pretty else None,
                           sort_keys=self._pretty)
        print(str(jsobj))

    def execute_cmd(self, cmd: QMPMessage) -> None:
        """Execute a qmp command."""
        if self._verbose:
            self._print(cmd)
        resp = self.cmd_obj(cmd)
        if resp is None:
            raise QMPSendError("Disconnected")
        if self._verbose:
            self._print(resp)
        if 'error' in resp:
            raise QMPSendError(f"Command failed: {resp['error']}")


def raw_load(file: TextIO) -> List[QMPMessage]:
    """parse a raw qmp command file.

    JSON formatted commands can expand on several lines but must
    be separated by an end-of-line (two commands can not share the
    same line).
    File must not end with empty lines.
    """
    cmds: List[QMPMessage] = []
    linecnt = 0
    while True:
        buf = file.readline()
        if not buf:
            return cmds
        prev_err_pos = None
        buf_linecnt = 1
        while True:
            try:
                cmds.append(json.loads(buf))
                break
            except json.JSONDecodeError as err:
                if prev_err_pos == err.pos:
                    # adding a line made no progress so
                    #  + either we're at EOF and json data is truncated
                    #  + or the parsing error is before
                    raise QmpRawDecodeError(err.msg, linecnt + err.lineno,
                                            err.colno) from err
                prev_err_pos = err.pos
            buf += file.readline()
            buf_linecnt += 1
        linecnt += buf_linecnt


def report_error(msg: str) -> None:
    """Write an error to stderr."""
    sys.stderr.write('ERROR: %s\n' % msg)


def main() -> None:
    """
    qmp-send entry point: parse command line arguments and start the REPL.
    """
    parser = argparse.ArgumentParser(
            description="""
            Send raw qmp commands to qemu as long as they succeed. It either
            connects to a remote qmp server using the provided socket or wrap
            the qemu process. It stops sending the provided commands when a
            command fails (disconnection or error response).
            """,
            epilog="""
            When qemu wrap option is used, this script waits for qemu
            to terminate but never send any quit or kill command. This
            needs to be done manually.
            """)

    parser.add_argument('-f', '--file', action='store',
                        help='Input file containing the commands')
    parser.add_argument('-s', '--socket', action='store',
                        help='< UNIX socket path | TCP address:port >')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Verbose (echo commands sent and received)')
    parser.add_argument('-p', '--pretty', action='store_true',
                        help='Pretty-print JSON')

    parser.add_argument('--wrap', nargs=argparse.REMAINDER,
                        help='QEMU command line to invoke')

    args = parser.parse_args()

    socket = args.socket
    wrap_qemu = args.wrap is not None

    if wrap_qemu:
        if len(args.wrap) != 0:
            qemu_cmdline = args.wrap
        else:
            qemu_cmdline = ["qemu-system-x86_64"]
        if socket is None:
            socket = "qmp-send-wrap-%d" % os.getpid()
        qemu_cmdline += ["-qmp", "unix:%s" % socket]

    try:
        address = QMPSend.parse_address(socket)
    except QMPBadPortError:
        parser.error(f"Bad port number: {socket}")
        return  # pycharm doesn't know error() is noreturn

    try:
        with open(args.file, mode='rt', encoding='utf8') as file:
            qmp_cmds = raw_load(file)
    except QmpRawDecodeError as err:
        report_error(str(err))
        sys.exit(1)

    try:
        with QMPSend(address, args.pretty, args.verbose,
                     server=wrap_qemu) as qmp:
            # starting with python 3.7 we could use contextlib.nullcontext
            qemu = Popen(qemu_cmdline) if wrap_qemu else contextlib.suppress()
            with qemu:
                try:
                    qmp.setup_connection()
                except ConnectError as err:
                    if isinstance(err.exc, OSError):
                        report_error(f"Couldn't connect to {socket}: {err!s}")
                    else:
                        report_error(str(err))
                    sys.exit(1)
                try:
                    for cmd in qmp_cmds:
                        qmp.execute_cmd(cmd)
                except QMPError as err:
                    report_error(str(err))
                    sys.exit(1)
    finally:
        if wrap_qemu:
            os.unlink(socket)


if __name__ == '__main__':
    main()
