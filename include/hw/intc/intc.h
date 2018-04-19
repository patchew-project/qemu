#ifndef INTC_H
#define INTC_H

#include "qom/object.h"

#define TYPE_INTERRUPT_STATS_PROVIDER "intctrl"

#define INTERRUPT_STATS_PROVIDER_CLASS(klass) \
    OBJECT_CLASS_CHECK(InterruptStatsProviderClass, (klass), \
                       TYPE_INTERRUPT_STATS_PROVIDER)
#define INTERRUPT_STATS_PROVIDER_GET_CLASS(obj) \
    OBJECT_GET_CLASS(InterruptStatsProviderClass, (obj), \
                     TYPE_INTERRUPT_STATS_PROVIDER)
#define INTERRUPT_STATS_PROVIDER(obj) \
    INTERFACE_CHECK(InterruptStatsProvider, (obj), \
                    TYPE_INTERRUPT_STATS_PROVIDER)

typedef struct InterruptStatsProvider {
    Object parent;
} InterruptStatsProvider;

typedef struct InterruptStatsProviderClass {
    InterfaceClass parent;

    /* The returned pointer and statistics must remain valid until
     * the BQL is next dropped.
     */
    bool (*get_statistics)(InterruptStatsProvider *obj, uint64_t **irq_counts,
                           unsigned int *nb_irqs);
    void (*print_info)(InterruptStatsProvider *obj, Monitor *mon);
} InterruptStatsProviderClass;

#define TYPE_CPU_INTC "cpu-intc"
#define CPU_INTC(obj)                                     \
    OBJECT_CHECK(CPUIntc, (obj), TYPE_CPU_INTC)
#define CPU_INTC_CLASS(klass)                                     \
    OBJECT_CLASS_CHECK(CPUIntcClass, (klass), TYPE_CPU_INTC)
#define CPU_INTC_GET_CLASS(obj)                                   \
    OBJECT_GET_CLASS(CPUIntcClass, (obj), TYPE_CPU_INTC)

typedef struct CPUIntc {
    Object parent;
} CPUIntc;

typedef struct CPUIntcClass {
    InterfaceClass parent;
    void (*connect)(CPUIntc *icp, Error **errp);
    void (*disconnect)(CPUIntc *icp, Error **errp);
} CPUIntcClass;

void cpu_intc_disconnect(CPUIntc *intc, Error **errp);
void cpu_intc_connect(CPUIntc *intc, Error **errp);

#endif
