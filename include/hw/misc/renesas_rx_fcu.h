/*
 * Renesas RX Flash Control Unit (FCU) with FACI command interface
 *
 * Datasheet: RX65N Group, RX651 Group User's Manual: Hardware
 *            (Rev.1.00 R01UH0590EJ0100), section 6 (Flash Memory)
 *
 * Copyright (c) 2026 Umar, Sayyad Mahammad
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef HW_MISC_RENESAS_RX_FCU_H
#define HW_MISC_RENESAS_RX_FCU_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_RENESAS_RX_FCU "renesas-rx-fcu"
#include "system/block-backend.h"

typedef struct RenesasRxFcuState RenesasRxFcuState;
DECLARE_INSTANCE_CHECKER(RenesasRxFcuState, RENESAS_RX_FCU, TYPE_RENESAS_RX_FCU)

/* The FACI control register block (0x007FE000) covers the first 4 KiB. */
#define RX_FCU_REGS_SIZE    0x1000

/*
 * FACI command-issuing area: every command byte and data word is written
 * here, and the destination inside the flash comes from FSADDR.
 */
#define RX_FCU_FACI_ISSUE_BASE  0x007E0000
#define RX_FCU_FACI_ISSUE_SIZE  4

/*
 * Option-setting memory (OFSM). It sits apart from the code flash array and
 * holds, among other things, the two registers that select the code flash
 * bank layout.
 */
#define RX_FCU_OFSM_SIZE    0x80
#define RX_OFSM_MDE         0x00    /* Endian Select Register  (BANKMD) */
#define RX_OFSM_BANKSEL     0x20    /* Bank Select Register    (BANKSWP) */

/*
 * MDE.BANKMD[2:0] picks the code flash layout: 111b is linear mode, 000b is
 * dual mode. A blank part reads 0xffffffff, hence linear.
 */
#define RX_BANKMD_MASK      0x7
#define RX_BANKMD_DUAL      0x0
#define RX_BANKMD_LINEAR    0x7

/*
 * BANKSEL.BANKSWP[2:0] picks which bank is mapped where in dual mode. 111b
 * puts bank 0 in the upper half, which is where the vectors live, so it is
 * the layout a linearly programmed image already has; 000b swaps the two.
 */
#define RX_BANKSWP_MASK     0x7
#define RX_BANKSWP_NORMAL   0x7
#define RX_BANKSWP_SWAPPED  0x0

/* sysbus MMIO regions exposed by the device. */
enum {
    RX_FCU_MMIO_REGS = 0,   /* FACI control/status registers      */
    RX_FCU_MMIO_CFLASH,     /* code flash array (rom_device)      */
    RX_FCU_MMIO_DFLASH,     /* data flash array (rom_device)      */
    RX_FCU_MMIO_OFSM,       /* option-setting memory              */
    RX_FCU_MMIO_FACI,       /* FACI command-issuing area          */
    RX_FCU_NR_MMIO,
};

/* sysbus IRQ lines. */
enum {
    RX_FCU_IRQ_FRDYI = 0,   /* flash ready interrupt   */
    RX_FCU_IRQ_FIFERR,      /* flash access error      */
    RX_FCU_NR_IRQ,
};

/* Identifies which flash array a FACI command targets. */
typedef enum {
    RX_FCU_CFLASH = 0,
    RX_FCU_DFLASH = 1,
} RxFcuTarget;

/*
 * Per-array context handed to the flash MemoryRegion ops so a single set of
 * callbacks can serve both the code and data flash arrays.
 */
typedef struct RxFcuFlash {
    RenesasRxFcuState *fcu;
    RxFcuTarget target;
} RxFcuFlash;

/* FACI command sequencer state. */
typedef enum {
    RX_FCU_ST_READY = 0,
    RX_FCU_ST_PROGRAM_COUNT,    /* awaiting the data-word count       */
    RX_FCU_ST_PROGRAM_DATA,     /* receiving data words, then 0xD0     */
    RX_FCU_ST_ERASE,            /* awaiting the 0xD0 confirmation      */
    RX_FCU_ST_BLANKCHECK,       /* awaiting the 0xD0 confirmation      */
    RX_FCU_ST_CONFIG_COUNT,     /* awaiting the config data-word count */
    RX_FCU_ST_CONFIG_DATA,      /* receiving config words, then 0xD0   */
} RxFcuCmdState;

struct RenesasRxFcuState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion regs_mr;
    MemoryRegion cflash_mr;     /* backing store, reached through the aliases */
    MemoryRegion dflash_mr;
    MemoryRegion ofsm_mr;
    MemoryRegion faci_mr;

    /*
     * The code flash is exposed through a container so the two dual-mode
     * banks can be mapped in either order. In linear mode the container
     * holds one alias spanning the whole array; in dual mode it holds one
     * alias per half, placed according to BANKSWP. The 1.5 MiB parts have a
     * gap between the two bank windows.
     */
    MemoryRegion cflash_container;
    MemoryRegion cflash_linear;
    MemoryRegion cflash_bank[2];

    RxFcuFlash cflash_ctx;
    RxFcuFlash dflash_ctx;

    qemu_irq frdyi;
    qemu_irq fiferr;

    /* Geometry (set via properties by the MCU). */
    uint32_t cflash_size;
    uint32_t dflash_size;
    uint32_t cflash_base;       /* absolute CPU address of code flash */
    uint32_t dflash_base;       /* absolute CPU address of data flash */
    uint32_t dual_bank_gap_size;

    /* Backing storage pointers (into the rom_device RAM). */
    uint8_t *cflash_ptr;
    uint8_t *dflash_ptr;
    uint8_t *ofsm_ptr;

    /* Code flash bank layout latched at reset from the OFSM. */
    uint32_t cfg_off;           /* OFSM offset for a configuration set  */
    bool dual_mode;
    bool bank_swapped;

    /*
     * Optional block backends. When present the array is loaded from the
     * image at realize and every program or erase is written back, so flash
     * contents survive across runs the way real flash does.
     */
    BlockBackend *cflash_blk;
    BlockBackend *dflash_blk;
    BlockBackend *ofsm_blk;
    bool cflash_ro;
    bool dflash_ro;
    bool ofsm_ro;

    /* FACI registers. */
    uint16_t fentryr;           /* P/E mode entry (read-back form)    */
    uint32_t fstatr;            /* flash status                       */
    uint8_t  fastat;            /* flash access status                */
    uint8_t  frdyie;            /* flash ready interrupt enable       */
    uint8_t  faeint;            /* flash access error interrupt enable */
    uint32_t fsaddr;            /* processing start address           */
    uint32_t fpsaddr;           /* programmed/erased start address    */
    uint32_t feaddr;            /* processing end address             */
    uint8_t  fbcstat;           /* blank check status                 */
    uint16_t fbccnt;            /* blank check control                */
    uint16_t fcpsr;             /* clear/processing switch            */
    uint16_t fpckar;            /* clock notification                 */
    uint16_t fprotr;            /* protection                         */
    uint16_t fsuacr;            /* startup area control               */
    uint16_t fpestat;           /* P/E error status                   */
    uint16_t fcmdr;             /* last FACI command (read-only)      */

    /*
     * Command sequencer (RxFcuCmdState / RxFcuTarget, stored as int32 for
     * migration).
     */
    int32_t cmd_state;
    int32_t cmd_target;         /* array of the in-progress command   */
    uint32_t      prog_off;     /* next program offset within array   */
    uint32_t      prog_words;   /* remaining 16-bit words to program  */
};

void rx_fcu_update_bank_map(RenesasRxFcuState *s);

#endif /* HW_MISC_RENESAS_RX_FCU_H */
