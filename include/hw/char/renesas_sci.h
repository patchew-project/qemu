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
#include "hw/clock.h"

#define TYPE_RENESAS_SCI_COMMON "renesas-sci-common"
#define RSCICommon(obj) OBJECT_CHECK(RSCICommonState, (obj), \
                                     TYPE_RENESAS_SCI_COMMON)
#define TYPE_RENESAS_SCI "renesas-sci"
#define RSCI(obj) OBJECT_CHECK(RSCIState, (obj), TYPE_RENESAS_SCI)
#define TYPE_RENESAS_SCIA "renesas-scia"
#define RSCIA(obj) OBJECT_CHECK(RSCIAState, (obj), TYPE_RENESAS_SCIA)
#define TYPE_RENESAS_SCIF "renesas-scif"
#define RSCIF(obj) OBJECT_CHECK(RSCIFState, (obj), TYPE_RENESAS_SCIF)

#define SCI_GET_CLASS(obj) \
    OBJECT_GET_CLASS(RenesasSCICommonClass, obj, TYPE_RENESAS_SCI_COMMON)
#define SCI_COMMON_CLASS(klass) \
    OBJECT_CLASS_CHECK(RenesasSCICommonClass, klass, TYPE_RENESAS_SCI_COMMON)
#define SCI_CLASS(klass) \
    OBJECT_CLASS_CHECK(RenesasSCIClass, klass, TYPE_RENESAS_SCI)
#define SCIA_CLASS(klass) \
    OBJECT_CLASS_CHECK(RenesasSCIAClass, klass, TYPE_RENESAS_SCIA)
#define SCIF_CLASS(klass) \
    OBJECT_CLASS_CHECK(RenesasSCIFClass, klass, TYPE_RENESAS_SCIF)

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

typedef struct RSCICommonState {
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
        int64_t (*handler)(struct RSCICommonState *sci);
    } event[NR_SCI_EVENT];
    QEMUTimer *event_timer;
    CharBackend chr;
    uint64_t input_freq;
    qemu_irq irq[SCI_NR_IRQ];
    Fifo8 rxfifo;
    int regshift;
    uint32_t unit;
    Clock *pck;
} RSCICommonState;

typedef struct {
    RSCICommonState parent_obj;

    /* SCI specific register */
    uint8_t sptr;
} RSCIState;

typedef struct {
    RSCICommonState parent_obj;

    /* SCIa specific register */
    uint8_t scmr;
    uint8_t semr;
} RSCIAState;

typedef struct {
    RSCICommonState parent_obj;

    /* SCIF specific register */
    uint16_t fcr;
    uint16_t sptr;
    uint16_t lsr;

    /* internal use */
    uint16_t read_lsr;
    int tdcnt;
} RSCIFState;

typedef struct RenesasSCICommonClass {
    SysBusDeviceClass parent;

    const struct MemoryRegionOps *ops;
    void (*irq_fn)(RSCICommonState *sci, int request);
    int (*divrate)(RSCICommonState *sci);
} RenesasSCICommonClass;

typedef struct RenesasSCIClass {
    RenesasSCICommonClass parent;

    void (*p_irq_fn)(RSCICommonState *sci, int request);
} RenesasSCIClass;

typedef struct RenesasSCIAClass {
    RenesasSCICommonClass parent;

    void (*p_irq_fn)(RSCICommonState *sci, int request);
    int (*p_divrate)(RSCICommonState *sci);
} RenesasSCIAClass;

typedef struct RenesasSCIFClass {
    RenesasSCICommonClass parent;

    void (*p_irq_fn)(RSCICommonState *sci, int request);
} RenesasSCIFClass;
