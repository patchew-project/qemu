/*
 * Renesas Serial Communication Interface
 *
 * Copyright (c) 2020 Yoshinori Sato
 *
 * This code is licensed under the GPL version 2 or later.
 *
 */

#include "chardev/char-fe.h"
#include "qemu/timer.h"
#include "qemu/fifo8.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_RENESAS_SCI_BASE "renesas-sci-base"
OBJECT_DECLARE_TYPE(RenesasSCIBaseState, RenesasSCIBaseClass,
                    RENESAS_SCI_BASE)
#define TYPE_RENESAS_SCI "renesas-sci"
OBJECT_DECLARE_TYPE(RenesasSCIState, RenesasSCIClass,
                    RENESAS_SCI)
#define TYPE_RENESAS_SCIA "renesas-scia"
OBJECT_DECLARE_TYPE(RenesasSCIAState, RenesasSCIAClass,
                    RENESAS_SCIA)
#define TYPE_RENESAS_SCIF "renesas-scif"
OBJECT_DECLARE_TYPE(RenesasSCIFState, RenesasSCIFClass,
                    RENESAS_SCIF)

enum {
    ERI = 0,
    RXI = 1,
    TXI = 2,
    BRI_TEI = 3,
    SCI_NR_IRQ = 4,
};

enum {
    RXTOUT,
    RXNEXT,
    TXEMPTY,
    TXEND,
    NR_SCI_EVENT,
};

enum {
    SCI_REGSIZE_8 = 0,
    SCI_REGSIZE_16 = 1,
    SCI_REGSIZE_32 = 2,
};

typedef struct RenesasSCIBaseState {
    /*< private >*/
    SysBusDevice parent_obj;
    MemoryRegion memory;
    MemoryRegion memory_p4;
    MemoryRegion memory_a7;

    /* SCI register */
    uint8_t smr;
    uint8_t brr;
    uint8_t scr;
    uint8_t tdr;
    uint16_t Xsr;

    /* internal use */
    uint16_t read_Xsr;
    int64_t etu;
    int64_t trtime;
    int64_t tx_start_time;
    struct {
        int64_t time;
        int64_t (*handler)(struct RenesasSCIBaseState *sci);
    } event[NR_SCI_EVENT];
    QEMUTimer *event_timer;
    CharBackend chr;
    uint64_t input_freq;
    qemu_irq irq[SCI_NR_IRQ];
    Fifo8 rxfifo;
    int regshift;
    uint32_t unit;
    Clock *pck;
} RenesasSCIBaseState;

struct RenesasSCIState {
    RenesasSCIBaseState parent_obj;

    /* SCI specific register */
    uint8_t sptr;
};

struct RenesasSCIAState {
    RenesasSCIBaseState parent_obj;

    /* SCIa specific register */
    uint8_t scmr;
    uint8_t semr;
};

struct RenesasSCIFState {
    RenesasSCIBaseState parent_obj;

    /* SCIF specific register */
    uint16_t fcr;
    uint16_t sptr;
    uint16_t lsr;

    /* private */
    uint16_t read_lsr;
    int tdcnt;
};

typedef struct RenesasSCIBaseClass {
    SysBusDeviceClass parent;

    const struct MemoryRegionOps *ops;
    void (*irq_fn)(struct RenesasSCIBaseState *sci, int request);
    int (*divrate)(struct RenesasSCIBaseState *sci);
} RenesasSCIBaseClass;

typedef struct RenesasSCIClass {
    RenesasSCIBaseClass parent;

    void (*p_irq_fn)(struct RenesasSCIBaseState *sci, int request);
} RenesasSCIClass;

typedef struct RenesasSCIAClass {
    RenesasSCIBaseClass parent;

    void (*p_irq_fn)(struct RenesasSCIBaseState *sci, int request);
    int (*p_divrate)(struct RenesasSCIBaseState *sci);
} RenesasSCIAClass;

typedef struct RenesasSCIFClass {
    RenesasSCIBaseClass parent;

    void (*p_irq_fn)(struct RenesasSCIBaseState *sci, int request);
} RenesasSCIFClass;
