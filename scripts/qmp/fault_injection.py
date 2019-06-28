# Fault injection helper script based on top of QMP.
#
# Copyright (C) 2016,2019 GreenSocs SAS
#
# Authors:
#   Frederic Konrad <fred.konrad@greensocs.com>
#   Damien Hedde <damien.hedde@greensocs.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.
#

import json
import ast
import readline
import sys
import struct
import os
sys.path.append(os.path.join(os.path.dirname(__file__),
                             '..', '..', 'python', 'qemu'))
import qmp

def die(cause):
    print(sys.stderr.write('error: %s\n' % cause))
    sys.exit(1)

class FaultInjectionQMPError(Exception):
    def __init__(self, rsp):
        self.args = (rsp,)

class FaultInjectionFramework(qmp.QEMUMonitorProtocol):
    qemu_time = 0
    verbose = 0
    callback = {}
    endianness = None

    def print_v(self, msg, level):
        if level <= self.verbose:
            print(msg)

    def print_qemu_version(self):
        version = self._greeting['QMP']['version']['qemu']
        print('Connected to QEMU %d.%d.%d' % (version['major'],
                                                version['minor'],
                                                version['micro']))

    def __init__(self, address, verbose = 0):
        self.verbose = verbose
        qmp.QEMUMonitorProtocol.__init__(self, self.__get_address(address))

        try:
            self._greeting = qmp.QEMUMonitorProtocol.connect(self)
        except qmp.QMPConnectError:
            die('Didn\'t get QMP greeting message')
        except qmp.QMPCapabilitiesError:
            die('Could not negotiate capabilities')
        except self.error:
            die('Could not connect to %s' % address)

        self.print_qemu_version()
        self._completer = None
        self._pretty = False
        self._transmode = False
        self._actions = list()

    def set_endian_big(self):
        self.endianness = 'big'

    def set_endian_little(self):
        self.endianness = 'little'

    def time_print(self, arg):
        self.print_v('%sns: %s' % (self.qemu_time, arg), 1)

    def send(self, qmpcmd):
        self.print_v(qmpcmd, 2)
        resp = self.cmd_obj(qmpcmd)
        if resp is None:
            die('Disconnected')
        self.print_v(resp, 2)
        if 'error' in resp:
            raise FaultInjectionQMPError(resp)
        return resp

    def cont(self):
        qmpcmd = {'execute': 'cont', 'arguments': {}}
        self.send(qmpcmd)

    def run_once(self):
        # RUN the simulation until one event is received
        self.cont()
        # Wait for an event to appear
        done = False
        while done == False:
            for ev in self.get_events(True):
                self.print_v(ev, 2)
                if ev['event'] == 'TIME_NOTIFICATION':
                    data = ev['data']
                    self.qemu_time = data['time_ns'];
                    self.callback[data['event_id']]()
                    self.cont()
                    done = True
                elif ev['event'] == 'SHUTDOWN':
                    self.clear_events()
                    return True
        self.clear_events()
        return False

    def run(self):
        # RUN the simulation.
        self.time_print('Simulation is now running')
        # Wait for an event to appear
        shutdown_evt = False
        while shutdown_evt == False:
            shutdown_evt = self.run_once()
        self.close()

    def notify(self, time_ns, cb, relative = False):
        # Notify a callback at qemu time time_ns
        next_index = len(self.callback)
        elt = 0
        for elt in range(0, next_index + 1):
            if elt == next_index:
                break
            if self.callback[elt] == cb:
                break

        self.callback[elt] = cb
        if relative:
            self.time_print('Notify %s in %sns' % (cb, time_ns))
        else:
            self.time_print('Notify %s at %sns' % (cb, time_ns))
        qmpcmd = {'execute': 'time-notify',
                  'arguments': {'event_id': elt,
                                'time_ns': time_ns,
                                'pause' : True}}
        if relative:
            qmpcmd['arguments']['relative'] = True
        self.send(qmpcmd)

    def _pvmemwrite(self, virtual, address, value, cpu = None):
        # write a value to a virtual or physical address
        if type(value) is not list:
            value = list(value)

        self.time_print('write: @%s0x%08x size %d values ['
                        % ('V' if virtual else 'P', address, len(value))
                        + ','.join('%d' % i for i in value)
                        + '] from cpu %s' % (cpu))
        qmpcmd = {'execute': 'memwrite' if virtual else 'pmemwrite',
                  'arguments': {'addr': address, 'bytes': value}}
        if cpu is not None:
            qmpcmd['arguments']['cpu'] = cpu;
        rsp = self.send(qmpcmd)

    def _pvmemread(self, virtual, address, size, cpu = None):
        # read a value to a virtual or physical address

        self.time_print('read: @%s0x%08x size %d values from cpu %s'
                        % ('V' if virtual else 'P', address, size, cpu))
        qmpcmd = {'execute': 'memread' if virtual else 'pmemread',
                  'arguments': {'addr': address, 'size': size}}
        if cpu is not None:
            qmpcmd['arguments']['cpu'] = cpu;
        rsp = self.send(qmpcmd)
        value = bytearray(rsp['return']['bytes'])
        return value

    def _memstructformat(self, size):
        if self.endianness == 'little':
            s = '<'
        elif self.endianness == 'big':
            s = '>'
        else:
            #defaults to native
            s = '='
        s += {1:'B', 2:'H', 4:'I', 8:'Q'}[size]
        return s

    def read_mem(self, address, size, cpu = None):
        fmt = self._memstructformat(size)
        value = self._pvmemread(True, address, size, cpu)
        return struct.unpack(fmt, value)[0]

    def write_mem(self, address, size, value, cpu = None):
        fmt = self._memstructformat(size)
        value = bytearray(struct.pack(fmt, value))
        self._pvmemwrite(True, address, value, cpu)

    def read_pmem(self, address, size):
        fmt = self._memstructformat(size)
        value = self._pvmemread(False, address, size)
        return struct.unpack(fmt, value)[0]

    def write_pmem(self, address, size, value):
        fmt = self._memstructformat(size)
        value = bytearray(struct.pack(fmt, value))
        self._pvmemwrite(False, address, value)

    def get_qom_property(self, path, property):
        # Get a QOM property
        qmpcmd = {'execute': 'qom-get',
                  'arguments': {'path': path,
                                'property': property}}
        value = self.send(qmpcmd)['return']
        return value

    def set_qom_property(self, path, property, value):
        # Set a QOM property
        qmpcmd = {'execute': 'qom-set',
                  'arguments': {'path': path,
                                'property': property,
                                'value': value}}
        self.send(qmpcmd)

    def set_gpio(self, path, gpio, num, value):
        # Set a GPIO
        qmpcmd = {'execute': 'gpio-set',
                  'arguments': {'path': path, 'value': value}}
        if gpio is not None:
            qmpcmd['arguments']['gpio'] = gpio
        if num is not None:
            qmpcmd['arguments']['number'] = num
        self.send(qmpcmd)

    def help(self):
        msg = [
            "Fault Injection Framework Commands",
            "==================================\n",
            "cont()",
            " * Resume the simulation when the Virtual Machine is stopped.\n",
            "run()",
            " * Start the simulation when the notify are set.\n",
            "notify(delta_ns, cb)",
            " * Notify the callback cb in guest time delta_ns.\n",
            "write_mem(address, size, value, cpu)",
            " * write @value of size @size at virtual @address from @cpu.",
            " * @size is in bytes, supported values are 1, 2, 4 and 8.",
            " * @cpu is the cpu id.\n",
            "read_mem(address, size, cpu)",
            " * read a value of size @size at virtual @address from @cpu.",
            " * @size is in bytes, supported values are 1, 2, 4 and 8.",
            " * @cpu is the cpu id.",
            " * returns the value.\n",
            "write_pmem(address, size, value)",
            " * write @value of size @size at physical @address.",
            " * @size is in bytes, supported values are 1, 2, 4 and 8.\n",
            "read_pmem(address, size)",
            " * read a value of size @size at physical @address.",
            " * @size is in bytes, supported values are 1, 2, 4 and 8.",
            " * returns the value.\n",
            "get_qom_property(path, property)",
            " * Get a qom property.",
            " * Returns the qom property named @property in @path.\n",
            "set_qom_property(path, property, value)",
            " * Set the property named @property in @path with @value.\n",
            "set_gpio(path, gpio, num, value)",
            " * Set the gpio named @gpio number @num in @path with the @val.",
            " * @val is a boolean.\n"
            ]
        for m in msg:
            print(m)

    def __get_address(self, arg):
        """
        Figure out if the argument is in the port:host form, if it's not it's
        probably a file path.
        """
        addr = arg.split(':')
        if len(addr) == 2:
            try:
                port = int(addr[1])
            except ValueError:
                raise QMPShellBadPort
            return ( addr[0], port )
        # socket path
        return arg

