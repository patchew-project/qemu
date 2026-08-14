/*
 *  i386 CPU dump to FILE
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "qemu/qemu-print.h"
#ifndef CONFIG_USER_ONLY
#include "hw/i386/apic_internal.h"
#endif
#ifdef CONFIG_KVM
#include <linux/kvm.h>
#endif

/***********************************************************/
/* x86 debug */

static const char * const cc_op_str[] = {
    [CC_OP_DYNAMIC] = "DYNAMIC",

    [CC_OP_EFLAGS] = "EFLAGS",
    [CC_OP_ADCX] = "ADCX",
    [CC_OP_ADOX] = "ADOX",
    [CC_OP_ADCOX] = "ADCOX",

    [CC_OP_MULB] = "MULB",
    [CC_OP_MULW] = "MULW",
    [CC_OP_MULL] = "MULL",
    [CC_OP_MULQ] = "MULQ",

    [CC_OP_ADDB] = "ADDB",
    [CC_OP_ADDW] = "ADDW",
    [CC_OP_ADDL] = "ADDL",
    [CC_OP_ADDQ] = "ADDQ",

    [CC_OP_ADCB] = "ADCB",
    [CC_OP_ADCW] = "ADCW",
    [CC_OP_ADCL] = "ADCL",
    [CC_OP_ADCQ] = "ADCQ",

    [CC_OP_SUBB] = "SUBB",
    [CC_OP_SUBW] = "SUBW",
    [CC_OP_SUBL] = "SUBL",
    [CC_OP_SUBQ] = "SUBQ",

    [CC_OP_SBBB] = "SBBB",
    [CC_OP_SBBW] = "SBBW",
    [CC_OP_SBBL] = "SBBL",
    [CC_OP_SBBQ] = "SBBQ",

    [CC_OP_LOGICB] = "LOGICB",
    [CC_OP_LOGICW] = "LOGICW",
    [CC_OP_LOGICL] = "LOGICL",
    [CC_OP_LOGICQ] = "LOGICQ",

    [CC_OP_INCB] = "INCB",
    [CC_OP_INCW] = "INCW",
    [CC_OP_INCL] = "INCL",
    [CC_OP_INCQ] = "INCQ",

    [CC_OP_DECB] = "DECB",
    [CC_OP_DECW] = "DECW",
    [CC_OP_DECL] = "DECL",
    [CC_OP_DECQ] = "DECQ",

    [CC_OP_SHLB] = "SHLB",
    [CC_OP_SHLW] = "SHLW",
    [CC_OP_SHLL] = "SHLL",
    [CC_OP_SHLQ] = "SHLQ",

    [CC_OP_SARB] = "SARB",
    [CC_OP_SARW] = "SARW",
    [CC_OP_SARL] = "SARL",
    [CC_OP_SARQ] = "SARQ",

    [CC_OP_BMILGB] = "BMILGB",
    [CC_OP_BMILGW] = "BMILGW",
    [CC_OP_BMILGL] = "BMILGL",
    [CC_OP_BMILGQ] = "BMILGQ",

    [CC_OP_POPCNT] = "POPCNT",

    [CC_OP_SBB_SELF] = "SBBx,x",
};

static void
cpu_x86_dump_seg_cache(CPUX86State *env, FILE *f,
                       const char *name, struct SegmentCache *sc)
{
#ifdef TARGET_X86_64
    if (env->hflags & HF_CS64_MASK) {
        qemu_fprintf(f, "%-3s=%04x %016" PRIx64 " %08x %08x", name,
                     sc->selector, sc->base, sc->limit,
                     sc->flags & 0x00ffff00);
    } else
#endif
    {
        qemu_fprintf(f, "%-3s=%04x %08x %08x %08x", name, sc->selector,
                     (uint32_t)sc->base, sc->limit,
                     sc->flags & 0x00ffff00);
    }

    if (!(env->hflags & HF_PE_MASK) || !(sc->flags & DESC_P_MASK))
        goto done;

    qemu_fprintf(f, " DPL=%d ",
                 (sc->flags & DESC_DPL_MASK) >> DESC_DPL_SHIFT);
    if (sc->flags & DESC_S_MASK) {
        if (sc->flags & DESC_CS_MASK) {
            qemu_fprintf(f, (sc->flags & DESC_L_MASK) ? "CS64" :
                         ((sc->flags & DESC_B_MASK) ? "CS32" : "CS16"));
            qemu_fprintf(f, " [%c%c", (sc->flags & DESC_C_MASK) ? 'C' : '-',
                         (sc->flags & DESC_R_MASK) ? 'R' : '-');
        } else {
            qemu_fprintf(f, (sc->flags & DESC_B_MASK
                             || env->hflags & HF_LMA_MASK)
                         ? "DS  " : "DS16");
            qemu_fprintf(f, " [%c%c", (sc->flags & DESC_E_MASK) ? 'E' : '-',
                         (sc->flags & DESC_W_MASK) ? 'W' : '-');
        }
        qemu_fprintf(f, "%c]", (sc->flags & DESC_A_MASK) ? 'A' : '-');
    } else {
        static const char *sys_type_name[2][16] = {
            { /* 32 bit mode */
                "Reserved", "TSS16-avl", "LDT", "TSS16-busy",
                "CallGate16", "TaskGate", "IntGate16", "TrapGate16",
                "Reserved", "TSS32-avl", "Reserved", "TSS32-busy",
                "CallGate32", "Reserved", "IntGate32", "TrapGate32"
            },
            { /* 64 bit mode */
                "<hiword>", "Reserved", "LDT", "Reserved", "Reserved",
                "Reserved", "Reserved", "Reserved", "Reserved",
                "TSS64-avl", "Reserved", "TSS64-busy", "CallGate64",
                "Reserved", "IntGate64", "TrapGate64"
            }
        };
        qemu_fprintf(f, "%s",
                     sys_type_name[(env->hflags & HF_LMA_MASK) ? 1 : 0]
                     [(sc->flags & DESC_TYPE_MASK) >> DESC_TYPE_SHIFT]);
    }
done:
    qemu_fprintf(f, "\n");
}

#ifndef CONFIG_USER_ONLY

/* ARRAY_SIZE check is not required because
 * DeliveryMode(dm) has a size of 3 bit.
 */
static inline const char *dm2str(uint32_t dm)
{
    static const char *str[] = {
        "Fixed",
        "...",
        "SMI",
        "...",
        "NMI",
        "INIT",
        "...",
        "ExtINT"
    };
    return str[dm];
}

static void dump_apic_lvt(const char *name, uint32_t lvt, bool is_timer)
{
    uint32_t dm = (lvt & APIC_LVT_DELIV_MOD) >> APIC_LVT_DELIV_MOD_SHIFT;
    qemu_printf("%s\t 0x%08x %s %-5s %-6s %-7s %-12s %-6s",
                name, lvt,
                lvt & APIC_LVT_INT_POLARITY ? "active-lo" : "active-hi",
                lvt & APIC_LVT_LEVEL_TRIGGER ? "level" : "edge",
                lvt & APIC_LVT_MASKED ? "masked" : "",
                lvt & APIC_LVT_DELIV_STS ? "pending" : "",
                !is_timer ?
                    "" : lvt & APIC_LVT_TIMER_PERIODIC ?
                            "periodic" : lvt & APIC_LVT_TIMER_TSCDEADLINE ?
                                            "tsc-deadline" : "one-shot",
                dm2str(dm));
    if (dm != APIC_DM_NMI) {
        qemu_printf(" (vec %u)\n", lvt & APIC_VECTOR_MASK);
    } else {
        qemu_printf("\n");
    }
}

/* ARRAY_SIZE check is not required because
 * destination shorthand has a size of 2 bit.
 */
static inline const char *shorthand2str(uint32_t shorthand)
{
    const char *str[] = {
        "no-shorthand", "self", "all-self", "all"
    };
    return str[shorthand];
}

static inline uint8_t divider_conf(uint32_t divide_conf)
{
    uint8_t divide_val = ((divide_conf & 0x8) >> 1) | (divide_conf & 0x3);

    return divide_val == 7 ? 1 : 2 << divide_val;
}

static inline void mask2str(char *str, uint32_t val, uint8_t size)
{
    while (size--) {
        *str++ = (val >> size) & 1 ? '1' : '0';
    }
    *str = 0;
}

#define MAX_LOGICAL_APIC_ID_MASK_SIZE 16

static void dump_apic_icr(APICCommonState *s, CPUX86State *env)
{
    uint32_t icr = s->icr[0], icr2 = s->icr[1];
    uint8_t dest_shorthand = \
        (icr & APIC_ICR_DEST_SHORT) >> APIC_ICR_DEST_SHORT_SHIFT;
    bool logical_mod = icr & APIC_ICR_DEST_MOD;
    char apic_id_str[MAX_LOGICAL_APIC_ID_MASK_SIZE + 1];
    uint32_t dest_field;
    bool x2apic;

    qemu_printf("ICR\t 0x%08x %s %s %s %s\n",
                icr,
                logical_mod ? "logical" : "physical",
                icr & APIC_ICR_TRIGGER_MOD ? "level" : "edge",
                icr & APIC_ICR_LEVEL ? "assert" : "de-assert",
                shorthand2str(dest_shorthand));

    qemu_printf("ICR2\t 0x%08x", icr2);
    if (dest_shorthand != 0) {
        qemu_printf("\n");
        return;
    }
    x2apic = env->features[FEAT_1_ECX] & CPUID_EXT_X2APIC;
    dest_field = x2apic ? icr2 : icr2 >> APIC_ICR_DEST_SHIFT;

    if (!logical_mod) {
        if (x2apic) {
            qemu_printf(" cpu %u (X2APIC ID)\n", dest_field);
        } else {
            qemu_printf(" cpu %u (APIC ID)\n",
                        dest_field & APIC_LOGDEST_XAPIC_ID);
        }
        return;
    }

    if (s->dest_mode == 0xf) { /* flat mode */
        mask2str(apic_id_str, icr2 >> APIC_ICR_DEST_SHIFT, 8);
        qemu_printf(" mask %s (APIC ID)\n", apic_id_str);
    } else if (s->dest_mode == 0) { /* cluster mode */
        if (x2apic) {
            mask2str(apic_id_str, dest_field & APIC_LOGDEST_X2APIC_ID, 16);
            qemu_printf(" cluster %u mask %s (X2APIC ID)\n",
                        dest_field >> APIC_LOGDEST_X2APIC_SHIFT, apic_id_str);
        } else {
            mask2str(apic_id_str, dest_field & APIC_LOGDEST_XAPIC_ID, 4);
            qemu_printf(" cluster %u mask %s (APIC ID)\n",
                        dest_field >> APIC_LOGDEST_XAPIC_SHIFT, apic_id_str);
        }
    }
}

static void dump_apic_interrupt(const char *name, uint32_t *ireg_tab,
                                uint32_t *tmr_tab)
{
    int i, empty = true;

    qemu_printf("%s\t ", name);
    for (i = 0; i < 256; i++) {
        if (apic_get_bit(ireg_tab, i)) {
            qemu_printf("%u%s ", i,
                        apic_get_bit(tmr_tab, i) ? "(level)" : "");
            empty = false;
        }
    }
    qemu_printf("%s\n", empty ? "(none)" : "");
}

void x86_cpu_dump_local_apic_state(CPUState *cs, int flags)
{
    X86CPU *cpu = X86_CPU(cs);
    APICCommonState *s = cpu->apic_state;
    if (!s) {
        qemu_printf("local apic state not available\n");
        return;
    }
    uint32_t *lvt = s->lvt;

    qemu_printf("dumping local APIC state for CPU %-2u\n\n",
                CPU(cpu)->cpu_index);
    dump_apic_lvt("LVT0", lvt[APIC_LVT_LINT0], false);
    dump_apic_lvt("LVT1", lvt[APIC_LVT_LINT1], false);
    dump_apic_lvt("LVTPC", lvt[APIC_LVT_PERFORM], false);
    dump_apic_lvt("LVTERR", lvt[APIC_LVT_ERROR], false);
    dump_apic_lvt("LVTTHMR", lvt[APIC_LVT_THERMAL], false);
    dump_apic_lvt("LVTT", lvt[APIC_LVT_TIMER], true);

    qemu_printf("Timer\t DCR=0x%x (divide by %u) initial_count = %u"
                " current_count = %u\n",
                s->divide_conf & APIC_DCR_MASK,
                divider_conf(s->divide_conf),
                s->initial_count, apic_get_current_count(s));

    qemu_printf("SPIV\t 0x%08x APIC %s, focus=%s, spurious vec %u\n",
                s->spurious_vec,
                s->spurious_vec & APIC_SPURIO_ENABLED ? "enabled" : "disabled",
                s->spurious_vec & APIC_SPURIO_FOCUS ? "on" : "off",
                s->spurious_vec & APIC_VECTOR_MASK);

    dump_apic_icr(s, &cpu->env);

    qemu_printf("ESR\t 0x%08x\n", s->esr);

    dump_apic_interrupt("ISR", s->isr, s->tmr);
    dump_apic_interrupt("IRR", s->irr, s->tmr);

    qemu_printf("\nAPR 0x%02x TPR 0x%02x DFR 0x%02x LDR 0x%02x",
                s->arb_id, s->tpr, s->dest_mode, s->log_dest);
    if (s->dest_mode == 0) {
        qemu_printf("(cluster %u: id %u)",
                    s->log_dest >> APIC_LOGDEST_XAPIC_SHIFT,
                    s->log_dest & APIC_LOGDEST_XAPIC_ID);
    }
    qemu_printf(" PPR 0x%02x\n", apic_get_ppr(s));
}

#endif /* !CONFIG_USER_ONLY */

#define DUMP_CODE_BYTES_TOTAL    50
#define DUMP_CODE_BYTES_BACKWARD 20

#ifdef CONFIG_KVM
/*
 * Byte offsets into the vmcs12 blob returned by KVM_GET_NESTED_STATE.
 * Taken from struct vmcs12 in arch/x86/kvm/vmx/vmcs12.h (kernel v6.16).
 * The layout is locked by live-migration compatibility and compile-time
 * CHECK_OFFSET assertions in the kernel, so existing offsets are stable.
 * Only new fields may be appended at the end; existing fields cannot be
 * moved, deleted, or have their type changed.
 */
#define VMCS12_REVISION                  0x11e57ed0
#define VMCS12_GUEST_IA32_PAT            192
#define VMCS12_GUEST_IA32_EFER           200
#define VMCS12_HOST_IA32_PAT             256
#define VMCS12_HOST_IA32_EFER            264
#define VMCS12_GUEST_CR0                 424
#define VMCS12_GUEST_CR3                 432
#define VMCS12_GUEST_CR4                 440
#define VMCS12_GUEST_ES_BASE             448
#define VMCS12_GUEST_CS_BASE             456
#define VMCS12_GUEST_SS_BASE             464
#define VMCS12_GUEST_DS_BASE             472
#define VMCS12_GUEST_FS_BASE             480
#define VMCS12_GUEST_GS_BASE             488
#define VMCS12_GUEST_LDTR_BASE           496
#define VMCS12_GUEST_TR_BASE             504
#define VMCS12_GUEST_GDTR_BASE           512
#define VMCS12_GUEST_IDTR_BASE           520
#define VMCS12_GUEST_DR7                 528
#define VMCS12_GUEST_RSP                 536
#define VMCS12_GUEST_RIP                 544
#define VMCS12_GUEST_RFLAGS              552
#define VMCS12_GUEST_SYSENTER_ESP        568
#define VMCS12_GUEST_SYSENTER_EIP        576
#define VMCS12_HOST_CR0                  584
#define VMCS12_HOST_CR3                  592
#define VMCS12_HOST_CR4                  600
#define VMCS12_HOST_FS_BASE              608
#define VMCS12_HOST_GS_BASE              616
#define VMCS12_HOST_TR_BASE              624
#define VMCS12_HOST_GDTR_BASE            632
#define VMCS12_HOST_IDTR_BASE            640
#define VMCS12_HOST_IA32_SYSENTER_ESP    648
#define VMCS12_HOST_IA32_SYSENTER_EIP    656
#define VMCS12_HOST_RSP                  664
#define VMCS12_HOST_RIP                  672
#define VMCS12_GUEST_ES_LIMIT            840
#define VMCS12_GUEST_CS_LIMIT            844
#define VMCS12_GUEST_SS_LIMIT            848
#define VMCS12_GUEST_DS_LIMIT            852
#define VMCS12_GUEST_FS_LIMIT            856
#define VMCS12_GUEST_GS_LIMIT            860
#define VMCS12_GUEST_LDTR_LIMIT          864
#define VMCS12_GUEST_TR_LIMIT            868
#define VMCS12_GUEST_GDTR_LIMIT          872
#define VMCS12_GUEST_IDTR_LIMIT          876
#define VMCS12_GUEST_ES_AR_BYTES         880
#define VMCS12_GUEST_CS_AR_BYTES         884
#define VMCS12_GUEST_SS_AR_BYTES         888
#define VMCS12_GUEST_DS_AR_BYTES         892
#define VMCS12_GUEST_FS_AR_BYTES         896
#define VMCS12_GUEST_GS_AR_BYTES         900
#define VMCS12_GUEST_LDTR_AR_BYTES       904
#define VMCS12_GUEST_TR_AR_BYTES         908
#define VMCS12_GUEST_SYSENTER_CS         920
#define VMCS12_HOST_IA32_SYSENTER_CS     924
#define VMCS12_GUEST_ES_SELECTOR         964
#define VMCS12_GUEST_CS_SELECTOR         966
#define VMCS12_GUEST_SS_SELECTOR         968
#define VMCS12_GUEST_DS_SELECTOR         970
#define VMCS12_GUEST_FS_SELECTOR         972
#define VMCS12_GUEST_GS_SELECTOR         974
#define VMCS12_GUEST_LDTR_SELECTOR       976
#define VMCS12_GUEST_TR_SELECTOR         978
#define VMCS12_HOST_ES_SELECTOR          982
#define VMCS12_HOST_CS_SELECTOR          984
#define VMCS12_HOST_SS_SELECTOR          986
#define VMCS12_HOST_DS_SELECTOR          988
#define VMCS12_HOST_FS_SELECTOR          990
#define VMCS12_HOST_GS_SELECTOR          992
#define VMCS12_HOST_TR_SELECTOR          994

static inline uint64_t vmcs12_read64(const uint8_t *vmcs12, size_t off)
{
    uint64_t val;
    memcpy(&val, vmcs12 + off, sizeof(val));
    return val;
}

static inline uint32_t vmcs12_read32(const uint8_t *vmcs12, size_t off)
{
    uint32_t val;
    memcpy(&val, vmcs12 + off, sizeof(val));
    return val;
}

static inline uint16_t vmcs12_read16(const uint8_t *vmcs12, size_t off)
{
    uint16_t val;
    memcpy(&val, vmcs12 + off, sizeof(val));
    return val;
}

static void dump_vmcs12_nested_state(CPUX86State *env, FILE *f,
                                     const char *seg_name[6])
{
    const uint8_t *vmcs12;
    uint64_t base[8];
    uint32_t limit[8], ar[8];
    uint16_t sel[8];
    int i;

    if (!env->nested_state ||
        env->nested_state->format != KVM_STATE_NESTED_FORMAT_VMX) {
        return;
    }

    vmcs12 = env->nested_state->data.vmx[0].vmcs12;

    /* Sanity check: vmcs12 revision must match expected layout */
    if (vmcs12_read32(vmcs12, 0) != VMCS12_REVISION) {
        return;
    }

    if (env->hflags & HF_GUEST_MASK) {
        /*
         * CPU is in L2 (VMX non-root). Show L1 state from vmcs12 HOST
         * area -- this is the context L1 set up for VM-exit from L2.
         */
        qemu_fprintf(f, "\nL1 state (from vmcs12 HOST area):\n");
        qemu_fprintf(f, "RIP=%016" PRIx64 " RSP=%016" PRIx64 "\n",
                     vmcs12_read64(vmcs12, VMCS12_HOST_RIP),
                     vmcs12_read64(vmcs12, VMCS12_HOST_RSP));
        qemu_fprintf(f, "CR0=%016" PRIx64 " CR3=%016" PRIx64
                     " CR4=%016" PRIx64 "\n",
                     vmcs12_read64(vmcs12, VMCS12_HOST_CR0),
                     vmcs12_read64(vmcs12, VMCS12_HOST_CR3),
                     vmcs12_read64(vmcs12, VMCS12_HOST_CR4));
        qemu_fprintf(f, "EFER=%016" PRIx64 " PAT=%016" PRIx64 "\n",
                     vmcs12_read64(vmcs12, VMCS12_HOST_IA32_EFER),
                     vmcs12_read64(vmcs12, VMCS12_HOST_IA32_PAT));

        sel[0] = vmcs12_read16(vmcs12, VMCS12_HOST_ES_SELECTOR);
        sel[1] = vmcs12_read16(vmcs12, VMCS12_HOST_CS_SELECTOR);
        sel[2] = vmcs12_read16(vmcs12, VMCS12_HOST_SS_SELECTOR);
        sel[3] = vmcs12_read16(vmcs12, VMCS12_HOST_DS_SELECTOR);
        sel[4] = vmcs12_read16(vmcs12, VMCS12_HOST_FS_SELECTOR);
        sel[5] = vmcs12_read16(vmcs12, VMCS12_HOST_GS_SELECTOR);
        for (i = 0; i < 6; i++) {
            qemu_fprintf(f, "%-4s sel=%04x\n", seg_name[i], sel[i]);
        }
        qemu_fprintf(f, "TR   sel=%04x base=%016" PRIx64 "\n",
                     vmcs12_read16(vmcs12, VMCS12_HOST_TR_SELECTOR),
                     vmcs12_read64(vmcs12, VMCS12_HOST_TR_BASE));
        qemu_fprintf(f, "FS base=%016" PRIx64 " GS base=%016" PRIx64 "\n",
                     vmcs12_read64(vmcs12, VMCS12_HOST_FS_BASE),
                     vmcs12_read64(vmcs12, VMCS12_HOST_GS_BASE));
        qemu_fprintf(f, "GDT=     %016" PRIx64 "\n",
                     vmcs12_read64(vmcs12, VMCS12_HOST_GDTR_BASE));
        qemu_fprintf(f, "IDT=     %016" PRIx64 "\n",
                     vmcs12_read64(vmcs12, VMCS12_HOST_IDTR_BASE));
        qemu_fprintf(f, "SYSENTER cs=%08x esp=%016" PRIx64
                     " eip=%016" PRIx64 "\n",
                     vmcs12_read32(vmcs12, VMCS12_HOST_IA32_SYSENTER_CS),
                     vmcs12_read64(vmcs12, VMCS12_HOST_IA32_SYSENTER_ESP),
                     vmcs12_read64(vmcs12, VMCS12_HOST_IA32_SYSENTER_EIP));
    } else {
        /*
         * CPU is in L1. Show L2 state from vmcs12 GUEST area -- this
         * is the state L1 configured for L2 via VMWRITE.
         */
        qemu_fprintf(f, "\nL2 state (from vmcs12 GUEST area):\n");
        qemu_fprintf(f, "RIP=%016" PRIx64 " RSP=%016" PRIx64
                     " RFL=%016" PRIx64 "\n",
                     vmcs12_read64(vmcs12, VMCS12_GUEST_RIP),
                     vmcs12_read64(vmcs12, VMCS12_GUEST_RSP),
                     vmcs12_read64(vmcs12, VMCS12_GUEST_RFLAGS));
        qemu_fprintf(f, "CR0=%016" PRIx64 " CR3=%016" PRIx64
                     " CR4=%016" PRIx64 "\n",
                     vmcs12_read64(vmcs12, VMCS12_GUEST_CR0),
                     vmcs12_read64(vmcs12, VMCS12_GUEST_CR3),
                     vmcs12_read64(vmcs12, VMCS12_GUEST_CR4));
        qemu_fprintf(f, "DR7=%016" PRIx64 " EFER=%016" PRIx64
                     " PAT=%016" PRIx64 "\n",
                     vmcs12_read64(vmcs12, VMCS12_GUEST_DR7),
                     vmcs12_read64(vmcs12, VMCS12_GUEST_IA32_EFER),
                     vmcs12_read64(vmcs12, VMCS12_GUEST_IA32_PAT));

        sel[0] = vmcs12_read16(vmcs12, VMCS12_GUEST_ES_SELECTOR);
        sel[1] = vmcs12_read16(vmcs12, VMCS12_GUEST_CS_SELECTOR);
        sel[2] = vmcs12_read16(vmcs12, VMCS12_GUEST_SS_SELECTOR);
        sel[3] = vmcs12_read16(vmcs12, VMCS12_GUEST_DS_SELECTOR);
        sel[4] = vmcs12_read16(vmcs12, VMCS12_GUEST_FS_SELECTOR);
        sel[5] = vmcs12_read16(vmcs12, VMCS12_GUEST_GS_SELECTOR);
        base[0] = vmcs12_read64(vmcs12, VMCS12_GUEST_ES_BASE);
        base[1] = vmcs12_read64(vmcs12, VMCS12_GUEST_CS_BASE);
        base[2] = vmcs12_read64(vmcs12, VMCS12_GUEST_SS_BASE);
        base[3] = vmcs12_read64(vmcs12, VMCS12_GUEST_DS_BASE);
        base[4] = vmcs12_read64(vmcs12, VMCS12_GUEST_FS_BASE);
        base[5] = vmcs12_read64(vmcs12, VMCS12_GUEST_GS_BASE);
        limit[0] = vmcs12_read32(vmcs12, VMCS12_GUEST_ES_LIMIT);
        limit[1] = vmcs12_read32(vmcs12, VMCS12_GUEST_CS_LIMIT);
        limit[2] = vmcs12_read32(vmcs12, VMCS12_GUEST_SS_LIMIT);
        limit[3] = vmcs12_read32(vmcs12, VMCS12_GUEST_DS_LIMIT);
        limit[4] = vmcs12_read32(vmcs12, VMCS12_GUEST_FS_LIMIT);
        limit[5] = vmcs12_read32(vmcs12, VMCS12_GUEST_GS_LIMIT);
        ar[0] = vmcs12_read32(vmcs12, VMCS12_GUEST_ES_AR_BYTES);
        ar[1] = vmcs12_read32(vmcs12, VMCS12_GUEST_CS_AR_BYTES);
        ar[2] = vmcs12_read32(vmcs12, VMCS12_GUEST_SS_AR_BYTES);
        ar[3] = vmcs12_read32(vmcs12, VMCS12_GUEST_DS_AR_BYTES);
        ar[4] = vmcs12_read32(vmcs12, VMCS12_GUEST_FS_AR_BYTES);
        ar[5] = vmcs12_read32(vmcs12, VMCS12_GUEST_GS_AR_BYTES);
        for (i = 0; i < 6; i++) {
            qemu_fprintf(f, "%-4s sel=%04x base=%016" PRIx64
                         " limit=%08x ar=%08x\n",
                         seg_name[i], sel[i], base[i], limit[i], ar[i]);
        }
        qemu_fprintf(f, "LDT  sel=%04x base=%016" PRIx64
                     " limit=%08x ar=%08x\n",
                     vmcs12_read16(vmcs12, VMCS12_GUEST_LDTR_SELECTOR),
                     vmcs12_read64(vmcs12, VMCS12_GUEST_LDTR_BASE),
                     vmcs12_read32(vmcs12, VMCS12_GUEST_LDTR_LIMIT),
                     vmcs12_read32(vmcs12, VMCS12_GUEST_LDTR_AR_BYTES));
        qemu_fprintf(f, "TR   sel=%04x base=%016" PRIx64
                     " limit=%08x ar=%08x\n",
                     vmcs12_read16(vmcs12, VMCS12_GUEST_TR_SELECTOR),
                     vmcs12_read64(vmcs12, VMCS12_GUEST_TR_BASE),
                     vmcs12_read32(vmcs12, VMCS12_GUEST_TR_LIMIT),
                     vmcs12_read32(vmcs12, VMCS12_GUEST_TR_AR_BYTES));

        qemu_fprintf(f, "GDT=     %016" PRIx64 " %08x\n",
                     vmcs12_read64(vmcs12, VMCS12_GUEST_GDTR_BASE),
                     vmcs12_read32(vmcs12, VMCS12_GUEST_GDTR_LIMIT));
        qemu_fprintf(f, "IDT=     %016" PRIx64 " %08x\n",
                     vmcs12_read64(vmcs12, VMCS12_GUEST_IDTR_BASE),
                     vmcs12_read32(vmcs12, VMCS12_GUEST_IDTR_LIMIT));
        qemu_fprintf(f, "SYSENTER cs=%08x esp=%016" PRIx64
                     " eip=%016" PRIx64 "\n",
                     vmcs12_read32(vmcs12, VMCS12_GUEST_SYSENTER_CS),
                     vmcs12_read64(vmcs12, VMCS12_GUEST_SYSENTER_ESP),
                     vmcs12_read64(vmcs12, VMCS12_GUEST_SYSENTER_EIP));
    }
}
#endif /* CONFIG_KVM */

void x86_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    int eflags, i, nb;
    static const char *seg_name[6] = { "ES", "CS", "SS", "DS", "FS", "GS" };

    eflags = cpu_compute_eflags(env);
#ifdef TARGET_X86_64
    if (env->hflags & HF_CS64_MASK) {
        qemu_fprintf(f, "RAX=%016" PRIx64 " RBX=%016" PRIx64 " RCX=%016" PRIx64 " RDX=%016" PRIx64 "\n"
                     "RSI=%016" PRIx64 " RDI=%016" PRIx64 " RBP=%016" PRIx64 " RSP=%016" PRIx64 "\n"
                     "R8 =%016" PRIx64 " R9 =%016" PRIx64 " R10=%016" PRIx64 " R11=%016" PRIx64 "\n"
                     "R12=%016" PRIx64 " R13=%016" PRIx64 " R14=%016" PRIx64 " R15=%016" PRIx64 "\n",
                     env->regs[R_EAX],
                     env->regs[R_EBX],
                     env->regs[R_ECX],
                     env->regs[R_EDX],
                     env->regs[R_ESI],
                     env->regs[R_EDI],
                     env->regs[R_EBP],
                     env->regs[R_ESP],
                     env->regs[8],
                     env->regs[9],
                     env->regs[10],
                     env->regs[11],
                     env->regs[12],
                     env->regs[13],
                     env->regs[14],
                     env->regs[15]);

        if (env->features[FEAT_7_1_EDX] & CPUID_7_1_EDX_APXF) {
            qemu_fprintf(f, "R16=%016" PRIx64 " R17=%016" PRIx64 " R18=%016" PRIx64 " R19=%016" PRIx64 "\n"
                         "R20=%016" PRIx64 " R21=%016" PRIx64 " R22=%016" PRIx64 " R23=%016" PRIx64 "\n"
                         "R24=%016" PRIx64 " R25=%016" PRIx64 " R26=%016" PRIx64 " R27=%016" PRIx64 "\n"
                         "R28=%016" PRIx64 " R29=%016" PRIx64 " R30=%016" PRIx64 " R31=%016" PRIx64 "\n",
                         env->regs[16],
                         env->regs[17],
                         env->regs[18],
                         env->regs[19],
                         env->regs[20],
                         env->regs[21],
                         env->regs[22],
                         env->regs[23],
                         env->regs[24],
                         env->regs[25],
                         env->regs[26],
                         env->regs[27],
                         env->regs[28],
                         env->regs[29],
                         env->regs[30],
                         env->regs[31]);
        }

        qemu_fprintf(f, "RIP=%016" PRIx64 " RFL=%08x [%c%c%c%c%c%c%c] CPL=%d II=%d A20=%d SMM=%d HLT=%d\n",
                     env->eip, eflags,
                     eflags & DF_MASK ? 'D' : '-',
                     eflags & CC_O ? 'O' : '-',
                     eflags & CC_S ? 'S' : '-',
                     eflags & CC_Z ? 'Z' : '-',
                     eflags & CC_A ? 'A' : '-',
                     eflags & CC_P ? 'P' : '-',
                     eflags & CC_C ? 'C' : '-',
                     env->hflags & HF_CPL_MASK,
                     (env->hflags >> HF_INHIBIT_IRQ_SHIFT) & 1,
                     (env->a20_mask >> 20) & 1,
                     (env->hflags >> HF_SMM_SHIFT) & 1,
                     cs->halted);
    } else
#endif
    {
        qemu_fprintf(f, "EAX=%08x EBX=%08x ECX=%08x EDX=%08x\n"
                     "ESI=%08x EDI=%08x EBP=%08x ESP=%08x\n"
                     "EIP=%08x EFL=%08x [%c%c%c%c%c%c%c] CPL=%d II=%d A20=%d SMM=%d HLT=%d\n",
                     (uint32_t)env->regs[R_EAX],
                     (uint32_t)env->regs[R_EBX],
                     (uint32_t)env->regs[R_ECX],
                     (uint32_t)env->regs[R_EDX],
                     (uint32_t)env->regs[R_ESI],
                     (uint32_t)env->regs[R_EDI],
                     (uint32_t)env->regs[R_EBP],
                     (uint32_t)env->regs[R_ESP],
                     (uint32_t)env->eip, eflags,
                     eflags & DF_MASK ? 'D' : '-',
                     eflags & CC_O ? 'O' : '-',
                     eflags & CC_S ? 'S' : '-',
                     eflags & CC_Z ? 'Z' : '-',
                     eflags & CC_A ? 'A' : '-',
                     eflags & CC_P ? 'P' : '-',
                     eflags & CC_C ? 'C' : '-',
                     env->hflags & HF_CPL_MASK,
                     (env->hflags >> HF_INHIBIT_IRQ_SHIFT) & 1,
                     (env->a20_mask >> 20) & 1,
                     (env->hflags >> HF_SMM_SHIFT) & 1,
                     cs->halted);
    }

    for(i = 0; i < 6; i++) {
        cpu_x86_dump_seg_cache(env, f, seg_name[i], &env->segs[i]);
    }
    cpu_x86_dump_seg_cache(env, f, "LDT", &env->ldt);
    cpu_x86_dump_seg_cache(env, f, "TR", &env->tr);

#ifdef TARGET_X86_64
    if (env->hflags & HF_LMA_MASK) {
        qemu_fprintf(f, "GDT=     %016" PRIx64 " %08x\n",
                     env->gdt.base, env->gdt.limit);
        qemu_fprintf(f, "IDT=     %016" PRIx64 " %08x\n",
                     env->idt.base, env->idt.limit);
        qemu_fprintf(f, "CR0=%08x CR2=%016" PRIx64 " CR3=%016" PRIx64 " CR4=%08x\n",
                     (uint32_t)env->cr[0],
                     env->cr[2],
                     env->cr[3],
                     (uint32_t)env->cr[4]);
        for(i = 0; i < 4; i++)
            qemu_fprintf(f, "DR%d=%016" PRIx64 " ", i, env->dr[i]);
        qemu_fprintf(f, "\nDR6=%016" PRIx64 " DR7=%016" PRIx64 "\n",
                     env->dr[6], env->dr[7]);
    } else
#endif
    {
        qemu_fprintf(f, "GDT=     %08x %08x\n",
                     (uint32_t)env->gdt.base, env->gdt.limit);
        qemu_fprintf(f, "IDT=     %08x %08x\n",
                     (uint32_t)env->idt.base, env->idt.limit);
        qemu_fprintf(f, "CR0=%08x CR2=%08x CR3=%08x CR4=%08x\n",
                     (uint32_t)env->cr[0],
                     (uint32_t)env->cr[2],
                     (uint32_t)env->cr[3],
                     (uint32_t)env->cr[4]);
        for(i = 0; i < 4; i++) {
            qemu_fprintf(f, "DR%d=" TARGET_FMT_lx " ", i, env->dr[i]);
        }
        qemu_fprintf(f, "\nDR6=" TARGET_FMT_lx " DR7=" TARGET_FMT_lx "\n",
                     env->dr[6], env->dr[7]);
    }
    if (flags & CPU_DUMP_CCOP) {
        const char *cc_op_name = NULL;
        char cc_op_buf[32];

        if ((unsigned)env->cc_op < ARRAY_SIZE(cc_op_str)) {
            cc_op_name = cc_op_str[env->cc_op];
        }
        if (cc_op_name == NULL) {
            snprintf(cc_op_buf, sizeof(cc_op_buf), "[%d]", env->cc_op);
            cc_op_name = cc_op_buf;
        }
#ifdef TARGET_X86_64
        if (env->hflags & HF_CS64_MASK) {
            qemu_fprintf(f, "CCS=%016" PRIx64 " CCD=%016" PRIx64 " CCO=%s\n",
                         env->cc_src, env->cc_dst,
                         cc_op_name);
        } else
#endif
        {
            qemu_fprintf(f, "CCS=%08x CCD=%08x CCO=%s\n",
                         (uint32_t)env->cc_src, (uint32_t)env->cc_dst,
                         cc_op_name);
        }
    }
    qemu_fprintf(f, "EFER=%016" PRIx64 "\n", env->efer);
    if (flags & CPU_DUMP_FPU) {
        int fptag;
        const uint64_t avx512_mask = XSTATE_OPMASK_MASK | \
                                     XSTATE_ZMM_Hi256_MASK | \
                                     XSTATE_Hi16_ZMM_MASK | \
                                     XSTATE_YMM_MASK | XSTATE_SSE_MASK,
                       avx_mask = XSTATE_YMM_MASK | XSTATE_SSE_MASK;
        fptag = 0;
        for(i = 0; i < 8; i++) {
            fptag |= ((!env->fptags[i]) << i);
        }
        update_mxcsr_from_sse_status(env);
        qemu_fprintf(f, "FCW=%04x FSW=%04x [ST=%d] FTW=%02x MXCSR=%08x\n",
                     env->fpuc,
                     (env->fpus & ~0x3800) | (env->fpstt & 0x7) << 11,
                     env->fpstt,
                     fptag,
                     env->mxcsr);
        for(i=0;i<8;i++) {
            CPU_LDoubleU u;
            u.d = env->fpregs[i].d;
            qemu_fprintf(f, "FPR%d=%016" PRIx64 " %04x",
                         i, u.l.lower, u.l.upper);
            if ((i & 1) == 1)
                qemu_fprintf(f, "\n");
            else
                qemu_fprintf(f, " ");
        }

        if ((env->xcr0 & avx512_mask) == avx512_mask) {
            /* XSAVE enabled AVX512 */
            for (i = 0; i < NB_OPMASK_REGS; i++) {
                qemu_fprintf(f, "Opmask%02d=%016"PRIx64"%s", i,
                             env->opmask_regs[i], ((i & 3) == 3) ? "\n" : " ");
            }

            nb = (env->hflags & HF_CS64_MASK) ? 32 : 8;
            for (i = 0; i < nb; i++) {
                qemu_fprintf(f, "ZMM%02d=%016"PRIx64" %016"PRIx64" %016"PRIx64
                             " %016"PRIx64" %016"PRIx64" %016"PRIx64
                             " %016"PRIx64" %016"PRIx64"\n",
                             i,
                             env->xmm_regs[i].ZMM_Q(7),
                             env->xmm_regs[i].ZMM_Q(6),
                             env->xmm_regs[i].ZMM_Q(5),
                             env->xmm_regs[i].ZMM_Q(4),
                             env->xmm_regs[i].ZMM_Q(3),
                             env->xmm_regs[i].ZMM_Q(2),
                             env->xmm_regs[i].ZMM_Q(1),
                             env->xmm_regs[i].ZMM_Q(0));
            }
        } else if ((env->xcr0 & avx_mask)  == avx_mask) {
            /* XSAVE enabled AVX */
            nb = env->hflags & HF_CS64_MASK ? 16 : 8;
            for (i = 0; i < nb; i++) {
                qemu_fprintf(f, "YMM%02d=%016"PRIx64" %016"PRIx64" %016"PRIx64
                             " %016"PRIx64"\n", i,
                             env->xmm_regs[i].ZMM_Q(3),
                             env->xmm_regs[i].ZMM_Q(2),
                             env->xmm_regs[i].ZMM_Q(1),
                             env->xmm_regs[i].ZMM_Q(0));
            }
        } else { /* SSE and below cases */
            nb = env->hflags & HF_CS64_MASK ? 16 : 8;
            for (i = 0; i < nb; i++) {
                qemu_fprintf(f, "XMM%02d=%016"PRIx64" %016"PRIx64"%s",
                             i,
                             env->xmm_regs[i].ZMM_Q(1),
                             env->xmm_regs[i].ZMM_Q(0),
                             (i & 1) ? "\n" : " ");
            }
        }
    }
    if (flags & CPU_DUMP_CODE) {
        target_ulong base = env->segs[R_CS].base + env->eip;
        target_ulong offs = MIN(env->eip, DUMP_CODE_BYTES_BACKWARD);
        uint8_t code;
        char codestr[3];

        qemu_fprintf(f, "Code=");
        for (i = 0; i < DUMP_CODE_BYTES_TOTAL; i++) {
            if (cpu_memory_rw_debug(cs, base - offs + i, &code, 1, 0) == 0) {
                snprintf(codestr, sizeof(codestr), "%02x", code);
            } else {
                snprintf(codestr, sizeof(codestr), "??");
            }
            qemu_fprintf(f, "%s%s%s%s", i > 0 ? " " : "",
                         i == offs ? "<" : "", codestr, i == offs ? ">" : "");
        }
        qemu_fprintf(f, "\n");
    }

#ifdef CONFIG_KVM
    /* Show nested peer state from vmcs12 when applicable */
    dump_vmcs12_nested_state(env, f, seg_name);
#endif
}
