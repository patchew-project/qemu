# Simple functional tests for SPICE functionality
#
# Copyright (c) 2019 Red Hat, Inc.
#
# Author:
#  Cleber Rosa <crosa@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import socket
import struct

from avocado_qemu import Test
from avocado.utils.network import find_free_port


class Spice(Test):

    def test_protocol(self):
        """
        :avocado: tags=quick
        :avocado: tags=spice
        """
        port = find_free_port(5001, 5500, sequent=False)
        self.vm.add_args('-nodefaults', '-S',
                         '-spice', 'port=%d,disable-ticketing' % port)
        self.vm.launch()

        RED_MAGIC = 0x51444552
        MAJOR_VERSION = 0x2

        client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        client.connect(('127.0.0.1', port))
        red_link_mess = struct.pack('<10I',
                                    RED_MAGIC,      # magic
                                    MAJOR_VERSION,  # major version
                                    0x0,            # minor version
                                    0x18,           # size in bytes from here
                                    0x0,            # connection id
                                    0x1,            # channel type RED_CHANNEL_MAIN
                                    0x0,            # channel id
                                    0x0,            # number of common caps
                                    0x0,            # number of channel caps
                                    0x14)           # caps offset from size
        client.send(red_link_mess)

        RED_LINK_REPLY_BASE_FMT = '<5I'  # magic, major, minor, size, error
        red_link_reply = client.recv(struct.calcsize(RED_LINK_REPLY_BASE_FMT))
        (magic, major, minor, size, error) = struct.unpack_from(RED_LINK_REPLY_BASE_FMT,
                                                                red_link_reply)
        self.assertEqual(magic, RED_MAGIC, "Mismatch of MAGIC number")
        self.assertEqual(major, MAJOR_VERSION, "Mismatch of major protocol version")
        self.assertEqual(error, 0x0, "Unexpected error reported by server")
