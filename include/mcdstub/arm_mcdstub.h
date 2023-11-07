#ifndef ARM_MCDSTUB_H
#define ARM_MCDSTUB_H

#include "hw/core/cpu.h"
#include "mcdstub_common.h"
/* just used for the register xml files */
#include "exec/gdbstub.h"

/* ids for each different type of register */
enum {
    MCD_ARM_REG_TYPE_GPR,
    MCD_ARM_REG_TYPE_VFP,
    MCD_ARM_REG_TYPE_VFP_SYS,
    MCD_ARM_REG_TYPE_MVE,
    MCD_ARM_REG_TYPE_CPR,
};

/**
 * arm_mcd_get_dynamic_xml() - Returns the contents of the desired XML file.
 *
 * @xmlname: Name of the desired XML file.
 * @cs: CPU state.
 */
const char *arm_mcd_get_dynamic_xml(CPUState *cs, const char *xmlname);

/**
 * arm_mcd_get_opcode() - Returns the opcode for a coprocessor register.
 *
 * This function uses the opc1, opc2, crm and crn members of the register to
 * create the opcode. The formular for creating the opcode is determined by ARM.
 * @n: The register ID of the CP register.
 * @cs: CPU state.
 */
uint16_t arm_mcd_get_opcode(CPUState *cs, uint32_t n);

/**
 * arm_mcd_store_mem_spaces() - Stores all 32-Bit ARM specific memory spaces.
 *
 * This function stores the memory spaces into the memspaces GArray.
 * It only stores secure memory spaces if the CPU has more than one address
 * space. It also stores a GPR and a CP15 register memory space.
 * @memspaces: GArray of memory spaces.
 * @cpu: CPU state.
 */
int arm_mcd_store_mem_spaces(CPUState *cpu, GArray *memspaces);

/**
 * arm_mcd_parse_core_xml_file() - Parses the GPR registers.
 *
 * This function parses the core XML file, which includes the GPR registers.
 * The regsters get stored in a GArray and a GPR register group is stored in a
 * second GArray.
 * @reggroups: GArray of register groups.
 * @registers: GArray of registers.
 * @cc: The CPU class.
 * @current_group_id: The current group ID. It increases after
 * each group.
 */
int arm_mcd_parse_core_xml_file(CPUClass *cc, GArray *reggroups,
    GArray *registers, int *current_group_id);

/**
 * arm_mcd_parse_general_xml_files() - Parses all but the GPR registers.
 *
 * This function parses all XML files except for the core XML file.
 * The regsters get stored in a GArray and if the system-registers.xml file is
 * parsed, it also adds a CP15 register group.
 * @reggroups: GArray of register groups.
 * @registers: GArray of registers.
 * @cpu: The CPU state.
 * @current_group_id: The current group ID. It increases after
 * each added group.
 */
int arm_mcd_parse_general_xml_files(CPUState *cpu, GArray* reggroups,
    GArray *registers, int *current_group_id);

/**
 * arm_mcd_get_additional_register_info() - Adds additional data to parsed
 * registers.
 *
 * This function is called, after :c:func:`arm_mcd_parse_core_xml_file` and
 * :c:func:`arm_mcd_parse_general_xml_files`. It adds additional data for all
 * already parsed registers. The registers get a correct ID, group, memory
 * space and opcode, if they are CP15 registers.
 * @reggroups: GArray of register groups.
 * @registers: GArray of registers.
 * @cpu: The CPU state.
 */
int arm_mcd_get_additional_register_info(GArray *reggroups, GArray *registers,
    CPUState *cpu);

/**
 * arm_mcd_get_address_space() - Returnes the correct QEMU address space name
 * @cpu_id: Correct CPU ID
 * @mem_space: Desired mcd specific memory space.
 */
AddressSpace *arm_mcd_get_address_space(uint32_t cpu_id,
    mcd_mem_space_st mem_space);

/**
 * arm_mcd_get_memtxattrs() - Returnes the correct QEMU address space access
 * attributes
 * @mem_space: Desired mcd specific memory space.
 */
MemTxAttrs arm_mcd_get_memtxattrs(mcd_mem_space_st mem_space);

#endif /* ARM_MCDSTUB_H */
