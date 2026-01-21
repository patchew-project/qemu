#!/usr/bin/env python3
#
# pylint: disable=C0114,R0903,R0912,R0914,R0915,R1732
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 2024 Mauro Carvalho Chehab <mchehab+huawei@kernel.org>

import argparse
import os
import re
import sys

from time import sleep

from qmp_helper import qmp, guid

class RawError:
    """
    Injects errors from a file containing raw data
    """

    SCRIPT_NAME = sys.argv[0]

    HELP=f"""
    Inject a CPER record from a previously recorded one.

    One or more CPER records can be recorded. The records to be
    injected are read from an specified file or from stdin and should
    have the format produced by this script when using --debug, e.g.:

    GUID: e19e3d16-bc11-11e4-9caa-c2051d5d46b0
    CPER:
        00000000  04 00 00 00 02 00 01 00 88 00 00 00 00 00 00 00   ................
        00000010  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00   ................
        00000020  00 00 00 00 00 00 00 00 00 20 05 00 08 02 00 03   ......... ......
        00000030  ff 0f 46 d6 80 00 00 00 ef be ad de 00 00 00 00   ..F.............
        00000040  ad 0b ba ab 00 00 00 00 00 20 04 00 04 01 00 03   ......... ......
        00000050  7f 00 54 00 00 00 00 00 ef be ad de 00 00 00 00   ..T.............
        00000060  ad 0b ba ab 00 00 00 00 00 00 05 00 18 00 00 00   ................
        00000070  ef be ad de 00 00 00 00 ab ba ba ab 00 00 00 00   ................
        00000080  00 00 00 00 00 00 00 00                           ........

    Multiple such records can be used. On such case, a delay will
    be introduced betewen them.

    All lines that can't be parsed will be silently ignored.
    As such, the output of this help can be piped to the raw-error
    generator with:

        {SCRIPT_NAME} -d raw-error -h | {SCRIPT_NAME} -d raw-error
    """

    def __init__(self, subparsers):
        """Initialize the error injection class and add subparser"""

        self.payload = bytearray()
        self.inj_type = None
        self.size = 0

        parser = subparsers.add_parser("raw-error",  aliases=['raw'],
                                       description=self.HELP,
                                       formatter_class=argparse.RawTextHelpFormatter)

        parser.add_argument("-f", "--file",
                            help="File name with the raw error data. '-' for stdin")
        parser.add_argument("-d", "--delay", type=lambda x: int(x, 0),
                            default=1,
                            help="Specify a delay between multiple CPER. Default=1")

        parser.set_defaults(func=self.send_cper)

    def send_cper(self, args):
        """Parse subcommand arguments and send a CPER via QMP"""

        if not args.file:
            args.file='-'

        is_guid = re.compile(r"^\s*guid:\s*(\w+\-\w+\-\w+\-\w+-\w+)", re.I)
        is_gesb = re.compile(r"^Generic Error Status Block.*:", re.I)
        is_gede = re.compile(r"^Generic Error Data Entry.*:", re.I)
        is_raw_data = re.compile(r"^Raw data.*:", re.I)
        is_payload = re.compile(r"^(Payload|CPER).*:", re.I)
        is_hexdump = re.compile(r"^(\s*[\da-f]........\s+)(.*)\s\s+.*", re.I)
        is_hex = re.compile(r"\b([\da-f].)\b", re.I)

        cper = []

        if args.file == "-":
            fp = sys.stdin
            if os.isatty(0):
                print("Using stdin. Press CTRL-D to finish input.")
            else:
                print("Reading from stdin pipe")
        else:
            try:
                fp = open(args.file, encoding="utf-8")
            except FileNotFoundError:
                sys.exit('File Not Found')

        guid_obj = None
        gebs = bytearray()
        gede = bytearray()
        raw_data = bytearray()
        payload = bytearray()
        ln_used = 0
        ln = 0

        cur = payload

        for ln, line in enumerate(fp):
            if match := is_guid.search(line):
                if guid_obj and payload:
                    cper.append({"guid": guid_obj, "raw-data": payload})
                    guid_obj = None
                    payload = bytearray()
                    gebs = bytearray()
                    gede = bytearray()

                guid_obj = guid.UUID(match.group(1))

                ln_used += 1
                continue

            if match := is_gesb.match(line):
                cur = gebs
                continue

            if match := is_gede.match(line):
                cur = gede
                continue

            if match := is_payload.match(line):
                cur = payload
                continue

            if match := is_raw_data.match(line):
                cur = raw_data
                continue

            new = is_hexdump.sub(r"\2", line)
            if new != line:
                if match := is_hex.findall(new):
                    for m in match:
                        cur.extend(bytes.fromhex(m))
                    ln_used += 1
                    continue
                continue

        if guid_obj and payload:
            cper.append({"guid": guid_obj,
                         "payload": payload,
                         "gede": gede,
                         "gebs": gebs,
                         "raw-data": raw_data})

        print(f"{ln} lines read, {ln - ln_used} lines ignored.")

        if fp is not sys.stdin:
            fp.close()

        qmp_cmd = qmp(args.host, args.port, args.debug)

        if not cper:
            sys.exit("Format of the file not recognized.")

        for i, c in enumerate(cper):
            if i:
                sleep(args.delay)

            ret = qmp_cmd.send_cper(c["guid"], c["payload"], gede=c["gede"],
                                    gebs=c["gebs"], raw_data=c["raw-data"])
            if not ret:
                return ret

        return True
