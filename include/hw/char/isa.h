#ifndef HW_CHAR_ISA_H
#define HW_CHAR_ISA_H

#include "qemu-common.h"
#include "hw/char/serial.h"
#include "hw/isa/isa.h"

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

typedef struct ISAParallelState {
    ISADevice parent_obj;

    uint32_t index;
    uint32_t iobase;
    uint32_t isairq;
    ParallelState state;
} ISAParallelState;

#define TYPE_ISA_PARALLEL "isa-parallel"
#define ISA_PARALLEL(obj) \
    OBJECT_CHECK(ISAParallelState, (obj), TYPE_ISA_PARALLEL)

typedef struct ISASerialState {
    ISADevice parent_obj;

    uint32_t index;
    uint32_t iobase;
    uint32_t isairq;
    SerialState state;
} ISASerialState;

#define TYPE_ISA_SERIAL "isa-serial"
#define ISA_SERIAL(obj) OBJECT_CHECK(ISASerialState, (obj), TYPE_ISA_SERIAL)

#endif
