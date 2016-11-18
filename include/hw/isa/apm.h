#ifndef APM_H
#define APM_H

#include "qemu-common.h"
#include "hw/hw.h"
#include "exec/memory.h"

typedef void (*apm_reg_changed_t)(uint32_t val, void *arg);

typedef struct APMState {
    uint8_t apmc;
    uint8_t apms;

    apm_reg_changed_t cnt_callback;
    apm_reg_changed_t sts_callback;
    void *arg;
    MemoryRegion io;
} APMState;

void apm_init(PCIDevice *dev, APMState *s, apm_reg_changed_t cnt_callback,
              apm_reg_changed_t sts_callback, void *arg);

extern const VMStateDescription vmstate_apm;

#endif /* APM_H */
