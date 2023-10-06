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

/**
 * \defgroup armmcdstub ARM mcdstub functions
 * All ARM specific functions of the mcdstub.
 */

/**
 * \addtogroup armmcdstub
 * @{
 */

/**
 * \brief Returns the contents of the desired XML file.
 * 
 * @param[in] xmlname Name of the desired XML file.
 * @param[in] cs CPU state.
 */
const char *arm_mcd_get_dynamic_xml(CPUState *cs, const char *xmlname);
/**
 * \brief Calls the correct read function which writes data into the mem_buf.
 * 
 * Depending on the reg_type of the register one of the following functions
 * will get called: arm_mcd_read_gpr_register, arm_mcd_read_vfp_register,
 * arm_mcd_read_vfp_sys_register, arm_mcd_read_mve_register and
 * arm_mcd_read_cpr_register. In those the data of the requested register will
 * be stored as byte array into mem_buf. The function returns zero if no bytes
 * were written
 * @param[out] mem_buf Byte array for register data.
 * @param[in] reg_type Type of register.
 * @param[in] n The register ID within its type.
 * @param[in] cs CPU state.
 */
int arm_mcd_read_register(CPUState *cs, GByteArray *mem_buf, uint8_t reg_type,
    uint32_t n);
/**
 * \brief Calls the correct write function which writes data from the mem_buf.
 * 
 * Depending on the reg_type of the register one of the following functions
 * will get called: arm_mcd_write_gpr_register, arm_mcd_write_vfp_register,
 * arm_mcd_write_vfp_sys_register, arm_mcd_write_mve_register and
 * arm_mcd_write_cpr_register. In those the register data from mem_buf will
 * be written. The function returns zero if no bytes were written.
 * @param[in] mem_buf Byte array for register data.
 * @param[in] reg_type Type of register.
 * @param[in] n The register ID within its type.
 * @param[in] cs CPU state.
 */
int arm_mcd_write_register(CPUState *cs, GByteArray *mem_buf, uint8_t reg_type,
    uint32_t n);
/**
 * \brief Returns the opcode for a coprocessor register.
 * 
 * This function uses the opc1, opc2, crm and crn members of the register to
 * create the opcode. The formular for creating the opcode is determined by ARM.
 * @param[in] n The register ID of the CP register.
 * @param[in] cs CPU state.
 */
uint16_t arm_mcd_get_opcode(CPUState *cs, uint32_t n);
/**
 * \brief Sets the scr_el3 register according to the secure parameter.
 * 
 * If secure is true, the first bit of the scr_el3 register gets set to 0,
 * if not it gets set to 1.
 * @param[in] secure True when secure is requested.
 * @param[in] cs CPU state.
 */
int arm_mcd_set_scr(CPUState *cs, bool secure);
/**
 * \brief Stores all 32-Bit ARM specific memory spaces.
 * 
 * This function stores the memory spaces into the memspaces GArray.
 * It only stores secure memory spaces if the CPU has more than one address
 * space. It also stores a GPR and a CP15 register memory space.
 * @param[out] memspaces GArray of memory spaces.
 * @param[in] cpu CPU state.
 */
int arm_mcd_store_mem_spaces(CPUState *cpu, GArray *memspaces);
/**
 * \brief Parses the GPR registers.
 * 
 * This function parses the core XML file, which includes the GPR registers.
 * The regsters get stored in a GArray and a GPR register group is stored in a
 * second GArray.
 * @param[out] reggroups GArray of register groups.
 * @param[out] registers GArray of registers.
 * @param[in] cc The CPU class.
 * @param[in,out] current_group_id The current group ID. It increases after
 * each group.
 */
int arm_mcd_parse_core_xml_file(CPUClass *cc, GArray *reggroups,
    GArray *registers, int *current_group_id);
/**
 * \brief Parses all but the GPR registers.
 * 
 * This function parses all XML files except for the core XML file.
 * The regsters get stored in a GArray and if the system-registers.xml file is
 * parsed, it also adds a CP15 register group.
 * @param[out] reggroups GArray of register groups.
 * @param[out] registers GArray of registers.
 * @param[in] cpu The CPU state.
 * @param[in,out] current_group_id The current group ID. It increases after
 * each added group.
 */
int arm_mcd_parse_general_xml_files(CPUState *cpu, GArray* reggroups,
    GArray *registers, int *current_group_id);
/**
 * \brief Adds additional data to parsed registers.
 * 
 * This function is called, after \ref arm_mcd_parse_core_xml_file and
 * \ref arm_mcd_parse_core_xml_file. It adds additional data for all already
 * parsed registers. The registers get a correct ID, group, memory space and
 * opcode, if they are CP15 registers.
 * @param[in] reggroups GArray of register groups.
 * @param[in,out] registers GArray of registers.
 * @param[in] cpu The CPU state.
 */
int arm_mcd_get_additional_register_info(GArray *reggroups, GArray *registers,
    CPUState *cpu);

/** @} */

#endif /* ARM_MCDSTUB_H */
