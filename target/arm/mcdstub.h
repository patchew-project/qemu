#ifndef ARM_MCDSTUB_H
#define ARM_MCDSTUB_H

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

#endif /* ARM_MCDSTUB_H */
