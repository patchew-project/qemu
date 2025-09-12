# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#
# A cut-down copy of gdb.py from the avocado project:
#
# Copyright: Red Hat Inc. 2014
# Authors: Cleber Rosa <cleber@redhat.com>

__all__ = ["GDB", "GDBServer", "GDBRemote"]


import socket

from . import gdbmi_parser

#: How the remote protocol signals a transmission success (in ACK mode)
REMOTE_TRANSMISSION_SUCCESS = "+"

#: How the remote protocol signals a transmission failure (in ACK mode)
REMOTE_TRANSMISSION_FAILURE = "-"

#: How the remote protocol flags the start of a packet
REMOTE_PREFIX = b"$"

#: How the remote protocol flags the end of the packet payload, and that the
#: two digits checksum follow
REMOTE_DELIMITER = b"#"

#: Rather conservative default maximum packet size for clients using the
#: remote protocol. Individual connections can ask (and do so by default)
#: the server about the maximum packet size they can handle.
REMOTE_MAX_PACKET_SIZE = 1024


class UnexpectedResponseError(Exception):
    """A response different from the one expected was received from GDB"""


class ServerInitTimeoutError(Exception):
    """Server took longer than expected to initialize itself properly"""


class InvalidPacketError(Exception):
    """Packet received has invalid format"""


class NotConnectedError(Exception):
    """GDBRemote is not connected to a remote GDB server"""


class RetransmissionRequestedError(Exception):
    """Message integrity was not validated and retransmission is being requested"""



class GDBRemote:
    """A GDBRemote acts like a client that speaks the GDB remote protocol,
    documented at:

    https://sourceware.org/gdb/current/onlinedocs/gdb/Remote-Protocol.html

    Caveat: we currently do not support communicating with devices, only
    with TCP sockets. This limitation is basically due to the lack of
    use cases that justify an implementation, but not due to any technical
    shortcoming.
    """

    def __init__(self, host, port, no_ack_mode=True, extended_mode=True):
        """Initializes a new GDBRemote object.

        :param host: the IP address or host name
        :type host: str
        :param port: the port number where the the remote GDB is listening on
        :type port: int
        :param no_ack_mode: if the packet transmission confirmation mode should
                            be disabled
        :type no_ack_mode: bool
        :param extended_mode: if the remote extended mode should be enabled
        :type param extended_mode: bool
        """
        self.host = host
        self.port = port

        # Temporary holder for the class init attributes
        self._no_ack_mode = no_ack_mode
        self.no_ack_mode = False
        self._extended_mode = extended_mode
        self.extended_mode = False

        self._socket = None

    @staticmethod
    def checksum(input_message):
        """Calculates a remote message checksum.

        More details are available at:
        https://sourceware.org/gdb/current/onlinedocs/gdb/Overview.html

        :param input_message: the message input payload, without the
                              start and end markers
        :type input_message: bytes
        :returns: two byte checksum
        :rtype: bytes
        """
        total = 0
        for i in input_message:
            total += i
        result = total % 256

        return b"%02x" % result

    @staticmethod
    def encode(data):
        """Encodes a command.

        That is, add prefix, suffix and checksum.

        More details are available at:
        https://sourceware.org/gdb/current/onlinedocs/gdb/Overview.html

        :param data: the command data payload
        :type data: bytes
        :returns: the encoded command, ready to be sent to a remote GDB
        :rtype: bytes
        """
        return b"$%b#%b" % (data, GDBRemote.checksum(data))

    @staticmethod
    def decode(data):
        """Decodes a packet and returns its payload.

        More details are available at:
        https://sourceware.org/gdb/current/onlinedocs/gdb/Overview.html

        :param data: the command data payload
        :type data: bytes
        :returns: the encoded command, ready to be sent to a remote GDB
        :rtype: bytes
        :raises InvalidPacketError: if the packet is not well constructed,
                                    like in checksum mismatches
        """
        if data[0:1] != REMOTE_PREFIX:
            raise InvalidPacketError

        if data[-3:-2] != REMOTE_DELIMITER:
            raise InvalidPacketError

        payload = data[1:-3]
        checksum = data[-2:]

        if payload == b"":
            expected_checksum = b"00"
        else:
            expected_checksum = GDBRemote.checksum(payload)

        if checksum != expected_checksum:
            raise InvalidPacketError

        return payload

    def cmd(self, command_data, expected_response=None):
        """Sends a command data to a remote gdb server

        Limitations: the current version does not deal with retransmissions.

        :param command_data: the remote command to send the the remote stub
        :type command_data: str
        :param expected_response: the (optional) response that is expected
                                  as a response for the command sent
        :type expected_response: str
        :raises: RetransmissionRequestedError, UnexpectedResponseError
        :returns: raw data read from from the remote server
        :rtype: str
        :raises NotConnectedError: if the socket is not initialized
        :raises RetransmissionRequestedError: if there was a failure while
                                              reading the result of the command
        :raises UnexpectedResponseError: if response is unexpected
        """
        if self._socket is None:
            raise NotConnectedError

        data = self.encode(command_data)
        self._socket.send(data)

        if not self.no_ack_mode:
            transmission_result = self._socket.recv(1)
            if transmission_result == REMOTE_TRANSMISSION_FAILURE:
                raise RetransmissionRequestedError

        result = self._socket.recv(REMOTE_MAX_PACKET_SIZE)
        response_payload = self.decode(result)

        if expected_response is not None:
            if expected_response != response_payload:
                raise UnexpectedResponseError

        return response_payload

    def set_extended_mode(self):
        """Enable extended mode. In extended mode, the remote server is made
        persistent. The 'R' packet is used to restart the program being
        debugged. Original documentation at:

        https://sourceware.org/gdb/current/onlinedocs/gdb/Packets.html#extended-mode
        """
        self.cmd(b"!", b"OK")
        self.extended_mode = True

    def start_no_ack_mode(self):
        """Request that the remote stub disable the normal +/- protocol
        acknowledgments. Original documentation at:

        https://sourceware.org/gdb/current/onlinedocs/gdb/General-Query-Packets.html#QStartNoAckMode
        """
        self.cmd(b"QStartNoAckMode", b"OK")
        self.no_ack_mode = True

    def connect(self):
        """Connects to the remote target and initializes the chosen modes"""
        self._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._socket.connect((self.host, self.port))

        if self._no_ack_mode:
            self.start_no_ack_mode()

        if self._extended_mode:
            self.set_extended_mode()
