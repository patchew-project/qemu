#!/usr/bin/env python3
#
# pylint: disable=C0114,R0903,R0912
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 2024 Mauro Carvalho Chehab <mchehab+huawei@kernel.org>

import argparse
import sys

from time import sleep
from random import randrange
from qmp_helper import qmp, util, cper_guid

class FuzzyError:
    """
    Implements Fuzzy error injection via GHES
    """

    def __init__(self, subparsers):
        """Initialize the error injection class and add subparser"""

        # as defined at UEFI spec v2.10, section N.2.2
        # Sizes here are just hints to have some default
        self.types = {
            "proc-generic": {
                "guid": cper_guid.CPER_PROC_GENERIC,
                "default_size": 192
            },
            "proc-x86": {
                "guid": cper_guid.CPER_PROC_X86,
                "default_size": 64
            },
            "proc-itanium": {
                "guid": cper_guid.CPER_PROC_ITANIUM,
                "default_size": 64
            },
            "proc-arm": {
                "guid": cper_guid.CPER_PROC_ARM,
                "default_size": 72
            },
            "platform-mem": {
                "guid": cper_guid.CPER_PLATFORM_MEM,
                "default_size": 80
            },
            "platform-mem2": {
                "guid": cper_guid.CPER_PLATFORM_MEM2,
                "default_size": 96
            },
            "pcie": {
                "guid": cper_guid.CPER_PCIE,
                "default_size": 208
            },
            "pci-bus": {
                "guid": cper_guid.CPER_PCI_BUS,
                "default_size": 72
            },
            "pci-dev": {
                "guid": cper_guid.CPER_PCI_DEV,
                "default_size": 56
            },
            "firmware-error": {
                "guid": cper_guid.CPER_FW_ERROR,
                "default_size": 32
            },
            "dma-generic": {
                "guid": cper_guid.CPER_DMA_GENERIC,
                "default_size": 32
            },
            "dma-vt": {
                "guid": cper_guid.CPER_DMA_VT,
                "default_size": 144
            },
            "dma-iommu": {
                "guid": cper_guid.CPER_DMA_IOMMU,
                "default_size": 144
            },
            "ccix-per": {
                "guid": cper_guid.CPER_CCIX_PER,
                "default_size": 36
            },
            "cxl-prot-err": {
                "guid": cper_guid.CPER_CXL_PROT_ERR,
                "default_size": 116
            },
            "cxl-evt-media": {
                "guid": cper_guid.CPER_CXL_EVT_GEN_MEDIA,
                "default_size": 32
            },
            "cxl-evt-dram": {
                "guid": cper_guid.CPER_CXL_EVT_DRAM,
                "default_size": 64
            },
            "cxl-evt-mem-module": {
                "guid": cper_guid.CPER_CXL_EVT_MEM_MODULE,
                "default_size": 64
            },
            "cxl-evt-mem-sparing": {
                "guid": cper_guid.CPER_CXL_EVT_MEM_SPARING,
                "default_size": 64
            },
            "cxl-evt-phy-sw": {
                "guid": cper_guid.CPER_CXL_EVT_PHY_SW,
                "default_size": 64
            },
            "cxl-evt-virt-sw": {
                "guid": cper_guid.CPER_CXL_EVT_VIRT_SW,
                "default_size": 64
            },
            "cxl-evt-mdl-port": {
                "guid": cper_guid.CPER_CXL_EVT_MLD_PORT,
                "default_size": 64
            },
            "cxl-evt-dyna-cap": {
                "guid": cper_guid.CPER_CXL_EVT_DYNA_CAP,
                "default_size": 64
            },
            "fru-mem-poison": {
                "guid": cper_guid.CPER_FRU_MEM_POISON,
                "default_size": 72
            },
        }

        DESC = "Inject fuzzy test CPER packets"

        parser = subparsers.add_parser("fuzzy-test", aliases=['fuzzy'],
                                       help=DESC, description=DESC,
                                       formatter_class=argparse.RawTextHelpFormatter)
        g_fuzzy = parser.add_argument_group("Fuzz testing error inject")


        cper_types = ",".join(self.types.keys())

        g_fuzzy.add_argument("-T", "--type",
                            help=f"Type of the error: {cper_types}")
        g_fuzzy.add_argument("--min-size",
                    type=lambda x: int(x, 0),
                    help="Minimal size of the CPER")
        g_fuzzy.add_argument("--max-size",
                    type=lambda x: int(x, 0),
                    help="Maximal size of the CPER")
        g_fuzzy.add_argument("-z", "--zero", action="store_true",
                            help="Zero all bytes of the CPER payload (default: %(default)s)")
        g_fuzzy.add_argument("-t", "--timeout", type=float,
                    default=30.0,
                    help="Specify timeout for CPER send retries (default: %(default)s seconds)")
        g_fuzzy.add_argument("-d", "--delay", type=float,
                    default=0,
                    help="Specify a delay between multiple CPER (default: %(default)s)")
        g_fuzzy.add_argument("-c", "--count", type=int,
                    default=1,
                    help="Specify the number of CPER records to be sent (default: %(default)s)")

        parser.set_defaults(func=self.send_cper)

    def send_cper(self, args):
        """Parse subcommand arguments and send a CPER via QMP"""

        qmp_cmd = qmp(args.host, args.port, args.debug)

        args.count = max(args.count, 1)

        for i in range(0, args.count):
            if i:
                if args.delay > 0:
                    sleep(args.delay)

            # Handle global parameters
            if args.type:
                if not args.type in self.types:
                    sys.exit(f"Invalid type: {args.type}")

                inj_type = args.type
            else:
                i = randrange(len(self.types))
                keys = list(self.types.keys())
                inj_type = keys[i]

            inject = self.types[inj_type]

            guid = inject["guid"]
            min_size = inject["default_size"]
            max_size = min_size

            if args.min_size:
                min_size = args.min_size

            if args.max_size:
                max_size = args.max_size

            size = min_size

            if min_size < max_size:
                size += randrange(max_size - min_size)

            data = bytearray()

            if not args.zero:
                for i in range(size):
                    util.data_add(data, randrange(256), 1)
            else:
                for i in range(size):
                    util.data_add(data, 0, 1)

            print(f"Injecting {inj_type} with {size} bytes")
            ret = qmp_cmd.send_cper(guid, data, timeout=args.timeout)
            if ret and ret != "OK":
                return ret
