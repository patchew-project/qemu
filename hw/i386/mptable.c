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

#include "qemu/osdep.h"
#include "hw/i386/mptable.h"
#include "standard-headers/linux/mpspec_def.h"

static int mptable_checksum(char *buf, int size)
{
    int i;
    int checksum = 0;

    for (i = 0; i < size; i++) {
        checksum += buf[i];
    }

    return checksum;
}

/*
 * Generate an MPTable for "ncpus". "apic_id" must be the next available
 * APIC ID (last CPU apic_id + 1). "table_base" is the physical location
 * in the Guest where the caller intends to write the table, needed to
 * fill the "physptr" field from the "mpf_intel" structure.
 *
 * On success, return a newly allocated buffer, that must be freed by the
 * caller using "g_free" when it's no longer needed, and update
 * "mptable_size" with the size of the buffer.
 */
char *mptable_generate(int ncpus, int apic_id,
                        int table_base, int *mptable_size)
{
    struct mpf_intel *mpf;
    struct mpc_table *table;
    struct mpc_cpu *cpu;
    struct mpc_bus *bus;
    struct mpc_ioapic *ioapic;
    struct mpc_intsrc *intsrc;
    struct mpc_lintsrc *lintsrc;
    const char mpc_signature[] = MPC_SIGNATURE;
    const char smp_magic_ident[] = "_MP_";
    char *mptable;
    int checksum = 0;
    int offset = 0;
    int ssize;
    int i;

    ssize = sizeof(struct mpf_intel);
    mptable = g_malloc0(ssize);

    mpf = (struct mpf_intel *) mptable;
    memcpy(mpf->signature, smp_magic_ident, sizeof(smp_magic_ident) - 1);
    mpf->length = 1;
    mpf->specification = 4;
    mpf->physptr = table_base + ssize;
    mpf->checksum -= mptable_checksum((char *) mpf, ssize);
    offset = ssize + sizeof(struct mpc_table);

    ssize = sizeof(struct mpc_cpu);
    for (i = 0; i < ncpus; i++) {
        mptable = g_realloc(mptable, offset + ssize);
        cpu = (struct mpc_cpu *) (mptable + offset);
        cpu->type = MP_PROCESSOR;
        cpu->apicid = i;
        cpu->apicver = APIC_VERSION;
        cpu->cpuflag = CPU_ENABLED;
        if (i == 0) {
            cpu->cpuflag |= CPU_BOOTPROCESSOR;
        }
        cpu->cpufeature = CPU_STEPPING;
        cpu->featureflag = CPU_FEATURE_APIC | CPU_FEATURE_FPU;
        checksum += mptable_checksum((char *) cpu, ssize);
        offset += ssize;
    }

    ssize = sizeof(struct mpc_bus);
    mptable = g_realloc(mptable, offset + ssize);
    bus = (struct mpc_bus *) (mptable + offset);
    bus->type = MP_BUS;
    bus->busid = 0;
    memcpy(bus->bustype, BUS_TYPE_ISA, sizeof(BUS_TYPE_ISA) - 1);
    checksum += mptable_checksum((char *) bus, ssize);
    offset += ssize;

    ssize = sizeof(struct mpc_ioapic);
    mptable = g_realloc(mptable, offset + ssize);
    ioapic = (struct mpc_ioapic *) (mptable + offset);
    ioapic->type = MP_IOAPIC;
    ioapic->apicid = ncpus + 1;
    ioapic->apicver = APIC_VERSION;
    ioapic->flags = MPC_APIC_USABLE;
    ioapic->apicaddr = IO_APIC_DEFAULT_PHYS_BASE;
    checksum += mptable_checksum((char *) ioapic, ssize);
    offset += ssize;

    ssize = sizeof(struct mpc_intsrc);
    for (i = 0; i < 16; i++) {
        mptable = g_realloc(mptable, offset + ssize);
        intsrc = (struct mpc_intsrc *) (mptable + offset);
        intsrc->type = MP_INTSRC;
        intsrc->irqtype = mp_INT;
        intsrc->irqflag = MP_IRQDIR_DEFAULT;
        intsrc->srcbus = 0;
        intsrc->srcbusirq = i;
        intsrc->dstapic = ncpus + 1;
        intsrc->dstirq = i;
        checksum += mptable_checksum((char *) intsrc, ssize);
        offset += ssize;
    }

    ssize = sizeof(struct mpc_lintsrc);
    mptable = g_realloc(mptable, offset + (ssize * 2));
    lintsrc = (struct mpc_lintsrc *) (mptable + offset);
    lintsrc->type = MP_LINTSRC;
    lintsrc->irqtype = mp_ExtINT;
    lintsrc->irqflag = MP_IRQDIR_DEFAULT;
    lintsrc->srcbusid = 0;
    lintsrc->srcbusirq = 0;
    lintsrc->destapic = 0;
    lintsrc->destapiclint = 0;
    checksum += mptable_checksum((char *) lintsrc, ssize);
    offset += ssize;

    lintsrc = (struct mpc_lintsrc *) (mptable + offset);
    lintsrc->type = MP_LINTSRC;
    lintsrc->irqtype = mp_NMI;
    lintsrc->irqflag = MP_IRQDIR_DEFAULT;
    lintsrc->srcbusid = 0;
    lintsrc->srcbusirq = 0;
    lintsrc->destapic = 0xFF;
    lintsrc->destapiclint = 1;
    checksum += mptable_checksum((char *) lintsrc, ssize);
    offset += ssize;

    ssize = sizeof(struct mpc_table);
    table = (struct mpc_table *) (mptable + sizeof(struct mpf_intel));
    memcpy(table->signature, mpc_signature, sizeof(mpc_signature) - 1);
    table->length = offset - sizeof(struct mpf_intel);
    table->spec = MPC_SPEC;
    memcpy(table->oem, MPC_OEM, sizeof(MPC_OEM) - 1);
    memcpy(table->productid, MPC_PRODUCT_ID, sizeof(MPC_PRODUCT_ID) - 1);
    table->lapic = APIC_DEFAULT_PHYS_BASE;
    checksum += mptable_checksum((char *) table, ssize);
    table->checksum -= checksum;

    *mptable_size = offset;
    return mptable;
}
