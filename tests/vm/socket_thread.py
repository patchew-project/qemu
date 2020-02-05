#!/usr/bin/env python
#
# This python module defines a thread object which
# reads from a socket and dumps it to a file.
#
# The main use case is for reading QEMU console char dev and
# dumping them to a file either for debugging or for
# parsing by QEMU itself.
#
# Copyright 2020 Linaro
#
# Authors:
#  Robert Foley <robert.foley@linaro.org>
#
# This code is licensed under the GPL version 2 or later.  See
# the COPYING file in the top-level directory.
#
import sys
import re
import threading
import time
import traceback
import gettext

class SocketThread(threading.Thread):
    """ Implements the standard threading.Thread API.(start, join, etc.).
        dumps all characters received on socket into a file.
    """
    def __init__(self, socket, filename):
        super(SocketThread, self).__init__()
        self.alive = threading.Event()
        self.alive.set()
        self.socket = socket
        self.log_file = open(filename, "w")
        self.debug = True

    def receive(self):
        """Until the user calls join, we will read chars from
           the socket and dump them as is to the file."""
        self.socket.setblocking(0)
        self.socket.settimeout(1.0)
        while self.alive.isSet():
            try:
                chars = self.socket.recv(1)
            except:
                continue
            output = chars.decode("latin1")
            self.log_file.write("{}".format(output))
            # Flush the file since we need the characters to be
            # always up to date in case someone is reading the file
            # waiting for some characters to show up.
            self.log_file.flush()
        self.socket.setblocking(1)

    def run(self):
        """This is the main loop of the socket thread.
           Simply receive from the file until the user
           calls join."""
        while self.alive.isSet():
            try:
                self.receive()
            except Exception as e:
                sys.stderr.write("Exception encountered\n")
                traceback.print_exc()
                continue

    def join(self, timeout=None):
        """Time to destroy the thread.
           Clear the event to stop the thread, and wait for
           it to complete."""
        self.alive.clear()
        threading.Thread.join(self, timeout)
        self.log_file.close()
