/*
 * Arm specific proc functions for linux-user
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef ARM_TARGET_PROC_H
#define ARM_TARGET_PROC_H

#include "target/arm/cpu-features.h" /* for MIDR_EL1 field definitions */

static int open_cpuinfo(CPUArchState *cpu_env, int fd)
{
    ARMCPU *cpu = env_archcpu(cpu_env);
    int arch, midr_rev, midr_part, midr_var, midr_impl;
    target_ulong elf_hwcap = get_elf_hwcap(env_cpu(cpu_env));
    target_ulong elf_hwcap2 = get_elf_hwcap2(env_cpu(cpu_env));
    uint64_t midr = GET_IDREG(&cpu->isar, MIDR);
    const char *elf_name;
    int num_cpus, len_part, len_var;

    arch = 8;
    elf_name = target_big_endian() ? "v8b" : "v8l";
    midr_rev = FIELD_EX32(midr, MIDR_EL1, REVISION);
    midr_part = FIELD_EX32(midr, MIDR_EL1, PARTNUM);
    midr_var = FIELD_EX32(midr, MIDR_EL1, VARIANT);
    midr_impl = FIELD_EX32(midr, MIDR_EL1, IMPLEMENTER);
    len_part = 3;
    len_var = 1;

#ifndef TARGET_AARCH64
    /* For simplicity, treat ARMv8 as an arm64 kernel with CONFIG_COMPAT. */
    if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
        if (arm_feature(&cpu->env, ARM_FEATURE_V7)) {
            arch = 7;
            midr_var = (midr >> 16) & 0x7f;
            len_var = 2;
            if (arm_feature(&cpu->env, ARM_FEATURE_M)) {
                elf_name = target_big_endian() ? "armv7mb" : "armv7ml";
            } else {
                elf_name = target_big_endian() ? "armv7b" : "armv7l";
            }
        } else {
            midr_part = midr >> 4;
            len_part = 7;
            if (arm_feature(&cpu->env, ARM_FEATURE_V6)) {
                arch = 6;
                elf_name = target_big_endian() ? "armv6b" : "armv6l";
            } else if (arm_feature(&cpu->env, ARM_FEATURE_V5)) {
                arch = 5;
                elf_name = target_big_endian() ? "armv5tb" : "armv5tl";
            } else {
                arch = 4;
                elf_name = target_big_endian() ? "armv4b" : "armv4l";
            }
        }
    }
#endif

    num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    for (int i = 0; i < num_cpus; i++) {
        dprintf(fd,
                "processor\t: %d\n"
                "model name\t: ARMv%d Processor rev %d (%s)\n"
                "BogoMIPS\t: 100.00\n"
                "Features\t:",
                i, arch, midr_rev, elf_name);

        for (target_ulong j = elf_hwcap; j ; j &= j - 1) {
            dprintf(fd, " %s", elf_hwcap_str(ctz64(j)));
        }
        for (target_ulong j = elf_hwcap2; j ; j &= j - 1) {
            dprintf(fd, " %s", elf_hwcap2_str(ctz64(j)));
        }

        dprintf(fd, "\n"
                "CPU implementer\t: 0x%02x\n"
                "CPU architecture: %d\n"
                "CPU variant\t: 0x%0*x\n",
                midr_impl, arch, len_var, midr_var);
        if (arch >= 7) {
            dprintf(fd, "CPU part\t: 0x%0*x\n", len_part, midr_part);
        }
        dprintf(fd, "CPU revision\t: %d\n\n", midr_rev);
    }

    if (arch < 8) {
        dprintf(fd, "Hardware\t: QEMU v%s %s\n", QEMU_VERSION,
                cpu->dtb_compatible ? : "");
        dprintf(fd, "Revision\t: 0000\n");
        dprintf(fd, "Serial\t\t: 0000000000000000\n");
    }
    return 0;
}
#define HAVE_ARCH_PROC_CPUINFO

#endif /* ARM_TARGET_PROC_H */
