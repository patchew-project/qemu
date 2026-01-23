#!/usr/bin/env python3
#
# pylint: disable=C0114,R0903
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 2024 Mauro Carvalho Chehab <mchehab+huawei@kernel.org>

from qmp_helper import qmp, util, cper_guid

class PciBusError:
    """
    Implements PCI Express bus error injection via GHES
    """

    def __init__(self, subparsers):
        """Initialize the error injection class and add subparser"""

        # Valid values
        self.valid_bits = {
            "status": util.bit(0),
            "type": util.bit(1),
            "bus-id": util.bit(2),
            "bus-addr": util.bit(3),
            "bus-data": util.bit(4),
            "command": util.bit(5),
            "requestor-id": util.bit(6),
            "completer-id": util.bit(7),
            "target-id": util.bit(8),
        }

        self.bus_command_bits = {
            "pci": 0,               # Bit 56 is zero
            "pci-x": util.bit(56)
        }

        self.data = bytearray()

        parser = subparsers.add_parser("pci-bus",
                                       description="Generate PCI/PCI-X bus error CPER")
        g_pci = parser.add_argument_group("PCI/PCI-X bus error")

        valid_bits = ",".join(self.valid_bits.keys())
        bus_command_bits = ",".join(self.bus_command_bits.keys())

        g_pci.add_argument("-v", "--valid",
                            help=f"Valid bits: {valid_bits}")
        g_pci.add_argument("-s", "--error-status",
                            type=lambda x: int(x, 0),
                            help="Error Status")
        g_pci.add_argument("-t", "--error-type",
                            type=lambda x: int(x, 0),
                            help="Error type")
        g_pci.add_argument("-b", "--bus-number",
                            type=lambda x: int(x, 0),
                            help="Bus number")
        g_pci.add_argument("-S", "--segment-number",
                            type=lambda x: int(x, 0),
                            help="Segment number")
        g_pci.add_argument("-a", "--bus-address",
                            type=lambda x: int(x, 0),
                            help="Bus address")
        g_pci.add_argument("-d", "--bus-data",
                            type=lambda x: int(x, 0),
                            help="Bus data")
        g_pci.add_argument("-c", "--bus-command",
                            help=f"bus-command: {bus_command_bits}")
        g_pci.add_argument("-r", "--bus-requestor",
                            type=lambda x: int(x, 0),
                            help="Bus requestor ID")
        g_pci.add_argument("-C", "--bus-completer",
                            type=lambda x: int(x, 0),
                            help="Bus completer ID")
        g_pci.add_argument("-i", "--target-id",
                            type=lambda x: int(x, 0),
                            help="Target ID")

        parser.set_defaults(func=self.send_cper)

    def send_cper(self, args):
        """Parse subcommand arguments and send a CPER via QMP"""

        qmp_cmd = qmp(args.host, args.port, args.debug)

        cper = {}
        arg = vars(args)

        # Handle global parameters
        if args.valid:
            valid_init = False
            cper["valid"] = util.get_choice(name="valid",
                                            value=args.valid,
                                            choices=self.valid_bits)
        else:
            cper["valid"] = 0
            valid_init = True

        if args.bus_command:
            cper["bus-command"] = util.get_choice(name="bus-command",
                                                    value=args.bus_command,
                                                    choices=self.bus_command_bits)
        if valid_init:
            if args.error_status:
                cper["valid"] |= self.valid_bits["status"]

            if args.error_type:
                cper["valid"] |= self.valid_bits["type"]

            if args.bus_number and args.bus_segment:
                cper["valid"] |= self.valid_bits["bus-id"]

            if args.bus_address:
                cper["valid"] |= self.valid_bits["bus-address"]

            if args.bus_data:
                cper["valid"] |= self.valid_bits["bus-data"]

            if args.bus_requestor:
                cper["valid"] |= self.valid_bits["requestor-id"]

            if args.bus_completer:
                cper["valid"] |= self.valid_bits["completer-id"]

            if args.target_id:
                cper["valid"] |= self.valid_bits["target-id"]

        util.data_add(self.data, cper["valid"], 8)
        util.data_add(self.data, arg.get("error-status", 0), 8)
        util.data_add(self.data, arg.get("error-type", util.bit(0)), 2)

        # Bus ID
        util.data_add(self.data, arg.get("bus-number", 0), 1)
        util.data_add(self.data, arg.get("segment-number", 0), 1)

        # Reserved
        util.data_add(self.data, 0, 4)

        util.data_add(self.data, arg.get("bus-address", 0), 8)
        util.data_add(self.data, arg.get("bus-data", 0), 8)

        util.data_add(self.data, cper.get("bus-command", 0), 8)

        util.data_add(self.data, arg.get("bus-requestor", 0), 8)
        util.data_add(self.data, arg.get("bus-completer", 0), 8)
        util.data_add(self.data, arg.get("target-id", 0), 8)

        return qmp_cmd.send_cper(cper_guid.CPER_PCI_BUS, self.data)
