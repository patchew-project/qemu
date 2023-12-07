/*
 * Copyright (c) 2023 Nicolas Eder
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.0+
 */

#include "qemu/osdep.h"
#include "exec/memory.h"
#include "mcdstub/arm_mcdstub.h"

int arm_mcd_store_mem_spaces(CPUState *cpu, GArray *memspaces)
{
    int nr_address_spaces = cpu->num_ases;
    uint32_t mem_space_id = 0;

    mem_space_id++;
    mcd_mem_space_st non_secure = {
        .name = "Non Secure",
        .id = mem_space_id,
        .type = 34,
        .bits_per_mau = 8,
        .invariance = 1,
        .endian = 1,
        .min_addr = 0,
        .max_addr = -1,
        .supported_access_options = 0,
        .is_secure = false,
        .is_physical = false,
    };
    g_array_append_vals(memspaces, (gconstpointer)&non_secure, 1);
    mem_space_id++;
    mcd_mem_space_st phys_non_secure = {
        .name = "Physical (Non Secure)",
        .id = mem_space_id,
        .type = 18,
        .bits_per_mau = 8,
        .invariance = 1,
        .endian = 1,
        .min_addr = 0,
        .max_addr = -1,
        .supported_access_options = 0,
        .is_secure = false,
        .is_physical = true,
    };
    g_array_append_vals(memspaces, (gconstpointer)&phys_non_secure, 1);
    if (nr_address_spaces > 1) {
        mem_space_id++;
        mcd_mem_space_st secure = {
            .name = "Secure",
            .id = mem_space_id,
            .type = 34,
            .bits_per_mau = 8,
            .invariance = 1,
            .endian = 1,
            .min_addr = 0,
            .max_addr = -1,
            .supported_access_options = 0,
            .is_secure = true,
            .is_physical = false,
        };
        g_array_append_vals(memspaces, (gconstpointer)&secure, 1);
        mem_space_id++;
        mcd_mem_space_st phys_secure = {
            .name = "Physical (Secure)",
            .id = mem_space_id,
            .type = 18,
            .bits_per_mau = 8,
            .invariance = 1,
            .endian = 1,
            .min_addr = 0,
            .max_addr = -1,
            .supported_access_options = 0,
            .is_secure = true,
            .is_physical = true,
        };
        g_array_append_vals(memspaces, (gconstpointer)&phys_secure, 1);
    }
    mem_space_id++;
    mcd_mem_space_st gpr = {
        .name = "GPR Registers",
        .id = mem_space_id,
        .type = 1,
        .bits_per_mau = 8,
        .invariance = 1,
        .endian = 1,
        .min_addr = 0,
        .max_addr = -1,
        .supported_access_options = 0,
    };
    g_array_append_vals(memspaces, (gconstpointer)&gpr, 1);
    mem_space_id++;
    mcd_mem_space_st cpr = {
        .name = "CP15 Registers",
        .id = mem_space_id,
        .type = 1,
        .bits_per_mau = 8,
        .invariance = 1,
        .endian = 1,
        .min_addr = 0,
        .max_addr = -1,
        .supported_access_options = 0,
    };
    g_array_append_vals(memspaces, (gconstpointer)&cpr, 1);
    return 0;
}

int arm_mcd_parse_core_xml_file(CPUClass *cc, GArray *reggroups,
    GArray *registers, int *current_group_id)
{
    const char *xml_filename = NULL;
    const char *current_xml_filename = NULL;
    const char *xml_content = NULL;
    int i = 0;

    /* 1. get correct file */
    xml_filename = cc->gdb_core_xml_file;
    for (i = 0; ; i++) {
        current_xml_filename = gdb_static_features[i].xmlname;
        if (!current_xml_filename || (strncmp(current_xml_filename,
            xml_filename, strlen(xml_filename)) == 0
            && strlen(current_xml_filename) == strlen(xml_filename)))
            break;
    }
    /* without gpr registers we can do nothing */
    if (!current_xml_filename) {
        return -1;
    }

    /* 2. add group for gpr registers */
    mcd_reg_group_st gprregs = {
        .name = "GPR Registers",
        .id = *current_group_id
    };
    g_array_append_vals(reggroups, (gconstpointer)&gprregs, 1);
    *current_group_id = *current_group_id + 1;

    /* 3. parse xml */
    /* the offset for gpr is always zero */
    xml_content = gdb_static_features[i].xml;
    parse_reg_xml(xml_content, strlen(xml_content), registers,
        MCD_ARM_REG_TYPE_GPR, 0);
    return 0;
}

int arm_mcd_parse_general_xml_files(CPUState *cpu, GArray *reggroups,
    GArray *registers, int *current_group_id) {
    const char *xml_filename = NULL;
    const char *current_xml_filename = NULL;
    const char *xml_content = NULL;
    uint8_t reg_type;
    CPUClass *cc = CPU_GET_CLASS(cpu);

    /* iterate over all gdb xml files*/
    GDBRegisterState *r;
    for (guint i = 0; i < cpu->gdb_regs->len; i++) {
        r = &g_array_index(cpu->gdb_regs, GDBRegisterState, i);

        xml_filename = r->xml;
        xml_content = NULL;

        /* 1. get xml content */
        if (cc->gdb_get_dynamic_xml) {
            xml_content = cc->gdb_get_dynamic_xml(cpu, xml_filename);
        }
        if (xml_content) {
            if (strcmp(xml_filename, "system-registers.xml") == 0) {
                /* these are the coprocessor register */
                mcd_reg_group_st corprocessorregs = {
                    .name = "CP15 Registers",
                    .id = *current_group_id
                };
                g_array_append_vals(reggroups,
                    (gconstpointer)&corprocessorregs, 1);
                *current_group_id = *current_group_id + 1;
                reg_type = MCD_ARM_REG_TYPE_CPR;
            }
        } else {
            /* its not a coprocessor xml -> it is a static xml file */
            int j = 0;
            for (j = 0; ; j++) {
                current_xml_filename = gdb_static_features[j].xmlname;
                if (!current_xml_filename || (strncmp(current_xml_filename,
                    xml_filename, strlen(xml_filename)) == 0
                    && strlen(current_xml_filename) == strlen(xml_filename)))
                    break;
            }
            if (current_xml_filename) {
                xml_content = gdb_static_features[j].xml;
                /* select correct reg_type */
                if (strcmp(current_xml_filename, "arm-vfp.xml") == 0) {
                    reg_type = MCD_ARM_REG_TYPE_VFP;
                } else if (strcmp(current_xml_filename, "arm-vfp3.xml") == 0) {
                    reg_type = MCD_ARM_REG_TYPE_VFP;
                } else if (strcmp(current_xml_filename,
                    "arm-vfp-sysregs.xml") == 0) {
                    reg_type = MCD_ARM_REG_TYPE_VFP_SYS;
                } else if (strcmp(current_xml_filename,
                    "arm-neon.xml") == 0) {
                    reg_type = MCD_ARM_REG_TYPE_VFP;
                } else if (strcmp(current_xml_filename,
                    "arm-m-profile-mve.xml") == 0) {
                    reg_type = MCD_ARM_REG_TYPE_MVE;
                }
            } else {
                continue;
            }
        }
        /* 2. parse xml */
        parse_reg_xml(xml_content, strlen(xml_content), registers, reg_type,
            r->base_reg);
    }
    return 0;
}

int arm_mcd_get_additional_register_info(GArray *reggroups, GArray *registers,
    CPUState *cpu)
{
    mcd_reg_st *current_register;
    uint32_t i = 0;

    /* iterate over all registers */
    for (i = 0; i < registers->len; i++) {
        current_register = &(g_array_index(registers, mcd_reg_st, i));
        /* add mcd_reg_group_id and mcd_mem_space_id */
        if (strcmp(current_register->group, "cp_regs") == 0) {
            /* coprocessor registers */
            current_register->mcd_reg_group_id = 2;
            current_register->mcd_mem_space_id = 6;
            /*
             * get info for opcode
             * for 32bit the opcode is only 16 bit long
             * for 64bit it is 32 bit long
             */
            current_register->opcode |=
                arm_mcd_get_opcode(cpu, current_register->internal_id);
        } else {
            /* gpr register */
            current_register->mcd_reg_group_id = 1;
            current_register->mcd_mem_space_id = 5;
        }
    }
    return 0;
}

uint16_t arm_mcd_get_opcode(CPUState *cs, uint32_t n)
{
    /* TODO: not working with current build structure */
    return 0;
}

AddressSpace *arm_mcd_get_address_space(uint32_t cpu_id,
    mcd_mem_space_st mem_space)
{
    /* get correct address space name */
    char as_name[ARGUMENT_STRING_LENGTH] = {0};
    if (mem_space.is_secure) {
        sprintf(as_name, "cpu-secure-memory-%u", cpu_id);
    } else {
        sprintf(as_name, "cpu-memory-%u", cpu_id);
    }
    /* return correct address space */
    AddressSpace *as = mcd_find_address_space(as_name);
    return as;
}

MemTxAttrs arm_mcd_get_memtxattrs(mcd_mem_space_st mem_space)
{
    MemTxAttrs attributes = {0};
    if (mem_space.is_secure) {
        attributes.secure = 1;
        attributes.space = 2;
    } else {
        attributes.space = 1;
    }
    return attributes;
}
