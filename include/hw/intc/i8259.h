#ifndef HW_I8259_H
#define HW_I8259_H

#include "qom/object.h"
#include "hw/isa/isa.h"
#include "qemu/typedefs.h"

#define TYPE_ISA_PIC "isa-pic"
OBJECT_DECLARE_SIMPLE_TYPE(ISAPICState, ISA_PIC)

struct ISAPICState {
    ISADevice parent_obj;

    qemu_irq in_irqs[ISA_NUM_IRQS];
    qemu_irq out_irqs[ISA_NUM_IRQS];
};

/* i8259.c */

extern DeviceState *isa_pic;
qemu_irq *i8259_init(ISABus *bus, qemu_irq parent_irq);
qemu_irq *kvm_i8259_init(ISABus *bus);
int pic_get_output(DeviceState *d);
int pic_read_irq(DeviceState *d);

#endif
