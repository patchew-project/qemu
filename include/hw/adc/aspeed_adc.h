#ifndef _ASPEED_ADC_H_
#define _ASPEED_ADC_H_

#include <stdint.h>

#include "hw/hw.h"
#include "hw/irq.h"
#include "hw/sysbus.h"

#define TYPE_ASPEED_ADC "aspeed.adc"
#define ASPEED_ADC(obj) OBJECT_CHECK(AspeedADCState, (obj), TYPE_ASPEED_ADC)

#define ASPEED_ADC_NR_CHANNELS 16

typedef struct AspeedADCState {
    /* <private> */
    SysBusDevice parent;

    MemoryRegion mmio;
    qemu_irq irq;

    uint32_t engine_ctrl;
    uint32_t irq_ctrl;
    uint32_t vga_detect_ctrl;
    uint32_t adc_clk_ctrl;
    uint32_t channels[ASPEED_ADC_NR_CHANNELS / 2];
    uint32_t bounds[ASPEED_ADC_NR_CHANNELS];
    uint32_t hysteresis[ASPEED_ADC_NR_CHANNELS];
    uint32_t irq_src;
    uint32_t comp_trim;
} AspeedADCState;

#endif /* _ASPEED_ADC_H_ */
