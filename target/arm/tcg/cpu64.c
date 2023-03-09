/*
 * QEMU AArch64 TCG CPUs
 *
 * Copyright (c) 2013 Linaro Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "qemu/module.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"
#include "internals.h"
#include "cpregs.h"

static void aarch64_a35_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "arm,cortex-a35";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);

    /* From B2.2 AArch64 identification registers. */
    cpu->midr = 0x411fd040;
    cpu->revidr = 0;
    cpu->ctr = 0x84448004;
    cpu->isar.id_pfr0 = 0x00000131;
    cpu->isar.id_pfr1 = 0x00011011;
    cpu->isar.id_dfr0 = 0x03010066;
    cpu->id_afr0 = 0;
    cpu->isar.id_mmfr0 = 0x10201105;
    cpu->isar.id_mmfr1 = 0x40000000;
    cpu->isar.id_mmfr2 = 0x01260000;
    cpu->isar.id_mmfr3 = 0x02102211;
    cpu->isar.id_isar0 = 0x02101110;
    cpu->isar.id_isar1 = 0x13112111;
    cpu->isar.id_isar2 = 0x21232042;
    cpu->isar.id_isar3 = 0x01112131;
    cpu->isar.id_isar4 = 0x00011142;
    cpu->isar.id_isar5 = 0x00011121;
    cpu->isar.id_aa64pfr0 = 0x00002222;
    cpu->isar.id_aa64pfr1 = 0;
    cpu->isar.id_aa64dfr0 = 0x10305106;
    cpu->isar.id_aa64dfr1 = 0;
    cpu->isar.id_aa64isar0 = 0x00011120;
    cpu->isar.id_aa64isar1 = 0;
    cpu->isar.id_aa64mmfr0 = 0x00101122;
    cpu->isar.id_aa64mmfr1 = 0;
    cpu->clidr = 0x0a200023;
    cpu->dcz_blocksize = 4;

    /* From B2.4 AArch64 Virtual Memory control registers */
    cpu->reset_sctlr = 0x00c50838;

    /* From B2.10 AArch64 performance monitor registers */
    cpu->isar.reset_pmcr_el0 = 0x410a3000;

    /* From B2.29 Cache ID registers */
    cpu->ccsidr[0] = 0x700fe01a; /* 32KB L1 dcache */
    cpu->ccsidr[1] = 0x201fe00a; /* 32KB L1 icache */
    cpu->ccsidr[2] = 0x703fe03a; /* 512KB L2 cache */

    /* From B3.5 VGIC Type register */
    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    cpu->gic_pribits = 5;

    /* From C6.4 Debug ID Register */
    cpu->isar.dbgdidr = 0x3516d000;
    /* From C6.5 Debug Device ID Register */
    cpu->isar.dbgdevid = 0x00110f13;
    /* From C6.6 Debug Device ID Register 1 */
    cpu->isar.dbgdevid1 = 0x2;

    /* From Cortex-A35 SIMD and Floating-point Support r1p0 */
    /* From 3.2 AArch32 register summary */
    cpu->reset_fpsid = 0x41034043;

    /* From 2.2 AArch64 register summary */
    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x12111111;
    cpu->isar.mvfr2 = 0x00000043;

    /* These values are the same with A53/A57/A72. */
    define_cortex_a72_a57_a53_cp_reginfo(cpu);
}

static void aarch64_a55_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "arm,cortex-a55";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);

    /* Ordered by B2.4 AArch64 registers by functional group */
    cpu->clidr = 0x82000023;
    cpu->ctr = 0x84448004; /* L1Ip = VIPT */
    cpu->dcz_blocksize = 4; /* 64 bytes */
    cpu->isar.id_aa64dfr0  = 0x0000000010305408ull;
    cpu->isar.id_aa64isar0 = 0x0000100010211120ull;
    cpu->isar.id_aa64isar1 = 0x0000000000100001ull;
    cpu->isar.id_aa64mmfr0 = 0x0000000000101122ull;
    cpu->isar.id_aa64mmfr1 = 0x0000000010212122ull;
    cpu->isar.id_aa64mmfr2 = 0x0000000000001011ull;
    cpu->isar.id_aa64pfr0  = 0x0000000010112222ull;
    cpu->isar.id_aa64pfr1  = 0x0000000000000010ull;
    cpu->id_afr0       = 0x00000000;
    cpu->isar.id_dfr0  = 0x04010088;
    cpu->isar.id_isar0 = 0x02101110;
    cpu->isar.id_isar1 = 0x13112111;
    cpu->isar.id_isar2 = 0x21232042;
    cpu->isar.id_isar3 = 0x01112131;
    cpu->isar.id_isar4 = 0x00011142;
    cpu->isar.id_isar5 = 0x01011121;
    cpu->isar.id_isar6 = 0x00000010;
    cpu->isar.id_mmfr0 = 0x10201105;
    cpu->isar.id_mmfr1 = 0x40000000;
    cpu->isar.id_mmfr2 = 0x01260000;
    cpu->isar.id_mmfr3 = 0x02122211;
    cpu->isar.id_mmfr4 = 0x00021110;
    cpu->isar.id_pfr0  = 0x10010131;
    cpu->isar.id_pfr1  = 0x00011011;
    cpu->isar.id_pfr2  = 0x00000011;
    cpu->midr = 0x412FD050;          /* r2p0 */
    cpu->revidr = 0;

    /* From B2.23 CCSIDR_EL1 */
    cpu->ccsidr[0] = 0x700fe01a; /* 32KB L1 dcache */
    cpu->ccsidr[1] = 0x200fe01a; /* 32KB L1 icache */
    cpu->ccsidr[2] = 0x703fe07a; /* 512KB L2 cache */

    /* From B2.96 SCTLR_EL3 */
    cpu->reset_sctlr = 0x30c50838;

    /* From B4.45 ICH_VTR_EL2 */
    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    cpu->gic_pribits = 5;

    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x13211111;
    cpu->isar.mvfr2 = 0x00000043;

    /* From D5.4 AArch64 PMU register summary */
    cpu->isar.reset_pmcr_el0 = 0x410b3000;
}

static void aarch64_a72_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "arm,cortex-a72";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);
    cpu->midr = 0x410fd083;
    cpu->revidr = 0x00000000;
    cpu->reset_fpsid = 0x41034080;
    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x12111111;
    cpu->isar.mvfr2 = 0x00000043;
    cpu->ctr = 0x8444c004;
    cpu->reset_sctlr = 0x00c50838;
    cpu->isar.id_pfr0 = 0x00000131;
    cpu->isar.id_pfr1 = 0x00011011;
    cpu->isar.id_dfr0 = 0x03010066;
    cpu->id_afr0 = 0x00000000;
    cpu->isar.id_mmfr0 = 0x10201105;
    cpu->isar.id_mmfr1 = 0x40000000;
    cpu->isar.id_mmfr2 = 0x01260000;
    cpu->isar.id_mmfr3 = 0x02102211;
    cpu->isar.id_isar0 = 0x02101110;
    cpu->isar.id_isar1 = 0x13112111;
    cpu->isar.id_isar2 = 0x21232042;
    cpu->isar.id_isar3 = 0x01112131;
    cpu->isar.id_isar4 = 0x00011142;
    cpu->isar.id_isar5 = 0x00011121;
    cpu->isar.id_aa64pfr0 = 0x00002222;
    cpu->isar.id_aa64dfr0 = 0x10305106;
    cpu->isar.id_aa64isar0 = 0x00011120;
    cpu->isar.id_aa64mmfr0 = 0x00001124;
    cpu->isar.dbgdidr = 0x3516d000;
    cpu->isar.dbgdevid = 0x01110f13;
    cpu->isar.dbgdevid1 = 0x2;
    cpu->isar.reset_pmcr_el0 = 0x41023000;
    cpu->clidr = 0x0a200023;
    cpu->ccsidr[0] = 0x701fe00a; /* 32KB L1 dcache */
    cpu->ccsidr[1] = 0x201fe012; /* 48KB L1 icache */
    cpu->ccsidr[2] = 0x707fe07a; /* 1MB L2 cache */
    cpu->dcz_blocksize = 4; /* 64 bytes */
    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    cpu->gic_pribits = 5;
    define_cortex_a72_a57_a53_cp_reginfo(cpu);
}

static void aarch64_a76_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "arm,cortex-a76";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);

    /* Ordered by B2.4 AArch64 registers by functional group */
    cpu->clidr = 0x82000023;
    cpu->ctr = 0x8444C004;
    cpu->dcz_blocksize = 4;
    cpu->isar.id_aa64dfr0  = 0x0000000010305408ull;
    cpu->isar.id_aa64isar0 = 0x0000100010211120ull;
    cpu->isar.id_aa64isar1 = 0x0000000000100001ull;
    cpu->isar.id_aa64mmfr0 = 0x0000000000101122ull;
    cpu->isar.id_aa64mmfr1 = 0x0000000010212122ull;
    cpu->isar.id_aa64mmfr2 = 0x0000000000001011ull;
    cpu->isar.id_aa64pfr0  = 0x1100000010111112ull; /* GIC filled in later */
    cpu->isar.id_aa64pfr1  = 0x0000000000000010ull;
    cpu->id_afr0       = 0x00000000;
    cpu->isar.id_dfr0  = 0x04010088;
    cpu->isar.id_isar0 = 0x02101110;
    cpu->isar.id_isar1 = 0x13112111;
    cpu->isar.id_isar2 = 0x21232042;
    cpu->isar.id_isar3 = 0x01112131;
    cpu->isar.id_isar4 = 0x00010142;
    cpu->isar.id_isar5 = 0x01011121;
    cpu->isar.id_isar6 = 0x00000010;
    cpu->isar.id_mmfr0 = 0x10201105;
    cpu->isar.id_mmfr1 = 0x40000000;
    cpu->isar.id_mmfr2 = 0x01260000;
    cpu->isar.id_mmfr3 = 0x02122211;
    cpu->isar.id_mmfr4 = 0x00021110;
    cpu->isar.id_pfr0  = 0x10010131;
    cpu->isar.id_pfr1  = 0x00010000; /* GIC filled in later */
    cpu->isar.id_pfr2  = 0x00000011;
    cpu->midr = 0x414fd0b1;          /* r4p1 */
    cpu->revidr = 0;

    /* From B2.18 CCSIDR_EL1 */
    cpu->ccsidr[0] = 0x701fe01a; /* 64KB L1 dcache */
    cpu->ccsidr[1] = 0x201fe01a; /* 64KB L1 icache */
    cpu->ccsidr[2] = 0x707fe03a; /* 512KB L2 cache */

    /* From B2.93 SCTLR_EL3 */
    cpu->reset_sctlr = 0x30c50838;

    /* From B4.23 ICH_VTR_EL2 */
    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    cpu->gic_pribits = 5;

    /* From B5.1 AdvSIMD AArch64 register summary */
    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x13211111;
    cpu->isar.mvfr2 = 0x00000043;

    /* From D5.1 AArch64 PMU register summary */
    cpu->isar.reset_pmcr_el0 = 0x410b3000;
}

static void aarch64_a64fx_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "arm,a64fx";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);
    cpu->midr = 0x461f0010;
    cpu->revidr = 0x00000000;
    cpu->ctr = 0x86668006;
    cpu->reset_sctlr = 0x30000180;
    cpu->isar.id_aa64pfr0 =   0x0000000101111111; /* No RAS Extensions */
    cpu->isar.id_aa64pfr1 = 0x0000000000000000;
    cpu->isar.id_aa64dfr0 = 0x0000000010305408;
    cpu->isar.id_aa64dfr1 = 0x0000000000000000;
    cpu->id_aa64afr0 = 0x0000000000000000;
    cpu->id_aa64afr1 = 0x0000000000000000;
    cpu->isar.id_aa64mmfr0 = 0x0000000000001122;
    cpu->isar.id_aa64mmfr1 = 0x0000000011212100;
    cpu->isar.id_aa64mmfr2 = 0x0000000000001011;
    cpu->isar.id_aa64isar0 = 0x0000000010211120;
    cpu->isar.id_aa64isar1 = 0x0000000000010001;
    cpu->isar.id_aa64zfr0 = 0x0000000000000000;
    cpu->clidr = 0x0000000080000023;
    cpu->ccsidr[0] = 0x7007e01c; /* 64KB L1 dcache */
    cpu->ccsidr[1] = 0x2007e01c; /* 64KB L1 icache */
    cpu->ccsidr[2] = 0x70ffe07c; /* 8MB L2 cache */
    cpu->dcz_blocksize = 6; /* 256 bytes */
    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    cpu->gic_pribits = 5;

    /* The A64FX supports only 128, 256 and 512 bit vector lengths */
    aarch64_add_sve_properties(obj);
    cpu->sve_vq.supported = (1 << 0)  /* 128bit */
                          | (1 << 1)  /* 256bit */
                          | (1 << 3); /* 512bit */

    cpu->isar.reset_pmcr_el0 = 0x46014040;

    /* TODO:  Add A64FX specific HPC extension registers */
}

static void aarch64_neoverse_n1_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "arm,neoverse-n1";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);

    /* Ordered by B2.4 AArch64 registers by functional group */
    cpu->clidr = 0x82000023;
    cpu->ctr = 0x8444c004;
    cpu->dcz_blocksize = 4;
    cpu->isar.id_aa64dfr0  = 0x0000000110305408ull;
    cpu->isar.id_aa64isar0 = 0x0000100010211120ull;
    cpu->isar.id_aa64isar1 = 0x0000000000100001ull;
    cpu->isar.id_aa64mmfr0 = 0x0000000000101125ull;
    cpu->isar.id_aa64mmfr1 = 0x0000000010212122ull;
    cpu->isar.id_aa64mmfr2 = 0x0000000000001011ull;
    cpu->isar.id_aa64pfr0  = 0x1100000010111112ull; /* GIC filled in later */
    cpu->isar.id_aa64pfr1  = 0x0000000000000020ull;
    cpu->id_afr0       = 0x00000000;
    cpu->isar.id_dfr0  = 0x04010088;
    cpu->isar.id_isar0 = 0x02101110;
    cpu->isar.id_isar1 = 0x13112111;
    cpu->isar.id_isar2 = 0x21232042;
    cpu->isar.id_isar3 = 0x01112131;
    cpu->isar.id_isar4 = 0x00010142;
    cpu->isar.id_isar5 = 0x01011121;
    cpu->isar.id_isar6 = 0x00000010;
    cpu->isar.id_mmfr0 = 0x10201105;
    cpu->isar.id_mmfr1 = 0x40000000;
    cpu->isar.id_mmfr2 = 0x01260000;
    cpu->isar.id_mmfr3 = 0x02122211;
    cpu->isar.id_mmfr4 = 0x00021110;
    cpu->isar.id_pfr0  = 0x10010131;
    cpu->isar.id_pfr1  = 0x00010000; /* GIC filled in later */
    cpu->isar.id_pfr2  = 0x00000011;
    cpu->midr = 0x414fd0c1;          /* r4p1 */
    cpu->revidr = 0;

    /* From B2.23 CCSIDR_EL1 */
    cpu->ccsidr[0] = 0x701fe01a; /* 64KB L1 dcache */
    cpu->ccsidr[1] = 0x201fe01a; /* 64KB L1 icache */
    cpu->ccsidr[2] = 0x70ffe03a; /* 1MB L2 cache */

    /* From B2.98 SCTLR_EL3 */
    cpu->reset_sctlr = 0x30c50838;

    /* From B4.23 ICH_VTR_EL2 */
    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    cpu->gic_pribits = 5;

    /* From B5.1 AdvSIMD AArch64 register summary */
    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x13211111;
    cpu->isar.mvfr2 = 0x00000043;

    /* From D5.1 AArch64 PMU register summary */
    cpu->isar.reset_pmcr_el0 = 0x410c3000;
}

static const ARMCPUInfo aarch64_cpus[] = {
    { .name = "cortex-a35",         .initfn = aarch64_a35_initfn },
    { .name = "cortex-a55",         .initfn = aarch64_a55_initfn },
    { .name = "cortex-a72",         .initfn = aarch64_a72_initfn },
    { .name = "cortex-a76",         .initfn = aarch64_a76_initfn },
    { .name = "a64fx",              .initfn = aarch64_a64fx_initfn },
    { .name = "neoverse-n1",        .initfn = aarch64_neoverse_n1_initfn },
};

static void aarch64_cpu_register_types(void)
{
    size_t i;

    for (i = 0; i < ARRAY_SIZE(aarch64_cpus); ++i) {
        aarch64_cpu_register(&aarch64_cpus[i]);
    }
}

type_init(aarch64_cpu_register_types)
