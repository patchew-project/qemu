#ifndef HW_PARALLEL_H
#define HW_PARALLEL_H

#include "exec/ioport.h"
#include "exec/memory.h"
#include "hw/isa/isa.h"
#include "hw/irq.h"
#include "chardev/char-fe.h"
#include "chardev/char.h"

/*
 * These are the definitions for the Printer Status Register
 */
#define PARA_STS_BUSY   0x80    /* Busy complement */
#define PARA_STS_ACK    0x40    /* Acknowledge */
#define PARA_STS_PAPER  0x20    /* Out of paper */
#define PARA_STS_ONLINE 0x10    /* Online */
#define PARA_STS_ERROR  0x08    /* Error complement */
#define PARA_STS_TMOUT  0x01    /* EPP timeout */

/*
 * These are the definitions for the Printer Control Register
 */
#define PARA_CTR_DIR    0x20    /* Direction (1=read, 0=write) */
#define PARA_CTR_INTEN  0x10    /* IRQ Enable */
#define PARA_CTR_SELECT 0x08    /* Select In complement */
#define PARA_CTR_INIT   0x04    /* Initialize Printer complement */
#define PARA_CTR_AUTOLF 0x02    /* Auto linefeed complement */
#define PARA_CTR_STROBE 0x01    /* Strobe complement */

#define PARA_CTR_SIGNAL (PARA_CTR_SELECT | PARA_CTR_INIT | PARA_CTR_AUTOLF \
                         | PARA_CTR_STROBE)

typedef struct ParallelState {
    MemoryRegion iomem;
    uint8_t dataw;
    uint8_t datar;
    uint8_t status;
    uint8_t control;
    qemu_irq irq;
    int irq_pending;
    CharBackend chr;
    int hw_driver;
    int epp_timeout;
    uint32_t last_read_offset; /* For debugging */
    /* Memory-mapped interface */
    int it_shift;
    PortioList portio_list;
} ParallelState;

void parallel_hds_isa_init(ISABus *bus, int n);

bool parallel_mm_init(MemoryRegion *address_space,
                      hwaddr base, int it_shift, qemu_irq irq,
                      Chardev *chr);

#endif
