#!/usr/bin/env python3

# Helpers used in aarch64 sysreg definition generation
#
# Copyright (C) 2026 Red Hat, Inc.
#
# Authors: Eric Auger <eric.auger@redhat.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later


import json
import os

# Some regs have op code values like 000x, 001x. Anyway we don't need
# them. Besides some regs are undesired in the generated file such as
# VMPIDR_EL2 and VPIDR_EL2 which are outside of the IDreg scope we
# are interested in and are tricky to decode as their system accessor
# refer to MPIDR_EL1/MIDR_EL1 respectively

skiplist = ['ALLINT', 'PM', 'S1_', 'S3_', 'SVCR', \
            'VMPIDR_EL2', 'VPIDR_EL2']

# returns the int value of a given @opcode for a reg @encoding
def get_opcode(encoding, opcode):
    fvalue = encoding.get(opcode)
    if fvalue:
        value = fvalue.get('value')
        if isinstance(value, str):
            value = value.strip("'")
            value = int(value, 2)
            return value
    return -1

def extract_idregs_from_registers_json(filename):
    """
    Load a Registers.json file and extract all ID registers, decode their
    opcode and dump the information in target/arm/cpu-sysregs.h.inc

    Args:
        filename (str): The path to the Registers.json
    returns:
        idregs: list of ID regs and their encoding
    """
    if not os.path.exists(filename):
        print(f"Error: {filename} could not be found!")
        return {}

    try:
        with open(filename, 'r') as f:
            register_data = json.load(f)

    except json.JSONDecodeError:
        print(f"Could not decode json from '{filename}'!")
        return {}
    except Exception as e:
        print(f"Unexpected error while reading {filename}: {e}")
        return {}

    registers = [r for r in register_data if isinstance(r, dict) and \
                r.get('_type') == 'Register']

    idregs = {}

    # Some regs have op code values like 000x, 001x. Anyway we don't need
    # them. Besides some regs are undesired in the generated file such as
    # VMPIDR_EL2 and VPIDR_EL2 which are outside of the IDreg scope we
    # are interested in and are tricky to decode as their system accessor
    # refer to MPIDR_EL1/MIDR_EL1 respectively

    skiplist = ['ALLINT', 'PM', 'S1_', 'S3_', 'SVCR', \
                'VMPIDR_EL2', 'VPIDR_EL2']

    for register in registers:
        reg_name = register.get('name')

        is_skipped = any(term in (reg_name or "").upper() for term in skiplist)

        if reg_name and not is_skipped:
            accessors = register.get('accessors', [])

            for accessor in accessors:
                type = accessor.get('_type')
                if type in ['Accessors.SystemAccessor']:
                    encoding_list = accessor.get('encoding')

                    if isinstance(encoding_list, list) and encoding_list and \
                       isinstance(encoding_list[0], dict):
                        encoding_wrapper = encoding_list[0]
                        encoding_source = encoding_wrapper.get('encodings', \
                                                               encoding_wrapper)

                        if isinstance(encoding_source, dict):
                                op0 = get_opcode(encoding_source, 'op0')
                                op1 = get_opcode(encoding_source, 'op1')
                                op2 = get_opcode(encoding_source, 'op2')
                                crn = get_opcode(encoding_source, 'CRn')
                                crm = get_opcode(encoding_source, 'CRm')
                                encoding_str=f"{op0} {op1} {crn} {crm} {op2}"

                # ID regs are assumed within this scope
                if op0 == 3 and (op1 == 0 or op1 == 1 or op1 == 3) and \
                   crn == 0 and (crm >= 0 and crm <= 7) and (op2 >= 0 and op2 <= 7):
                    idregs[reg_name] = encoding_str

    return idregs



