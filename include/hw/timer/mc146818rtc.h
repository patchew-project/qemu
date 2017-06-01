#ifndef MC146818RTC_H
#define MC146818RTC_H

#include "hw/isa/isa.h"
#include "hw/timer/mc146818rtc_regs.h"

#define TYPE_MC146818_RTC "mc146818rtc"

ISADevice *rtc_init(ISABus *bus, int base_year, qemu_irq intercept_irq);
void rtc_set_memory(ISADevice *dev, int addr, int val);
int rtc_get_memory(ISADevice *dev, int addr);

static inline uint32_t periodic_period_to_clock(int period_code)
{
    if (!period_code) {
        return 0;
   }

    if (period_code <= 2) {
        period_code += 7;
    }
    /* period in 32 Khz cycles */
   return 1 << (period_code - 1);
}

#define RTC_CLOCK_RATE            32768

static inline int64_t periodic_clock_to_ns(int64_t clocks)
{
    return muldiv64(clocks, NANOSECONDS_PER_SECOND, RTC_CLOCK_RATE);
}
#endif /* MC146818RTC_H */
