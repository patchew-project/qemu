#ifndef INTC_H
#define INTC_H

#include "qom/object.h"

#define TYPE_INTCTRL "intctrl"

#define INTCTRL_CLASS(klass) \
    OBJECT_CLASS_CHECK(IntCtrlClass, (klass), TYPE_INTCTRL)
#define INTCTRL_GET_CLASS(obj) \
    OBJECT_GET_CLASS(IntCtrlClass, (obj), TYPE_INTCTRL)
#define INTCTRL(obj) \
    INTERFACE_CHECK(IntCtrl, (obj), TYPE_INTCTRL)

typedef struct IntCtrl {
    Object parent;
} IntCtrl;

typedef struct IntCtrlClass {
    InterfaceClass parent;

    bool (*get_statistics)(IntCtrl *obj, uint64_t **irq_counts,
                           unsigned int *nb_irqs);
    void (*print_info)(IntCtrl *obj, Monitor *mon);
} IntCtrlClass;

#endif
