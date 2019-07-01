/*
 * Intel MPTable generator
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * Authors:
 *   Sergio Lopez <slp@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_I386_MPTABLE_H
#define HW_I386_MPTABLE_H

#define APIC_VERSION     0x14
#define CPU_STEPPING     0x600
#define CPU_FEATURE_APIC 0x200
#define CPU_FEATURE_FPU  0x001
#define MPC_SPEC         0x4

#define MP_IRQDIR_DEFAULT 0
#define MP_IRQDIR_HIGH    1
#define MP_IRQDIR_LOW     3

static const char MPC_OEM[]        = "QEMU    ";
static const char MPC_PRODUCT_ID[] = "000000000000";
static const char BUS_TYPE_ISA[]   = "ISA   ";

#define IO_APIC_DEFAULT_PHYS_BASE 0xfec00000
#define APIC_DEFAULT_PHYS_BASE    0xfee00000
#define APIC_VERSION              0x14

char *mptable_generate(int ncpus, int table_base, int *mptable_size);

#endif
