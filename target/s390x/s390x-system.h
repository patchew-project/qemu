/*
 * s390x system internal definitions and helpers
 *
 * Copyright (c) 2009 Ulrich Hecht
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef S390X_SYSTEM_H
#define S390X_SYSTEM_H

typedef struct LowCore {
    /* prefix area: defined by architecture */
    uint32_t        ccw1[2];                  /* 0x000 */
    uint32_t        ccw2[4];                  /* 0x008 */
    uint8_t         pad1[0x80 - 0x18];        /* 0x018 */
    uint32_t        ext_params;               /* 0x080 */
    uint16_t        cpu_addr;                 /* 0x084 */
    uint16_t        ext_int_code;             /* 0x086 */
    uint16_t        svc_ilen;                 /* 0x088 */
    uint16_t        svc_code;                 /* 0x08a */
    uint16_t        pgm_ilen;                 /* 0x08c */
    uint16_t        pgm_code;                 /* 0x08e */
    uint32_t        data_exc_code;            /* 0x090 */
    uint16_t        mon_class_num;            /* 0x094 */
    uint16_t        per_perc_atmid;           /* 0x096 */
    uint64_t        per_address;              /* 0x098 */
    uint8_t         exc_access_id;            /* 0x0a0 */
    uint8_t         per_access_id;            /* 0x0a1 */
    uint8_t         op_access_id;             /* 0x0a2 */
    uint8_t         ar_access_id;             /* 0x0a3 */
    uint8_t         pad2[0xA8 - 0xA4];        /* 0x0a4 */
    uint64_t        trans_exc_code;           /* 0x0a8 */
    uint64_t        monitor_code;             /* 0x0b0 */
    uint16_t        subchannel_id;            /* 0x0b8 */
    uint16_t        subchannel_nr;            /* 0x0ba */
    uint32_t        io_int_parm;              /* 0x0bc */
    uint32_t        io_int_word;              /* 0x0c0 */
    uint8_t         pad3[0xc8 - 0xc4];        /* 0x0c4 */
    uint32_t        stfl_fac_list;            /* 0x0c8 */
    uint8_t         pad4[0xe8 - 0xcc];        /* 0x0cc */
    uint64_t        mcic;                     /* 0x0e8 */
    uint8_t         pad5[0xf4 - 0xf0];        /* 0x0f0 */
    uint32_t        external_damage_code;     /* 0x0f4 */
    uint64_t        failing_storage_address;  /* 0x0f8 */
    uint8_t         pad6[0x110 - 0x100];      /* 0x100 */
    uint64_t        per_breaking_event_addr;  /* 0x110 */
    uint8_t         pad7[0x120 - 0x118];      /* 0x118 */
    PSW             restart_old_psw;          /* 0x120 */
    PSW             external_old_psw;         /* 0x130 */
    PSW             svc_old_psw;              /* 0x140 */
    PSW             program_old_psw;          /* 0x150 */
    PSW             mcck_old_psw;             /* 0x160 */
    PSW             io_old_psw;               /* 0x170 */
    uint8_t         pad8[0x1a0 - 0x180];      /* 0x180 */
    PSW             restart_new_psw;          /* 0x1a0 */
    PSW             external_new_psw;         /* 0x1b0 */
    PSW             svc_new_psw;              /* 0x1c0 */
    PSW             program_new_psw;          /* 0x1d0 */
    PSW             mcck_new_psw;             /* 0x1e0 */
    PSW             io_new_psw;               /* 0x1f0 */
    uint8_t         pad13[0x11b0 - 0x200];    /* 0x200 */

    uint64_t        mcesad;                    /* 0x11B0 */

    /* 64 bit extparam used for pfault, diag 250 etc  */
    uint64_t        ext_params2;               /* 0x11B8 */

    uint8_t         pad14[0x1200 - 0x11C0];    /* 0x11C0 */

    /* System info area */

    uint64_t        floating_pt_save_area[16]; /* 0x1200 */
    uint64_t        gpregs_save_area[16];      /* 0x1280 */
    uint32_t        st_status_fixed_logout[4]; /* 0x1300 */
    uint8_t         pad15[0x1318 - 0x1310];    /* 0x1310 */
    uint32_t        prefixreg_save_area;       /* 0x1318 */
    uint32_t        fpt_creg_save_area;        /* 0x131c */
    uint8_t         pad16[0x1324 - 0x1320];    /* 0x1320 */
    uint32_t        tod_progreg_save_area;     /* 0x1324 */
    uint64_t        cpu_timer_save_area;       /* 0x1328 */
    uint64_t        clock_comp_save_area;      /* 0x1330 */
    uint8_t         pad17[0x1340 - 0x1338];    /* 0x1338 */
    uint32_t        access_regs_save_area[16]; /* 0x1340 */
    uint64_t        cregs_save_area[16];       /* 0x1380 */

    /* align to the top of the prefix area */

    uint8_t         pad18[0x2000 - 0x1400];    /* 0x1400 */
} QEMU_PACKED LowCore;
QEMU_BUILD_BUG_ON(sizeof(LowCore) != 8192);

#define MAX_ILEN 6

/* Compute the ATMID field that is stored in the per_perc_atmid lowcore
   entry when a PER exception is triggered.  */
static inline uint8_t get_per_atmid(CPUS390XState *env)
{
    return ((env->psw.mask & PSW_MASK_64) ?       (1 << 7) : 0) |
                                                  (1 << 6)      |
           ((env->psw.mask & PSW_MASK_32) ?       (1 << 5) : 0) |
           ((env->psw.mask & PSW_MASK_DAT) ?      (1 << 4) : 0) |
           ((env->psw.mask & PSW_ASC_SECONDARY) ? (1 << 3) : 0) |
           ((env->psw.mask & PSW_ASC_ACCREG) ?    (1 << 2) : 0);
}

static inline hwaddr decode_basedisp_s(CPUS390XState *env, uint32_t ipb,
                                       uint8_t *ar)
{
    hwaddr addr = 0;
    uint8_t reg;

    reg = ipb >> 28;
    if (reg > 0) {
        addr = env->regs[reg];
    }
    addr += (ipb >> 16) & 0xfff;
    if (ar) {
        *ar = reg;
    }

    return addr;
}

/* Base/displacement are at the same locations. */
#define decode_basedisp_rs decode_basedisp_s

/* arch_dump.c */
int s390_cpu_write_elf64_note(WriteCoreDumpFunction f, CPUState *cs,
                              int cpuid, DumpState *s);

/* cpu.c */
unsigned int s390_count_running_cpus(void);
void s390_cpu_halt(S390CPU *cpu);
void s390_cpu_unhalt(S390CPU *cpu);
void s390_cpu_system_init(Object *obj);
bool s390_cpu_system_realize(DeviceState *dev, Error **errp);
void s390_cpu_finalize(Object *obj);
void s390_cpu_system_class_init(CPUClass *cc);
void s390_cpu_machine_reset_cb(void *opaque);
bool s390_cpu_has_work(CPUState *cs);

/* excp_helper.c */
void s390x_cpu_debug_excp_handler(CPUState *cs);
bool s390_cpu_exec_interrupt(CPUState *cpu, int int_req);

/* helper.c */
void do_restart_interrupt(CPUS390XState *env);
void s390_cpu_recompute_watchpoints(CPUState *cs);
void s390x_tod_timer(void *opaque);
void s390x_cpu_timer(void *opaque);
void s390_handle_wait(S390CPU *cpu);
hwaddr s390_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);
hwaddr s390_cpu_get_phys_addr_debug(CPUState *cpu, vaddr addr);
#define S390_STORE_STATUS_DEF_ADDR offsetof(LowCore, floating_pt_save_area)
int s390_store_status(S390CPU *cpu, hwaddr addr, bool store_arch);
int s390_store_adtl_status(S390CPU *cpu, hwaddr addr, hwaddr len);
LowCore *cpu_map_lowcore(CPUS390XState *env);
void cpu_unmap_lowcore(LowCore *lowcore);

void cpu_inject_clock_comparator(S390CPU *cpu);
void cpu_inject_cpu_timer(S390CPU *cpu);
void cpu_inject_emergency_signal(S390CPU *cpu, uint16_t src_cpu_addr);
int cpu_inject_external_call(S390CPU *cpu, uint16_t src_cpu_addr);
bool s390_cpu_has_io_int(S390CPU *cpu);
bool s390_cpu_has_ext_int(S390CPU *cpu);
bool s390_cpu_has_mcck_int(S390CPU *cpu);
bool s390_cpu_has_int(S390CPU *cpu);
bool s390_cpu_has_restart_int(S390CPU *cpu);
bool s390_cpu_has_stop_int(S390CPU *cpu);
void cpu_inject_restart(S390CPU *cpu);
void cpu_inject_stop(S390CPU *cpu);

/* ioinst.c */
void ioinst_handle_xsch(S390CPU *cpu, uint64_t reg1, uintptr_t ra);
void ioinst_handle_csch(S390CPU *cpu, uint64_t reg1, uintptr_t ra);
void ioinst_handle_hsch(S390CPU *cpu, uint64_t reg1, uintptr_t ra);
void ioinst_handle_msch(S390CPU *cpu, uint64_t reg1, uint32_t ipb,
                        uintptr_t ra);
void ioinst_handle_ssch(S390CPU *cpu, uint64_t reg1, uint32_t ipb,
                        uintptr_t ra);
void ioinst_handle_stcrw(S390CPU *cpu, uint32_t ipb, uintptr_t ra);
void ioinst_handle_stsch(S390CPU *cpu, uint64_t reg1, uint32_t ipb,
                         uintptr_t ra);
int ioinst_handle_tsch(S390CPU *cpu, uint64_t reg1, uint32_t ipb, uintptr_t ra);
void ioinst_handle_chsc(S390CPU *cpu, uint32_t ipb, uintptr_t ra);
void ioinst_handle_schm(S390CPU *cpu, uint64_t reg1, uint64_t reg2,
                        uint32_t ipb, uintptr_t ra);
void ioinst_handle_rsch(S390CPU *cpu, uint64_t reg1, uintptr_t ra);
void ioinst_handle_rchp(S390CPU *cpu, uint64_t reg1, uintptr_t ra);
void ioinst_handle_sal(S390CPU *cpu, uint64_t reg1, uintptr_t ra);

/* mem_helper.c */
target_ulong mmu_real2abs(CPUS390XState *env, target_ulong raddr);

/* mmu_helper.c */
bool mmu_absolute_addr_valid(target_ulong addr, bool is_write);
/* Special access mode only valid for mmu_translate() */
#define MMU_S390_LRA        -1
int mmu_translate(CPUS390XState *env, target_ulong vaddr, int rw, uint64_t asc,
                  target_ulong *raddr, int *flags, uint64_t *tec);
int mmu_translate_real(CPUS390XState *env, target_ulong raddr, int rw,
                       target_ulong *addr, int *flags, uint64_t *tec);

/* misc_helper.c */
int handle_diag_288(CPUS390XState *env, uint64_t r1, uint64_t r3);
void handle_diag_308(CPUS390XState *env, uint64_t r1, uint64_t r3,
                     uintptr_t ra);

/* sigp.c */
int handle_sigp(CPUS390XState *env, uint8_t order, uint64_t r1, uint64_t r3);
void do_stop_interrupt(CPUS390XState *env);

#endif
