#!/usr/bin/env python3
#
# pylint: disable=R0903,R0912,R0913,R0915,R0917,R1713,E1121,C0302,W0613
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Copyright (C) 2025 Mauro Carvalho Chehab <mchehab+huawei@kernel.org>

"""
Helper classes to decode a generic error data entry.

By purpose, the logic here is independent of the logic inside qmp_helper
and other modules. With a different implementation, it is more likely to
discover bugs at the error injection logic. Also, as this can be used to
dump errors injected by reproducing an error mesage or for fuzzy error
injection, it can't rely on the encoding logic inside each module of
ghes_inject.py.

To make the decoder simple, the decode logic here is at field level, not
trying to decode bitmaps.
"""

from typing import Optional

class DecodeField():
    """
    Helper functions to decode a field, printing its results
    """

    def __init__(self, cper_data: bytearray):
        """Initialize the decoder with a cper bytearray"""
        self.data = cper_data
        self.pos = 0
        self.past_end = False

    @property
    def remaining(self):
        """Returns the number of bytes not decoded yet"""
        return max(0, len(self.data) - self.pos)

    @property
    def is_end(self):
        """
        Returns true if all bytes were decoded and it didn't try
        to read past the end.
        """
        if not self.past_end and self.pos == len(self.data):
            return True

        return False

    def decode(self, name: str, size: int, ftype: str,
               pos: Optional[int] = None,
               show_incomplete: Optional[bool] = False) -> None:
        """
        Decodes and outputs a specified field from an ACPI table.

        For ints, we opted to decode them byte by byte, thus not being
        limited to an integer max size.

        Arguments:
            name: name of the field
            size: number of bytes of the field
            ftype: field type (str, int, guid, bcd)
            pos: if specified, show a field at the specific position. If
                 not, use last position and increment it with size at the end
        """
        if pos:
            cur_pos = pos
        else:
            cur_pos = self.pos

        try:
            if cur_pos + size > len(self.data):
                if not pos:
                    self.past_end = True

                if not show_incomplete:
                    decoded = "N/A"
                    return None

            raw_data = self.data[cur_pos:cur_pos + size]

            decoded = ""
            if ftype == "str":
                failures = False
                for b in raw_data:
                    if b >= 32 and b <= 126:            # pylint: disable=R1716
                        decoded += chr(b)
                    elif b:
                        decoded += '.'
                        failures = True
                    else:
                        decoded += r'\x0'

                if failures:
                    decoded += " # warning: non-ascii chars found"

                if self.past_end:
                    if decoded:
                        decoded += " "
                    decoded += "EOL"

            elif ftype == "int":
                i = 0
                for b in reversed(raw_data):
                    i += 1
                    if len(raw_data) > 8 and i > 1:
                        decoded += " "

                    decoded += f"{b:02x}"

                if self.past_end:
                    if decoded:
                        decoded += " "
                    decoded += "EOL"

            elif ftype == "guid":
                if len(raw_data) != 16 or size != 16:
                    decoded = "Invalid GUID"
                else:
                    for b in reversed(raw_data[0:4]):
                        decoded += f"{b:02x}"

                    decoded += "-"

                    for b in reversed(raw_data[4:6]):
                        decoded += f"{b:02x}"

                    decoded += "-"

                    for b in reversed(raw_data[6:8]):
                        decoded += f"{b:02x}"

                    decoded += "-"

                    for b in raw_data[8:10]:
                        decoded += f"{b:02x}"

                    decoded += "-"

                    for b in raw_data[10:]:
                        decoded += f"{b:02x}"

                    raw_data = decoded

            elif ftype == "bcd":
                val = 0
                for b in raw_data:
                    if (b & 0xf0) > 9 or (b & 0x0f) > 9:
                        raise ValueError("Invalid BCD value")
                    val = (val << 4) | (b & 0x0f)

                decoded = f"{val:0{size * 2}x}"

                if self.past_end:
                    if decoded:
                        decoded += " "
                    decoded += "EOL"
            else:
                decoded = f"Warning: Unknown format {ftype}"

        except ValueError as e:
            decoded = f"Error decoding {e}"

        finally:
            print(f"{name:<26s}: {decoded}")
            if not pos:
                self.pos += size

        return raw_data


class DecodeProcGeneric():
    """
    Class to decode a Generic Processor Error as defined at
    UEFI 2.1 - N.2.2 Section Descriptor
    """
    # GUID for Generic Processor Error
    guid = "9876ccad-47b4-4bdb-b65e-16f193c4f3db"

    fields = [
        ("Validation Bits", 8, "int"),
        ("Processor Type", 1, "int"),
        ("Processor ISA", 1, "int"),
        ("Processor Error Type", 1, "int"),
        ("Operation", 1, "int"),
        ("Flags", 1, "int"),
        ("Level", 1, "int"),
        ("Reserved", 2, "int"),
        ("CPU Version Info", 8, "int"),
        ("CPU Brand String", 128, "str"),
        ("Processor ID", 8, "int"),
        ("Target Address", 8, "int"),
        ("Requestor Identifier", 8, "int"),
        ("Responder Identifier", 8, "int"),
        ("Instruction IP", 8, "int")
    ]

    def __init__(self, cper: DecodeField):
        self.cper = cper

    def decode(self, guid):
        """Decode Generic Processor Error"""
        print("Generic Processor Error")

        for name, size, ftype in self.fields:
            self.cper.decode(name, size, ftype)

    @staticmethod
    def decode_list():
        """
        Returns a tuple with the GUID and class
        """
        return [(DecodeProcGeneric.guid, DecodeProcGeneric)]

class DecodeProcX86():
    """
    Class to decode an x86 Processor Error as defined at
    UEFI 2.1 - N.2.2 Section Descriptor
    """

    # GUID for x86 Processor Error
    guid = "dc3ea0b0-a144-4797-b95b-53fa242b6e1d"

    pei_fields = [
        ("Error Structure Type", 16, "guid"),
        ("Validation Bits", 8, "int"),
        ("Check Information", 8, "int"),
        ("Target Identifier", 8, "int"),
        ("Requestor Identifier", 8, "int"),
        ("Responder Identifier", 8, "int"),
        ("Instruction Pointer", 8, "int")
    ]

    def __init__(self, cper: DecodeField):
        self.cper = cper

    def decode(self, guid):
        """Decode x86 Processor Error"""
        print("x86 Processor Error")

        val = self.cper.decode("Validation Bits", 8, "int")
        try:
            val_bits = int.from_bytes(val, byteorder='little')
        except ValueError, TypeError:
            val_bits = 0

        error_info_num = (val_bits >> 2) & 0x3f    # bits 2-7
        context_info_num = (val_bits >> 8) & 0xff  # bits 8-13

        self.cper.decode("Local APIC_ID", 8, "int")
        self.cper.decode("CPUID Info", 48, "int")

        for pei in range(0, error_info_num):
            if self.cper.past_end:
                return

            print()
            print(f"Processor Error Info {pei}")
            for name, size, ftype in self.pei_fields:
                self.cper.decode(name, size, ftype)

        for ctx in range(0, context_info_num):
            if self.cper.past_end:
                return

            print()
            print(f"Context {ctx}")

            self.cper.decode("Register Context Type", 2, "int")

            val = self.cper.decode("Register Array Size", 2, "int")
            try:
                context_size = int(int.from_bytes(val, byteorder='little') / 8)
            except ValueError, TypeError:
                context_size = 0

            self.cper.decode("MSR Address", 4, "int")
            self.cper.decode("MM Register Address", 8, "int")

            for reg in range(0, context_size):
                if self.cper.past_end:
                    return
                self.cper.decode(f"Register offset {reg:<3}", 8, "int")

    @staticmethod
    def decode_list():
        """
        Returns a tuple with the GUID and class
        """
        return [(DecodeProcX86.guid, DecodeProcX86)]

class DecodeProcItanium():
    """
    Class to decode an Itanium Processor Error as defined at
    UEFI 2.1 - N.2.2 Section Descriptor
    """

    # GUID for Itanium Processor Error
    guid = "e429faf1-3cb7-11d4-bca7-0080c73c8881"

    def __init__(self, cper: DecodeField):
        self.cper = cper

    def decode(self, guid):
        """
        Decode Itanium Processor Error.

        Itanum processors stopped being sold in 2021. Probably not much
        sense implementing a decoder for it.
        """

        print("Itanium Processor Error")

        remaining = self.cper.remaining
        if remaining:
            print()
            self.cper.decode("Data", remaining, "int")

    @staticmethod
    def decode_list():
        """
        Returns a tuple with the GUID and class
        """
        return [(DecodeProcItanium.guid, DecodeProcItanium)]


class DecodeProcArm():
    """
    Class to decode an ARM Processor Error as defined at
    UEFI 2.6 - N.2.2 Section Descriptor
    """

    # GUID for ARM Processor Error
    guid = "e19e3d16-bc11-11e4-9caa-c2051d5d46b0"

    arm_pei_fields = [
        ("Version",              1, "int"),
        ("Length",               1, "int"),
        ("valid",                2, "int"),
        ("type",                 1, "int"),
        ("multiple-error",       2, "int"),
        ("flags",                1, "int"),
        ("error-info",           8, "int"),
        ("virt-addr",            8, "int"),
        ("phy-addr",             8, "int"),
    ]

    def __init__(self, cper: DecodeField):
        self.cper = cper

    def decode(self, guid):
        """Decode Processor ARM"""

        print("ARM Processor Error")

        start = self.cper.pos

        self.cper.decode("Valid", 4, "int")

        val = self.cper.decode("Error Info num", 2, "int")
        try:
            error_info_num = int.from_bytes(val, byteorder='little')
        except ValueError, TypeError:
            error_info_num = 0

        val = self.cper.decode("Context Info num", 2, "int")
        try:
            context_info_num = int.from_bytes(val, byteorder='little')
        except ValueError, TypeError:
            context_info_num = 0

        val = self.cper.decode("Section Length", 4, "int")
        try:
            section_length = int.from_bytes(val, byteorder='little')
        except ValueError, TypeError:
            section_length = 0

        self.cper.decode("Error affinity level", 1, "int")
        self.cper.decode("Reserved", 3, "int")
        self.cper.decode("MPIDR_EL1", 8, "int")
        self.cper.decode("MIDR_EL1", 8, "int")
        self.cper.decode("Running State", 4, "int")
        self.cper.decode("PSCI State", 4, "int")

        for pei in range(0, error_info_num):
            if self.cper.past_end:
                return

            print()
            print(f"Processor Error Info {pei}")
            for name, size, ftype in self.arm_pei_fields:
                self.cper.decode(name, size, ftype)

        for ctx in range(0, context_info_num):
            if self.cper.past_end:
                return

            print()
            print(f"Context {ctx}")
            self.cper.decode("Version", 2, "int")
            self.cper.decode("Register Context Type", 2, "int")
            val = self.cper.decode("Register Array Size", 4, "int")
            try:
                context_size = int(int.from_bytes(val, byteorder='little') / 8)
            except ValueError:
                context_size = 0

            for reg in range(0, context_size):
                if self.cper.past_end:
                    return
                self.cper.decode(f"Register {reg:<3}", 8, "int")

        remaining = max(section_length + start - self.cper.pos, 0)
        if remaining:
            print()
            self.cper.decode("Vendor data", remaining, "int")

    @staticmethod
    def decode_list():
        """
        Returns a tuple with the GUID and class
        """
        return [(DecodeProcArm.guid, DecodeProcArm)]


class DecodePlatformMem():
    """
    Class to decode a Platform Memory Error as defined at
    UEFI 2.1 - N.2.2 Section Descriptor
    """

    # GUID for Platform Memory Error
    guid = "a5bc1114-6f64-4ede-b863-3e83ed7c83b1"

    fields = [
        ("Validation Bits", 8, "int"),
        ("Error Status", 8, "int"),
        ("Physical Address", 8, "int"),
        ("Physical Address Mask", 8, "int"),
        ("Node", 2, "int"),
        ("Card", 2, "int"),
        ("Module", 2, "int"),
        ("Bank", 2, "int"),
        ("Device", 2, "int"),
        ("Row", 2, "int"),
        ("Column", 2, "int"),
        ("Bit Position", 2, "int"),
        ("Requestor ID", 8, "int"),
        ("Responder ID", 8, "int"),
        ("Target ID", 8, "int"),
        ("Memory Error Type", 1, "int"),
        ("Extended", 1, "int"),
        ("Rank Number", 2, "int"),
        ("Card Handle", 2, "int"),
        ("Module Handle", 2, "int")
    ]

    def __init__(self, cper: DecodeField):
        self.cper = cper

    def decode(self, guid):
        """Decode Platform Memory Error"""
        print("Platform Memory Error")

        for name, size, ftype in self.fields:
            self.cper.decode(name, size, ftype)

    @staticmethod
    def decode_list():
        """
        Returns a tuple with the GUID and class
        """
        return [(DecodePlatformMem.guid, DecodePlatformMem)]


class DecodePlatformMem2():
    """
    Class to decode a Platform Memory Error (Type 2) as defined at
    UEFI 2.5 - N.2.6. Memory Error Section 2
    """

    # GUID for Platform Memory Error Type 2
    guid = "61ec04fc-48e6-d813-25c9-8daa44750b12"

    fields = [
        ("Validation Bits", 8, "int"),
        ("Error Status", 8, "int"),
        ("Physical Address", 8, "int"),
        ("Physical Address Mask", 8, "int"),
        ("Node", 2, "int"),
        ("Card", 2, "int"),
        ("Module", 2, "int"),
        ("Bank", 2, "int"),
        ("Device", 4, "int"),
        ("Row", 4, "int"),
        ("Column", 4, "int"),
        ("Rank", 4, "int"),
        ("Bit Position", 4, "int"),
        ("Chip Identification", 1, "int"),
        ("Memory Error Type", 1, "int"),
        ("Status", 1, "int"),
        ("Reserved", 1, "int"),
        ("Requestor ID", 8, "int"),
        ("Responder ID", 8, "int"),
        ("Target ID", 8, "int"),
        ("Card Handle", 4, "int"),
        ("Module Handle", 4, "int")
    ]

    def __init__(self, cper: DecodeField):
        self.cper = cper

    def decode(self, guid):
        """Decode Platform Memory Error Type 2"""
        print("Platform Memory Error Type 2")

        for name, size, ftype in self.fields:
            self.cper.decode(name, size, ftype)

    @staticmethod
    def decode_list():
        """
        Returns a tuple with the GUID and class
        """
        return [(DecodePlatformMem2.guid, DecodePlatformMem2)]


class DecodePCIe():
    """
    Class to decode a PCI Express Error as defined at
    UEFI 2.1 - N.2.2 Section Descriptor
    """
    # GUID for PCI Express Error
    guid = "d995e954-bbc1-430f-ad91-b44dcb3c6f35"

    fields = [
        ("Validation Bits", 8, "int"),
        ("Port Type", 4, "int"),
        ("Version", 4, "int"),
        ("Command Status", 4, "int"),
        ("RCRB High Address", 4, "int"),
        ("Device ID", 16, "int"),
        ("Device Serial Number", 8, "int"),
        ("Bridge Control Status", 4, "int"),
        ("Capability Structure", 60, "int"),
        ("AER Info", 96, "int")
    ]

    def __init__(self, cper: DecodeField):
        self.cper = cper

    def decode(self, guid):
        """Decode PCI Express Error"""
        print("PCI Express Error")

        for name, size, ftype in self.fields:
            self.cper.decode(name, size, ftype)

    @staticmethod
    def decode_list():
        """
        Returns a tuple with the GUID and class
        """
        return [(DecodePCIe.guid, DecodePCIe)]


class DecodePCIBus():
    """
    Class to decode a PCI Bus Error as defined at
    UEFI 2.1 - N.2.2 Section Descriptor
    """

    # GUID for PCI Bus Error
    guid = "c5753963-3b84-4095-bf78-eddad3f9c9dd"

    fields = [
        ("Validation Bits", 8, "int"),
        ("Error Status", 8, "int"),
        ("Error Type", 2, "int"),
        ("Bus Id", 2, "int"),
        ("Reserved", 4, "int"),
        ("Bus Address", 8, "int"),
        ("Bus Data", 8, "int"),
        ("Bus Command", 8, "int"),
        ("Bus Requestor Id", 8, "int"),
        ("Bus Completer Id", 8, "int"),
        ("Target Id", 8, "int")
    ]

    def __init__(self, cper: DecodeField):
        self.cper = cper

    def decode(self, guid):
        """Decode PCI Bus Error"""
        print("PCI Bus Error")

        for name, size, ftype in self.fields:
            self.cper.decode(name, size, ftype)

    @staticmethod
    def decode_list():
        """
        Returns a tuple with the GUID and class
        """
        return [(DecodePCIBus.guid, DecodePCIBus)]


class DecodePCIDev():
    """
    Class to decode a PCI Device Error as defined at
    UEFI 2.1 - N.2.2 Section Descriptor
    """

    # GUID for PCI Device Error
    guid = "eb5e4685-ca66-4769-b6a2-26068b001326"

    def __init__(self, cper: DecodeField):
        self.cper = cper

    def decode(self, guid):
        """Decode PCI Device Error"""
        print("PCI Device Error")

        self.cper.decode("Validation Bits", 8, "int")
        self.cper.decode("Error Status", 8, "int")
        self.cper.decode("Id Info", 16, "int")

        val = self.cper.decode("Memory Number", 4, "int")
        try:
            mem_num = int.from_bytes(val, byteorder='little')
        except ValueError, TypeError:
            mem_num = 0

        self.cper.decode("IO Number", 4, "int")

        for mem in range(0, mem_num):
            if self.cper.past_end:
                return

            print()
            print(f"Register Data Pair {mem}")
            self.cper.decode("Register 0", 8, "int")
            self.cper.decode("Register 1", 8, "int")

    @staticmethod
    def decode_list():
        """
        Returns a tuple with the GUID and class
        """
        return [(DecodePCIDev.guid, DecodePCIDev)]


class DecodeFWError():
    """
    Class to decode a Firmware Error as defined at
    UEFI 2.1 - N.2.2 Section Descriptor
    """

    # GUID for Firmware Error
    guid = "81212a96-09ed-4996-9471-8d729c8e69ed"

    # NOTE: UEFI 2.11 has a discrepancy, as it lists:
    #       byte offset 1: revision (1 byte)
    #       byte offset 1: reserved (7 bytes)
    #
    # both starting at position 1. We opted to change reserved size to 6,
    # in order to better cope with the spec issues

    fields = [
        ("Firmware Error Record Type", 1, "int"),
        ("Revision", 1, "int"),
        ("Reserved", 6, "int"),
        ("Record Identifier", 8, "int"),
        ("Record identifier GUID extension", 16, "guid")
    ]

    def __init__(self, cper: DecodeField):
        self.cper = cper

    def decode(self, guid):
        """Decode Firmware Error"""
        print("Firmware Error")

        for name, size, ftype in self.fields:
            self.cper.decode(name, size, ftype)

    @staticmethod
    def decode_list():
        """
        Returns a tuple with the GUID and class
        """
        return [(DecodeFWError.guid, DecodeFWError)]


class DecodeDMAGeneric():
    """
    Class to decode a Generic DMA Error as defined at
    UEFI 2.2 - N.2.2 Section Descriptor
    """

    # GUID for Generic DMA Error
    guid = "5b51fef7-c79d-4434-8f1b-aa62de3e2c64"

    fields = [
        ("Requester-ID", 2, "int"),
        ("Segment Number", 2, "int"),
        ("Fault Reason", 1, "int"),
        ("Access Type", 1, "int"),
        ("Address Type", 1, "int"),
        ("Architecture Type", 1, "int"),
        ("Device Address", 8, "int"),
        ("Reserved", 16, "int")
    ]

    def __init__(self, cper: DecodeField):
        self.cper = cper

    def decode(self, guid):
        """Decode Generic DMA Error"""
        print("Generic DMA Error")

        for name, size, ftype in self.fields:
            self.cper.decode(name, size, ftype)

    @staticmethod
    def decode_list():
        """
        Returns a tuple with the GUID and class
        """
        return [(DecodeDMAGeneric.guid, DecodeDMAGeneric)]


class DecodeDMAVT():
    """
    Class to decode a DMA Virtualization Technology Error as defined at
    UEFI 2.2 - N.2.2 Section Descriptor
    """

    # GUID for DMA VT Error
    guid = "71761d37-32b2-45cd-a7d0-b0fedd93e8cf"

    fields = [
        ("Version", 1, "int"),
        ("Revision", 1, "int"),
        ("OemId", 6, "int"),
        ("Capability", 8, "int"),
        ("Extended Capability", 8, "int"),
        ("Global Command", 4, "int"),
        ("Global Status", 4, "int"),
        ("Fault Status", 4, "int"),
        ("Reserved", 12, "int"),
        ("Fault record", 16, "int"),
        ("Root Entry", 16, "int"),
        ("Context Entry", 16, "int"),
        ("Level 6 Page Table Entry", 8, "int"),
        ("Level 5 Page Table Entry", 8, "int"),
        ("Level 4 Page Table Entry", 8, "int"),
        ("Level 3 Page Table Entry", 8, "int"),
        ("Level 2 Page Table Entry", 8, "int"),
        ("Level 1 Page Table Entry", 8, "int")
    ]

    def __init__(self, cper: DecodeField):
        self.cper = cper

    def decode(self, guid):
        """Decode DMA VT Error"""
        print("DMA VT Error")

        for name, size, ftype in self.fields:
            self.cper.decode(name, size, ftype)

    @staticmethod
    def decode_list():
        """
        Returns a tuple with the GUID and class
        """
        return [(DecodeDMAVT.guid, DecodeDMAVT)]


class DecodeDMAIOMMU():
    """
    Class to decode an IOMMU DMA Error as defined at
    UEFI 2.2 - N.2.2 Section Descriptor
    """

    # GUID for IOMMU DMA Error
    guid = "036f84e1-7f37-428c-a79e-575fdfaa84ec"

    fields = [
        ("Revision", 1, "int"),
        ("Reserved", 7, "int"),
        ("Control", 8, "int"),
        ("Status", 8, "int"),
        ("Reserved", 8, "int"),
        ("Event Log Entry", 16, "int"),
        ("Reserved", 16, "int"),
        ("Device Table Entry", 32, "int"),
        ("Level 6 Page Table Entry", 8, "int"),
        ("Level 5 Page Table Entry", 8, "int"),
        ("Level 4 Page Table Entry", 8, "int"),
        ("Level 3 Page Table Entry", 8, "int"),
        ("Level 2 Page Table Entry", 8, "int"),
        ("Level 1 Page Table Entry", 8, "int")
    ]

    def __init__(self, cper: DecodeField):
        self.cper = cper

    def decode(self, guid):
        """Decode IOMMU DMA Error"""
        print("IOMMU DMA Error")

        for name, size, ftype in self.fields:
            self.cper.decode(name, size, ftype)

    @staticmethod
    def decode_list():
        """
        Returns a tuple with the GUID and class
        """
        return [(DecodeDMAIOMMU.guid, DecodeDMAIOMMU)]


class DecodeCCIXPER():
    """
    Class to decode a CCIX Protocol Error as defined at
    UEFI 2.8 - N.2.12. CCIX PER Log Error Section
    """

    # GUID for CCIX Protocol Error
    guid = "91335ef6-ebfb-4478-a6a6-88b728cf75d7"

    fields = [
        ("Validation Bits", 8, "int"),
        ("CCIX Source ID", 1, "int"),
        ("CCIX Port ID", 1, "int"),
        ("Reserved", 2, "int"),
    ]

    def __init__(self, cper: DecodeField):
        self.cper = cper

    def decode(self, guid):
        """Decode CCIX Protocol Error"""
        print("CCIX Protocol Error")

        val = self.cper.decode("Length", 4, "int")
        try:
            length = int.from_bytes(val, byteorder='little')
        except ValueError, TypeError:
            length = 0

        for name, size, ftype in self.fields:
            self.cper.decode(name, size, ftype)

        remaining = max(0, length - self.cper.pos)
        for dword in range(0, int(remaining / 4)):
            if self.cper.past_end:
                return

            self.cper.decode(f"CCIX PER log {dword}", 4, "int")

    @staticmethod
    def decode_list():
        """
        Returns a tuple with the GUID and class
        """
        return [(DecodeCCIXPER.guid, DecodeCCIXPER)]


class DecodeCXLProtErr():
    """
    Class to decode a CXL Protocol Error as defined at
    UEFI 2.9 - N.2.13. Compute Express Link (CXL) Protocol Error Section
    """

    # GUID for CXL Protocol Error
    guid = "80b9efb4-52b5-4de3-a777-68784b771048"

    fields = [
        ("Validation Bits", 8, "int"),
        ("CXL Agent Type", 1, "int"),
        ("Reserved", 7, "int"),
        ("CXL Agent Address", 8, "int"),
        ("Device ID", 16, "int"),
        ("Device Serial Number", 8, "int"),
        ("Capability Structure", 60, "int"),
    ]

    def __init__(self, cper: DecodeField):
        self.cper = cper

    def decode(self, guid):
        """Decode CXL Protocol Error"""
        print("CXL Protocol Error")

        for name, size, ftype in self.fields:
            self.cper.decode(name, size, ftype)

        val = self.cper.decode("CXL DVSEC Length", 2, "int")
        try:
            cxl_devsec_len = int.from_bytes(val, byteorder='little')
        except ValueError, TypeError:
            cxl_devsec_len = 0

        val = self.cper.decode("CXL Error Log Length", 2, "int")
        try:
            cxl_error_log_len = int.from_bytes(val, byteorder='little')
        except ValueError, TypeError:
            cxl_error_log_len = 0

        self.cper.decode("Reserved", 4, "int")
        self.cper.decode("CXL DVSEC", cxl_devsec_len, "int",
                         show_incomplete=True)
        self.cper.decode("CXL Error Log", cxl_error_log_len, "int",
                         show_incomplete=True)

    @staticmethod
    def decode_list():
        """
        Returns a tuple with the GUID and class
        """
        return [(DecodeCXLProtErr.guid, DecodeCXLProtErr)]


class DecodeCXLCompEvent():
    """
    Class to decode a CXL Component Error as defined at
    UEFI 2.9 - N.2.14. CXL Component Events Section

    Currently, the decoder handles only the common fields, displaying
    the CXL Component Event Log field in bytes.
    """

    # GUIDs, as defined at CXL specification 3.2: 8.2.10.2.1 Event Records
    # on Table 8-55. Common Event Record Format
    #
    # Please notice that, in practice, not all those events will be passed
    # to OSPM. Some may be handled internally.
    guids = [
        ("General Media",              "fbcd0a77-c260-417f-85a9-088b1621eba6"),
        ("DRAM",                       "601dcbb3-9c06-4eab-b8af-4e9bfb5c9624"),
        ("Memory Module",              "fe927475-dd59-4339-a586-79bab113bc74"),
        ("Memory Sparing",             "e71f3a40-2d29-4092-8a39-4d1c966c7c65"),
        ("Physical Switch",            "77cf9271-9c02-470b-9fe4-bc7b75f2da97"),
        ("Virtual Switch",             "40d26425-3396-4c4d-a5da-3d472a63af25"),
        ("Multi-Logical Device Port",  "8dc44363-0c96-4710-b7bf-04bb99534c3f"),
        ("Dynamic Capabilities",       "ca95afa7-f183-4018-8c2f-95268e101a2a"),
    ]

    fields = [
        ("Validation Bits", 8, "int"),
        ("Device ID", 12, "int"),
        ("Device Serial Number", 8, "int")
    ]

    def __init__(self, cper: DecodeField):
        self.cper = cper

    def decode(self, guid):
        """Decode CXL Protocol Error"""
        for name, guid_event in DecodeCXLCompEvent.guids:
            if guid == guid_event:
                print(f"CXL {name} Event Record")
                break

        val = self.cper.decode("Length", 4, "int")
        try:
            length = int.from_bytes(val, byteorder='little')
        except ValueError, TypeError:
            length = 0

        for name, size, ftype in self.fields:
            self.cper.decode(name, size, ftype)

        length = max(0, length - self.cper.pos)

        self.cper.decode("CXL Component Event Log", length, "int",
                         show_incomplete=True)

    @staticmethod
    def decode_list():
        """
        Returns a tuple with the GUID and class
        """

        guid_list = []

        for _, guid in DecodeCXLCompEvent.guids:
            guid_list.append((guid, DecodeCXLCompEvent))

        return guid_list


class DecodeFRUMemoryPoison():
    """
    Class to decode a CXL Protocol Error as defined at
    UEFI 2.11 - N.2.15 FRU Memory Poison Section
    """

    # GUID for FRU Memory Poison Section
    guid = "5e4706c1-5356-48c6-930b-52f2120a4458"

    common_fields = [
        ("Checksum", 4, "int"),
        ("Validation Bits", 8, "int"),
        ("FRU Architecture Type", 4, "int"),
        ("FRU Architecture Value", 8, "int"),
        ("FRU Identifier Type", 4, "int"),
        ("FRU Identifier Value", 8, "int")
    ]

    poison_fields = [
        ("Poison Timestamp", 8, "int"),
        ("Hardware Identifier Type", 4, "int"),
        ("Hardware Identifier Value", 8, "int"),
        ("Address Type", 4, "int"),
        ("Address Value", 8, "int")
    ]

    def __init__(self, cper: DecodeField):
        self.cper = cper

    def decode(self, guid):
        """Decode CXL Protocol Error"""
        print("FRU Memory Poison")

        for name, size, ftype in self.common_fields:
            self.cper.decode(name, size, ftype)

        val = self.cper.decode("Poison List Entries", 4, "int")
        try:
            poison_list_entries = int.from_bytes(val, byteorder='little')
        except ValueError, TypeError:
            poison_list_entries = 0

        for entry in range(0, poison_list_entries):
            if self.cper.past_end:
                return

            print()
            print(f"Poison List {entry}")
            for name, size, ftype in self.poison_fields:
                if self.cper.past_end:
                    return

                self.cper.decode(name, size, ftype)

    @staticmethod
    def decode_list():
        """
        Returns a tuple with the GUID and class
        """
        return [(DecodeFRUMemoryPoison.guid, DecodeFRUMemoryPoison)]


class DecodeGhesEntry():
    """
    Class to decode a GHESv2 element, as defined at:
    ACPI 6.1: 18.3.2.8 Generic Hardware Error Source version 2
    """

    # Fields present on all CPER records
    common_fields = [
        # Generic Error Status Block fields
        ("Block Status",           4, "int"),
        ("Raw Data Offset",        4, "int"),
        ("Raw Data Length",        4, "int"),
        ("Data Length",            4, "int"),
        ("Error Severity",         4, "int"),

        # Generic Error Data Entry
        ("Section Type",          16, "guid"),
        ("Error Severity",         4, "int"),
        ("Revision",               2, "int"),
        ("Validation Bits",        1, "int"),
        ("Flags",                  1, "int"),
        ("Error Data Length",      4, "int"),
        ("FRU Id",                16, "guid"),
        ("FRU Text",              20, "str"),
        ("Timestamp",              8, "bcd"),
    ]

    def __init__(self, cper_data: bytearray):
        """
        Initializes a byte array, decoding it, printing results at the
        screen.
        """

        # Create a decode list with the per-type decoders
        decode_list = []
        decode_list += DecodeProcGeneric.decode_list()
        decode_list += DecodeProcX86.decode_list()
        decode_list += DecodeProcItanium.decode_list()
        decode_list += DecodeProcArm.decode_list()
        decode_list += DecodePlatformMem.decode_list()
        decode_list += DecodePlatformMem2.decode_list()
        decode_list += DecodePCIe.decode_list()
        decode_list += DecodePCIBus.decode_list()
        decode_list += DecodePCIDev.decode_list()
        decode_list += DecodeFWError.decode_list()
        decode_list += DecodeDMAGeneric.decode_list()
        decode_list += DecodeDMAVT.decode_list()
        decode_list += DecodeDMAIOMMU.decode_list()
        decode_list += DecodeCCIXPER.decode_list()
        decode_list += DecodeCXLProtErr.decode_list()
        decode_list += DecodeCXLCompEvent.decode_list()
        decode_list += DecodeFRUMemoryPoison.decode_list()

        # Handle common types
        cper = DecodeField(cper_data)

        fields = {}
        for name, size, ftype in self.common_fields:
            val = cper.decode(name, size, ftype)

            if ftype == "int":
                try:
                    val = int.from_bytes(val, byteorder='little')
                except ValueError, TypeError:
                    val = 0

            fields[name] = val

        if fields.get("Raw Data Length"):
            cper.decode("Raw Data", fields.get("Raw Data Length", 0),
                        "int", pos=fields.get("Raw Data Offset", 0))

        if not fields.get("Section Type"):
            return

        print()

        # Now, decode the rest of the record for known decoders
        for guid, cls in decode_list:
            if fields.get("Section Type", "") == guid:
                dec = cls(cper)
                dec.decode(guid)

                if not cper.is_end:
                    print()
                    print("Warning: incomplete decode or broken CPER")
                    if cper.remaining:
                        cper.decode("Extra Data", cper.remaining, "int")

                print()
                return

        # If we don't have a class to decode the full payload,
        # output the undecoded part
        print(f"Unknown GGID: {fields.get('Section Type', "")}")
        remaining = cper.remaining
        if remaining:
            cper.decode("Payload", remaining, "int")

        print()
