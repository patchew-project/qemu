#ifndef RX_ICU_H
#define RX_ICU_H

#include "qemu-common.h"
#include "hw/irq.h"

enum TRG_MODE {
    TRG_LEVEL = 0,
    TRG_NEDGE = 1,      /* Falling */
    TRG_PEDGE = 2,      /* Raising */
    TRG_BEDGE = 3,      /* Both */
};

struct IRQSource {
    enum TRG_MODE sense;
    int level;
};

enum {
    /* Software interrupt request */
    SWI = 27,
    NR_IRQS = 256,
};

struct RXICUState {
    SysBusDevice parent_obj;

    MemoryRegion memory;
    struct IRQSource src[NR_IRQS];
    char *icutype;
    uint32_t nr_irqs;
    uint32_t *map;
    uint32_t nr_sense;
    uint32_t *init_sense;

    uint8_t ir[NR_IRQS];
    uint8_t dtcer[NR_IRQS];
    uint8_t ier[NR_IRQS / 8];
    uint8_t ipr[142];
    uint8_t dmasr[4];
    uint16_t fir;
    uint8_t nmisr;
    uint8_t nmier;
    uint8_t nmiclr;
    uint8_t nmicr;
    int req_irq;
    qemu_irq _irq;
    qemu_irq _fir;
    qemu_irq _swi;
};
typedef struct RXICUState RXICUState;

#define TYPE_RXICU "rx-icu"
#define RXICU(obj) OBJECT_CHECK(RXICUState, (obj), TYPE_RXICU)

#endif /* RX_ICU_H */
