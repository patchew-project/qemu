#ifndef ARM_MCDSTUB_H
#define ARM_MCDSTUB_H

const char *arm_mcd_get_dynamic_xml(CPUState *cs, const char *xmlname);
int arm_mcd_read_register(CPUState *cs, GByteArray *mem_buf, int n);

#endif /* ARM_MCDSTUB_H */
