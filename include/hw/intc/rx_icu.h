#ifndef RX_ICU_H
#define RX_ICU_H

#include "qemu-common.h"
#include "hw/irq.h"

struct IRQSource {
    int sense;
    int level;
};

struct RXICUState {
    SysBusDevice parent_obj;

    MemoryRegion memory;
    struct IRQSource src[256];
    char *icutype;
    uint32_t nr_irqs;
    uint32_t *map;
    uint32_t nr_sense;
    uint32_t *init_sense;

    uint8_t ir[256];
    uint8_t dtcer[256];
    uint8_t ier[32];
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

#define TYPE_RXICU "rxicu"
#define RXICU(obj) OBJECT_CHECK(RXICUState, (obj), TYPE_RXICU)

#define SWI 27
#define TRG_LEVEL 0
#define TRG_NEDGE 1
#define TRG_PEDGE 2
#define TRG_BEDGE 3

#endif /* RX_ICU_H */
