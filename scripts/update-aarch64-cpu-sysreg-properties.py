#!/usr/bin/env python3

# This script takes as input the Registers.json file delivered in
# the AARCHMRS Features Model downloadable from the Arm Developer
# A-Profile Architecture Exploration Tools page:
# https://developer.arm.com/Architectures/A-Profile%20Architecture#Downloads
# and outputs target/arm/cpu-sysreg-properties.c content.
# There, initialize_cpu_sysreg_properties() populates arm64_id_regs array
# with the name of each ID register and definition of all its fields
# including their name and min/max bit under the form of the below pattern:
#
# /* CCSIDR2_EL1 */
# ARM64SysReg *CCSIDR2_EL1 = arm64_sysreg_get(CCSIDR2_EL1_IDX);
# CCSIDR2_EL1->name = "CCSIDR2_EL1";
# arm64_sysreg_add_field(CCSIDR2_EL1, "NumSets", 0, 23);
#
# Copyright (C) 2026 Red Hat, Inc.
#
# Authors: Eric Auger <eric.auger@redhat.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later


import json
import os
import sys
from aarch64_sysreg_helpers import extract_idregs_from_registers_json

def collect_fields(item, bit_offset=0):
    """
    Recursively finds all field-like objects, handling Fields.Array,
    Fields.ArrayField, and ConditionalField structures.
    Applies bit_offset from containers to child fields.
    """
    fields = []
    if not isinstance(item, dict):
        return fields

    _type = item.get('_type', '')

    # Array types (for example CLIDR_EL1 Ctype<n>, Ttype<n>)
    if _type == 'Fields.Array':
        name_template = item.get('name') or item.get('label', '')
        index_info = item.get('indexes', [{}])[0]
        start_idx = index_info.get('start', 0)
        count = index_info.get('width', 0)

        full_range = item.get('rangeset', [{}])[0]
        bit_start = full_range.get('start', 0) + bit_offset
        elem_width = full_range.get('width', 0) // count if count else 0

        for i in range(count):
            idx = start_idx + i
            # Correctly handle indexed names like Ctype1, Ctype2
            field_name = name_template.replace('<n>', str(idx))
            fields.append({
                'name': field_name,
                'rangeset': [{
                    'start': bit_start + (i * elem_width),
                    'width': elem_width
                }],
                '_type': 'Fields.Field'
            })
        return fields

    # ConditionalFields
    elif _type == 'Fields.ConditionalField':
        inner_offset = bit_offset
        if item.get('rangeset'):
            # Parent container defines the absolute start bit
            inner_offset = item['rangeset'][0].get('start', bit_offset)

        for entry in item.get('fields', []):
            inner = entry.get('field')
            if inner:
                fields.extend(collect_fields(inner, inner_offset))
        return fields

    # Normal Field Types
    leaf_types = ['Fields.Field', 'Fields.ConstantField',
                  'Fields.EnumeratedField', 'Fields.Bitfield']
    if _type in leaf_types:
        field_copy = item.copy()
        if field_copy.get('rangeset'):
            new_ranges = []
            for r in field_copy['rangeset']:
                nr = r.copy()
                # Apply the cumulative offset to the field's start bit
                nr['start'] = r.get('start', 0) + bit_offset
                new_ranges.append(nr)
            field_copy['rangeset'] = new_ranges
        fields.append(field_copy)
        return fields

    # Traverse the hierarchy for other cases
    for key in ['fields', 'values', 'fieldsets']:
        for nested in item.get(key, []):
            fields.extend(collect_fields(nested, bit_offset))

    return fields


def generate_sysreg_properties_from_registers_json(id_reg_names, raw_json_path):
    with open(raw_json_path, 'r') as f:
        register_data = json.load(f)

    regs = {r.get('name'): r for r in register_data if r.get('_type') == 'Register'}

    final_output = ""

    for reg_name in id_reg_names:
        register = regs.get(reg_name)
        if not register:
            continue

        final_output += f"    /* {reg_name} */\n"
        final_output += (f"    ARM64SysReg *{reg_name} = "
                         f"arm64_sysreg_get({reg_name}_IDX);\n")
        final_output += f"    {reg_name}->name = \"{reg_name}\";\n"

        unique_fields = {}
        for fieldset in register.get('fieldsets', []):
            candidates = collect_fields(fieldset)
            for val in candidates:
                name = (val.get('name') or val.get('label', '')).strip()
                if not name or "RESERVED" in name.upper():
                    continue
                for r in val.get('rangeset', []):
                    lsb = int(r.get('start'))
                    msb = lsb + int(r.get('width')) - 1

                    # Only keep the fields with the highest MSB
                    # needed fir CCSIDR_EL1
                    if name not in unique_fields or msb > unique_fields[name]['msb']:
                        unique_fields[name] = {'lsb': lsb, 'msb': msb}

        # Sort decreasing lsbs
        sorted_fields = sorted(unique_fields.items(),
                               key=lambda x: x[1]['lsb'], reverse=True)

        for name, bits in sorted_fields:
            line = (f"    arm64_sysreg_add_field({reg_name}, "
                    f"\"{name}\", {bits['lsb']}, {bits['msb']});\n")
            final_output += line
        final_output += "\n"

    os.makedirs("target/arm", exist_ok=True)
    with open("target/arm/cpu-sysreg-properties.c", 'w') as f:
        f.write("/* AUTOMATICALLY GENERATED, DO NOT MODIFY */\n\n")
        f.write("/* SPDX-License-Identifier: GPL-2.0-or-later */\n\n\n")
        f.write("#include \"cpu-idregs.h\"\n\n")
        f.write("ARM64SysReg arm64_id_regs[NUM_ID_IDX];\n\n")
        f.write("void initialize_cpu_sysreg_properties(void)\n{\n")
        f.write(final_output)
        f.write("}\n")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python scripts/update-aarch64-cpu-sysreg-properties.py "
              "<path_to_registers_json>")
    else:
        json_path = sys.argv[1]

        id_regs_dict = extract_idregs_from_registers_json(json_path)
        sorted_names = sorted(id_regs_dict.keys())

        if sorted_names:
            generate_sysreg_properties_from_registers_json(sorted_names, json_path)
            print("Generated target/arm/cpu-sysreg-properties.c")
