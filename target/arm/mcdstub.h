#ifndef ARM_MCDSTUB_H
#define ARM_MCDSTUB_H

#include "hw/core/cpu.h"
#include "mcdstub/mcdstub_common.h"
/* just used for the register xml files */
#include "exec/gdbstub.h"

typedef struct GDBRegisterState {
    /* needed for the used gdb functions */
    int base_reg;
    int num_regs;
    gdb_get_reg_cb get_reg;
    gdb_set_reg_cb set_reg;
    const char *xml;
    struct GDBRegisterState *next;
} GDBRegisterState;

/* ids for each different type of register */
enum {
    MCD_ARM_REG_TYPE_GPR,
    MCD_ARM_REG_TYPE_VFP,
    MCD_ARM_REG_TYPE_VFP_SYS,
    MCD_ARM_REG_TYPE_MVE,
    MCD_ARM_REG_TYPE_CPR,
};

const char *arm_mcd_get_dynamic_xml(CPUState *cs, const char *xmlname);
int arm_mcd_read_register(CPUState *cs, GByteArray *mem_buf, uint8_t reg_type,
    uint32_t n);
int arm_mcd_write_register(CPUState *cs, GByteArray *mem_buf, uint8_t reg_type,
    uint32_t n);
uint16_t arm_mcd_get_opcode(CPUState *cs, uint32_t n);
int arm_mcd_set_scr(CPUState *cs, bool secure);
int arm_mcd_store_mem_spaces(CPUState *cpu, GArray *memspaces);
int arm_mcd_parse_core_xml_file(CPUClass *cc, GArray *reggroups,
    GArray *registers, int *current_group_id);
int arm_mcd_parse_general_xml_files(CPUState *cpu, GArray* reggroups,
    GArray *registers, int *current_group_id);
int arm_mcd_get_additional_register_info(GArray *reggroups, GArray *registers,
    CPUState *cpu);

#endif /* ARM_MCDSTUB_H */
