#ifndef HW_MIPS_CPUDEVS_H
#define HW_MIPS_CPUDEVS_H

#include "target/mips/cpu-qom.h"

/* Definitions for MIPS CPU internal devices.  */

/* addr.c */
uint64_t cpu_mips_kseg0_to_phys(void *opaque, uint64_t addr);
uint64_t cpu_mips_phys_to_kseg0(void *opaque, uint64_t addr);
uint64_t cpu_mips_kseg1_to_phys(void *opaque, uint64_t addr);
uint64_t cpu_mips_phys_to_kseg1(void *opaque, uint64_t addr);
uint64_t cpu_mips_kvm_um_phys_to_kseg0(void *opaque, uint64_t addr);
bool mips_um_ksegs_enabled(void);
void mips_um_ksegs_enable(void);

/* bootloader.c */
void bl_gen_jump_to(uint32_t **p, uint32_t jump_addr);
void bl_gen_jump_kernel(uint32_t **p, uint32_t sp, uint32_t a0,
                        uint32_t a1, uint32_t a2, uint32_t a3,
                        uint32_t kernel_addr);
void bl_gen_writel(uint32_t **p, uint32_t val, uint32_t addr);
void bl_gen_writeq(uint32_t **p, uint64_t val, uint32_t addr);

/* mips_int.c */
void cpu_mips_irq_init_cpu(MIPSCPU *cpu);

/* mips_timer.c */
void cpu_mips_clock_init(MIPSCPU *cpu);

#endif
