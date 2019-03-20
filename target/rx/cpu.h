/*
 *  RX emulation definition
 *
 *  Copyright (c) 2019 Yoshinori Sato
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef RX_CPU_H
#define RX_CPU_H

#include "qemu/bitops.h"
#include "qemu-common.h"
#include "cpu-qom.h"

#define TARGET_LONG_BITS 32
#define TARGET_PAGE_BITS 12

#define CPUArchState struct CPURXState

#include "exec/cpu-defs.h"

#define TARGET_PHYS_ADDR_SPACE_BITS 32
#define TARGET_VIRT_ADDR_SPACE_BITS 32

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
#define FPSW_CAUSE   2
#define FPSW_CAUSE_V 2
#define FPSW_CAUSE_O 3
#define FPSW_CAUSE_Z 4
#define FPSW_CAUSE_U 5
#define FPSW_CAUSE_X 6
#define FPSW_CAUSE_E 7
#define FPSW_ENABLE_MASK 0x00007c00
#define FPSW_ENABLE  10
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
#define RX_PSW_OP_SHLL 3

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
    uint64_t acc;

    /* Internal use */
    uint32_t in_sleep;
    uint32_t req_irq;           /* Requested interrupt no (hard) */
    uint32_t req_ipl;           /* Requested interrupt level */
    uint32_t ack_irq;           /* execute irq */
    uint32_t ack_ipl;           /* execute ipl */
    float_status fp_status;

    /* Flag operation */
    uint32_t psw_op;
    uint32_t psw_v[3];
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
void rx_cpu_unpack_psw(CPURXState *env, int all);

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
    *flags = deposit32(*flags, PSW_PM,  1, env->psw_pm);
}

static inline int cpu_mmu_index(CPURXState *env, bool ifetch)
{
    return 0;
}

static inline uint32_t pack_psw(CPURXState *env)
{
    uint32_t psw = 0;
    psw = deposit32(psw, PSW_IPL, 4, env->psw_ipl);
    psw = deposit32(psw, PSW_PM,  1, env->psw_pm);
    psw = deposit32(psw, PSW_U,   1, env->psw_u);
    psw = deposit32(psw, PSW_I,   1, env->psw_i);
    psw = deposit32(psw, PSW_O,   1, env->psw_o >> 31);
    psw = deposit32(psw, PSW_S,   1, env->psw_s >> 31);
    psw = deposit32(psw, PSW_Z,   1, env->psw_z == 0);
    psw = deposit32(psw, PSW_C,   1, env->psw_c);
    return psw;
}

#endif /* RX_CPU_H */
