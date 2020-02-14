/*
 * Protected Virtualization header
 *
 * Copyright IBM Corp. 2019
 * Author(s):
 *  Janosch Frank <frankja@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef HW_S390_PV_H
#define HW_S390_PV_H

#ifdef CONFIG_KVM
int s390_pv_vm_create(void);
void s390_pv_vm_destroy(void);
void s390_pv_vcpu_destroy(CPUState *cs);
int s390_pv_vcpu_create(CPUState *cs);
int s390_pv_set_sec_parms(uint64_t origin, uint64_t length);
void s390_pv_set_ipl_psw(CPUState *cs);
int s390_pv_unpack(uint64_t addr, uint64_t size, uint64_t tweak);
void s390_pv_perf_clear_reset(void);
int s390_pv_verify(void);
void s390_pv_unshare(void);
#else
int s390_pv_vm_create(void) { return 0; }
void s390_pv_vm_destroy(void) {}
void s390_pv_vcpu_destroy(CPUState *cs) {}
int s390_pv_vcpu_create(CPUState *cs) { return 0; }
int s390_pv_set_sec_parms(uint64_t origin, uint64_t length) { return 0; }
void s390_pv_set_ipl_psw(CPUState *cs) {}
int s390_pv_unpack(uint64_t addr, uint64_t size, uint64_t tweak) { return 0: }
void s390_pv_perf_clear_reset(void) {}
int s390_pv_verify(void) { return 0; }
void s390_pv_unshare(void) {}
#endif

#endif /* HW_S390_PV_H */
