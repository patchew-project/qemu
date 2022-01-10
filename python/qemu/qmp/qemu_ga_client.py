"""
QEMU Guest Agent Client

Usage:

Start QEMU with:

# qemu [...] -chardev socket,path=/tmp/qga.sock,server=on,wait=off,id=qga0 \
  -device virtio-serial \
  -device virtserialport,chardev=qga0,name=org.qemu.guest_agent.0

Run the script:

$ qemu-ga-client --address=/tmp/qga.sock <command> [args...]

or

$ export QGA_CLIENT_ADDRESS=/tmp/qga.sock
$ qemu-ga-client <command> [args...]

For example:

$ qemu-ga-client cat /etc/resolv.conf
# Generated by NetworkManager
nameserver 10.0.2.3
$ qemu-ga-client fsfreeze status
thawed
$ qemu-ga-client fsfreeze freeze
2 filesystems frozen

See also: https://wiki.qemu.org/Features/QAPI/GuestAgent
"""

# Copyright (C) 2012 Ryota Ozaki <ozaki.ryota@gmail.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.

import argparse
import asyncio
import base64
import os
import random
import sys
from typing import (
    Any,
    Callable,
    Dict,
    Optional,
    Sequence,
)

from qemu.aqmp import ConnectError, SocketAddrT
from qemu.aqmp.legacy import QEMUMonitorProtocol


# This script has not seen many patches or careful attention in quite
# some time. If you would like to improve it, please review the design
# carefully and add docstrings at that point in time. Until then:

# pylint: disable=missing-docstring


class QemuGuestAgent(QEMUMonitorProtocol):
    def __getattr__(self, name: str) -> Callable[..., Any]:
        def wrapper(**kwds: object) -> object:
            return self.command('guest-' + name.replace('_', '-'), **kwds)
        return wrapper


class QemuGuestAgentClient:
    def __init__(self, address: SocketAddrT):
        self.qga = QemuGuestAgent(address)
        self.qga.connect(negotiate=False)

    def sync(self, timeout: Optional[float] = 3) -> None:
        # Avoid being blocked forever
        if not self.ping(timeout):
            raise EnvironmentError('Agent seems not alive')
        uid = random.randint(0, (1 << 32) - 1)
        while True:
            ret = self.qga.sync(id=uid)
            if isinstance(ret, int) and int(ret) == uid:
                break

    def __file_read_all(self, handle: int) -> bytes:
        eof = False
        data = b''
        while not eof:
            ret = self.qga.file_read(handle=handle, count=1024)
            _data = base64.b64decode(ret['buf-b64'])
            data += _data
            eof = ret['eof']
        return data

    def read(self, path: str) -> bytes:
        handle = self.qga.file_open(path=path)
        try:
            data = self.__file_read_all(handle)
        finally:
            self.qga.file_close(handle=handle)
        return data

    def info(self) -> str:
        info = self.qga.info()

        msgs = []
        msgs.append('version: ' + info['version'])
        msgs.append('supported_commands:')
        enabled = [c['name'] for c in info['supported_commands']
                   if c['enabled']]
        msgs.append('\tenabled: ' + ', '.join(enabled))
        disabled = [c['name'] for c in info['supported_commands']
                    if not c['enabled']]
        msgs.append('\tdisabled: ' + ', '.join(disabled))

        return '\n'.join(msgs)

    @classmethod
    def __gen_ipv4_netmask(cls, prefixlen: int) -> str:
        mask = int('1' * prefixlen + '0' * (32 - prefixlen), 2)
        return '.'.join([str(mask >> 24),
                         str((mask >> 16) & 0xff),
                         str((mask >> 8) & 0xff),
                         str(mask & 0xff)])

    def ifconfig(self) -> str:
        nifs = self.qga.network_get_interfaces()

        msgs = []
        for nif in nifs:
            msgs.append(nif['name'] + ':')
            if 'ip-addresses' in nif:
                for ipaddr in nif['ip-addresses']:
                    if ipaddr['ip-address-type'] == 'ipv4':
                        addr = ipaddr['ip-address']
                        mask = self.__gen_ipv4_netmask(int(ipaddr['prefix']))
                        msgs.append(f"\tinet {addr}  netmask {mask}")
                    elif ipaddr['ip-address-type'] == 'ipv6':
                        addr = ipaddr['ip-address']
                        prefix = ipaddr['prefix']
                        msgs.append(f"\tinet6 {addr}  prefixlen {prefix}")
            if nif['hardware-address'] != '00:00:00:00:00:00':
                msgs.append("\tether " + nif['hardware-address'])

        return '\n'.join(msgs)

    def ping(self, timeout: Optional[float]) -> bool:
        self.qga.settimeout(timeout)
        try:
            self.qga.ping()
        except asyncio.TimeoutError:
            return False
        return True

    def fsfreeze(self, cmd: str) -> object:
        if cmd not in ['status', 'freeze', 'thaw']:
            raise Exception('Invalid command: ' + cmd)
        # Can be int (freeze, thaw) or GuestFsfreezeStatus (status)
        return getattr(self.qga, 'fsfreeze' + '_' + cmd)()

    def fstrim(self, minimum: int) -> Dict[str, object]:
        # returns GuestFilesystemTrimResponse
        ret = getattr(self.qga, 'fstrim')(minimum=minimum)
        assert isinstance(ret, dict)
        return ret

    def suspend(self, mode: str) -> None:
        if mode not in ['disk', 'ram', 'hybrid']:
            raise Exception('Invalid mode: ' + mode)

        try:
            getattr(self.qga, 'suspend' + '_' + mode)()
            # On error exception will raise
        except asyncio.TimeoutError:
            # On success command will timed out
            return

    def shutdown(self, mode: str = 'powerdown') -> None:
        if mode not in ['powerdown', 'halt', 'reboot']:
            raise Exception('Invalid mode: ' + mode)

        try:
            self.qga.shutdown(mode=mode)
        except asyncio.TimeoutError:
            pass


def _cmd_cat(client: QemuGuestAgentClient, args: Sequence[str]) -> None:
    if len(args) != 1:
        print('Invalid argument')
        print('Usage: cat <file>')
        sys.exit(1)
    print(client.read(args[0]))


def _cmd_fsfreeze(client: QemuGuestAgentClient, args: Sequence[str]) -> None:
    usage = 'Usage: fsfreeze status|freeze|thaw'
    if len(args) != 1:
        print('Invalid argument')
        print(usage)
        sys.exit(1)
    if args[0] not in ['status', 'freeze', 'thaw']:
        print('Invalid command: ' + args[0])
        print(usage)
        sys.exit(1)
    cmd = args[0]
    ret = client.fsfreeze(cmd)
    if cmd == 'status':
        print(ret)
        return

    assert isinstance(ret, int)
    verb = 'frozen' if cmd == 'freeze' else 'thawed'
    print(f"{ret:d} filesystems {verb}")


def _cmd_fstrim(client: QemuGuestAgentClient, args: Sequence[str]) -> None:
    if len(args) == 0:
        minimum = 0
    else:
        minimum = int(args[0])
    print(client.fstrim(minimum))


def _cmd_ifconfig(client: QemuGuestAgentClient, args: Sequence[str]) -> None:
    assert not args
    print(client.ifconfig())


def _cmd_info(client: QemuGuestAgentClient, args: Sequence[str]) -> None:
    assert not args
    print(client.info())


def _cmd_ping(client: QemuGuestAgentClient, args: Sequence[str]) -> None:
    timeout = 3.0 if len(args) == 0 else float(args[0])
    alive = client.ping(timeout)
    if not alive:
        print("Not responded in %s sec" % args[0])
        sys.exit(1)


def _cmd_suspend(client: QemuGuestAgentClient, args: Sequence[str]) -> None:
    usage = 'Usage: suspend disk|ram|hybrid'
    if len(args) != 1:
        print('Less argument')
        print(usage)
        sys.exit(1)
    if args[0] not in ['disk', 'ram', 'hybrid']:
        print('Invalid command: ' + args[0])
        print(usage)
        sys.exit(1)
    client.suspend(args[0])


def _cmd_shutdown(client: QemuGuestAgentClient, args: Sequence[str]) -> None:
    assert not args
    client.shutdown()


_cmd_powerdown = _cmd_shutdown


def _cmd_halt(client: QemuGuestAgentClient, args: Sequence[str]) -> None:
    assert not args
    client.shutdown('halt')


def _cmd_reboot(client: QemuGuestAgentClient, args: Sequence[str]) -> None:
    assert not args
    client.shutdown('reboot')


commands = [m.replace('_cmd_', '') for m in dir() if '_cmd_' in m]


def send_command(address: str, cmd: str, args: Sequence[str]) -> None:
    if not os.path.exists(address):
        print(f"'{address}' not found. (Is QEMU running?)")
        sys.exit(1)

    if cmd not in commands:
        print('Invalid command: ' + cmd)
        print('Available commands: ' + ', '.join(commands))
        sys.exit(1)

    try:
        client = QemuGuestAgentClient(address)
    except ConnectError as err:
        print(err)
        if isinstance(err.exc, ConnectionError):
            print('(Is QEMU running?)')
        sys.exit(1)

    if cmd == 'fsfreeze' and args[0] == 'freeze':
        client.sync(60)
    elif cmd != 'ping':
        client.sync()

    globals()['_cmd_' + cmd](client, args)


def main() -> None:
    address = os.environ.get('QGA_CLIENT_ADDRESS')

    parser = argparse.ArgumentParser()
    parser.add_argument('--address', action='store',
                        default=address,
                        help='Specify a ip:port pair or a unix socket path')
    parser.add_argument('command', choices=commands)
    parser.add_argument('args', nargs='*')

    args = parser.parse_args()
    if args.address is None:
        parser.error('address is not specified')
        sys.exit(1)

    send_command(args.address, args.command, args.args)


if __name__ == '__main__':
    main()
