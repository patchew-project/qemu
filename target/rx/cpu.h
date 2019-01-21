/*
 *  RX emulation
 *
 *  Copyright (c) 2019 Yoshinori Sato
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

#ifndef RX_CPU_H
#define RX_CPU_H

#include "qemu-common.h"
#include "cpu-qom.h"

#define TARGET_LONG_BITS 32
#define TARGET_PAGE_BITS 12

#define CPUArchState struct CPURXState

#include "exec/cpu-defs.h"

#define TARGET_PHYS_ADDR_SPACE_BITS 32

#define PSW_I3 27
#define PSW_I2 26
#define PSW_I1 25
#define PSW_I0 24
#define PSW_IPL PSW_I0
#define PSW_PM 20
#define PSW_U  17
#define PSW_I  16
#define PSW_O  3
#define PSW_S  2
#define PSW_Z  1
#define PSW_C  0

#define FPSW_MASK 0xfc007cff
#define FPSW_RM_MASK 0x00000003
#define FPSW_DN (1 << 8)
#define FPSW_CAUSE_MASK 0x000000fc
#define FPSW_CAUSE_SHIFT 2
#define FPSW_CAUSE_V (1 << 2)
#define FPSW_CAUSE_O (1 << 3)
#define FPSW_CAUSE_Z (1 << 4)
#define FPSW_CAUSE_U (1 << 5)
#define FPSW_CAUSE_X (1 << 6)
#define FPSW_CAUSE_E (1 << 7)
#define FPSW_ENABLE_MASK 0x00007c00
#define FPSW_ENABLE_SHIFT 10
#define FPSW_ENABLE_V (1 << 10)
#define FPSW_ENABLE_O (1 << 11)
#define FPSW_ENABLE_Z (1 << 12)
#define FPSW_ENABLE_U (1 << 13)
#define FPSW_ENABLE_X (1 << 14)
#define FPSW_FLAG_SHIFT 26
#define FPSW_FLAG_V 26
#define FPSW_FLAG_O 27
#define FPSW_FLAG_Z 28
#define FPSW_FLAG_U 29
#define FPSW_FLAG_X 30
#define FPSW_FLAG_S 31

#define NB_MMU_MODES 1
#define MMU_MODE0_SUFFIX _all

#define RX_PSW_OP_NONE 0
#define RX_PSW_OP_SUB 1
#define RX_PSW_OP_ADD 2
#define RX_PSW_OP_ABS 3
#define RX_PSW_OP_DIV 4
#define RX_PSW_OP_STRING 5
#define RX_PSW_OP_BTST 6
#define RX_PSW_OP_LOGIC 7
#define RX_PSW_OP_ROT 8
#define RX_PSW_OP_SHLL 9
#define RX_PSW_OP_SHAR 10
#define RX_PSW_OP_SHLR 11
#define RX_PSW_OP_FLOAT 12
#define RX_PSW_OP_FCMP 13

typedef struct memory_content {
    uint32_t address;
    struct memory_content *next;
} memory_content;

struct CCop;

typedef struct CPURXState {
    /* CPU registers */
    uint32_t regs[16];          /* general registers */
    uint32_t psw;               /* processor status */
    uint32_t psw_o;             /* O bit of status register */
    uint32_t psw_s;             /* S bit of status register */
    uint32_t psw_z;             /* Z bit of status register */
    uint32_t psw_c;             /* C bit of status register */
    uint32_t psw_u;
    uint32_t psw_i;
    uint32_t psw_pm;
    uint32_t psw_ipl;
    uint32_t bpsw;              /* backup status */
    uint32_t bpc;               /* backup pc */
    uint32_t isp;               /* global base register */
    uint32_t usp;               /* vector base register */
    uint32_t pc;                /* program counter */
    uint32_t intb;              /* interrupt vector */
    uint32_t fintv;
    uint32_t fpsw;
    uint32_t acc_m;
    uint32_t acc_l;

    /* Internal use */
    uint32_t in_sleep;
    uint32_t intlevel;          /* Requested interrupt level */
    uint32_t irq;               /* Requested interrupt no (hard) */
    uint32_t sirq;              /* Requested interrupt no (soft) */
    float_status fp_status;

    /* Flag operation */
    uint32_t op_a1[12];
    uint32_t op_a2[12];
    uint32_t op_r[12];
    uint32_t op_mode;
    /* Fields up to this point are cleared by a CPU reset */
    struct {} end_reset_fields;

    CPU_COMMON

    void *ack;
} CPURXState;

/*
 * RXCPU:
 * @env: #CPURXState
 *
 * A RX CPU
 */
struct RXCPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPURXState env;
};

static inline RXCPU *rx_env_get_cpu(CPURXState *env)
{
    return container_of(env, RXCPU, env);
}

#define ENV_GET_CPU(e) CPU(rx_env_get_cpu(e))

#define ENV_OFFSET offsetof(RXCPU, env)

#define RX_CPU_TYPE_SUFFIX "-" TYPE_RXCPU
#define RX_CPU_TYPE_NAME(model) model RX_CPU_TYPE_SUFFIX
#define CPU_RESOLVING_TYPE TYPE_RXCPU

void rx_cpu_do_interrupt(CPUState *cpu);
bool rx_cpu_exec_interrupt(CPUState *cpu, int int_req);
void rx_cpu_dump_state(CPUState *cpu, FILE *f,
                           fprintf_function cpu_fprintf, int flags);
int rx_cpu_gdb_read_register(CPUState *cpu, uint8_t *buf, int reg);
int rx_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);
hwaddr rx_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);

void rx_translate_init(void);
int cpu_rx_signal_handler(int host_signum, void *pinfo,
                           void *puc);

void rx_cpu_list(FILE *f, fprintf_function cpu_fprintf);
void rx_load_image(RXCPU *cpu, const char *filename,
                   uint32_t start, uint32_t size);
void rx_cpu_pack_psw(CPURXState *env);
void rx_cpu_unpack_psw(CPURXState *env, int all);
uint32_t rx_get_psw_low(CPURXState *env);

#define cpu_signal_handler cpu_rx_signal_handler
#define cpu_list rx_cpu_list

#include "exec/cpu-all.h"

#define CPU_INTERRUPT_SOFT CPU_INTERRUPT_TGT_INT_0
#define CPU_INTERRUPT_FIR  CPU_INTERRUPT_TGT_INT_1

#define RX_CPU_IRQ 0
#define RX_CPU_FIR 1

static inline void cpu_get_tb_cpu_state(CPURXState *env, target_ulong *pc,
                                        target_ulong *cs_base, uint32_t *flags)
{
    *pc = env->pc;
    *cs_base = 0;
    *flags = 0;
}

static inline int cpu_mmu_index(CPURXState *env, bool ifetch)
{
    return 0;
}

#endif /* RX_CPU_H */
