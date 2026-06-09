/*
 * ppc specific proc functions for linux-user
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef PPC_TARGET_PROC_H
#define PPC_TARGET_PROC_H

static int open_cpuinfo(CPUArchState *cpu_env, int fd)
{
    int i, num_cpus;

    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(env_cpu(cpu_env));
    DeviceClass *dc = DEVICE_CLASS(ppc_cpu_get_family_class(pcc));

    num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    for (i = 0; i < num_cpus; i++) {
        dprintf(fd, "processor:\t: %d\n", i);
        dprintf(fd, "cpu:\t\t: %s%s\n",
                    dc->desc,
                    pcc->insns_flags & PPC_ALTIVEC ? ", altivec supported":"");
        dprintf(fd, "clock\t\t: 3425.000000MHz\n");
        dprintf(fd, "revision\t: %d.%d (pvr %04x %04x)\n\n",
                    (pcc->pvr >> 8) & 0x0f, pcc->pvr & 0x0f,
                    pcc->pvr >> 16, pcc->pvr & 0xffff);
    }

    dprintf(fd, "timebase\t: 512000000\n");
    dprintf(fd, "platform\t: pSeries\n");
    dprintf(fd, "model\t\t: IBM pSeries (QEMU user v" QEMU_VERSION ")\n");
    dprintf(fd, "machine\t\t: CHRP IBM pSeries\n");

    return 0;
}
#define HAVE_ARCH_PROC_CPUINFO

#endif /* PPC_TARGET_PROC_H */
