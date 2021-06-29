#ifndef QEMU_IRQ_H
#define QEMU_IRQ_H

/* Generic IRQ/GPIO pin infrastructure.  */

#define TYPE_IRQ "irq"

/*Tracking irq define*/

#define qemu_set_irq(irq, level) \
    qemu_set_irq_with_trace(irq, level, __func__)

#define qemu_irq_raise(irq) \
    qemu_set_irq(irq, 1)

#define qemu_irq_lower(irq) \
    qemu_set_irq(irq, 0);

#define qemu_irq_pulse(irq) \
    do { \
        qemu_set_irq(irq, 1); \
        qemu_set_irq(irq, 0); \
    } while (0)

#define qemu_allocate_irqs(handler, opaque, n) \
    qemu_allocate_irqs_with_trace(handler, opaque, n, #handler)

#define qemu_allocate_irq(handler, opaque, n) \
    qemu_allocate_irq_with_trace(handler, opaque, n, #handler)

#define qemu_extend_irqs(old, n_old, handler, opaque, n) \
    qemu_extend_irqs_with_trace(old, n_old, handler, opaque, n, __func__)

/*Tracking irq define*/

/*Tracking irq function*/

void qemu_set_irq_with_trace(qemu_irq irq, int level, const char *callFunc);

/* Returns an array of N IRQs. Each IRQ is assigned the argument handler and
 * opaque data.
 */
qemu_irq *qemu_allocate_irqs_with_trace(qemu_irq_handler handler, void *opaque,
                                        int n, const char *targetFunc);

/*
 * Allocates a single IRQ. The irq is assigned with a handler, an opaque
 * data and the interrupt number.
 */
qemu_irq qemu_allocate_irq_with_trace(qemu_irq_handler handler, void *opaque,
                                      int n, const char *targetFunc);
/* Extends an Array of IRQs. Old IRQs have their handlers and opaque data
 * preserved. New IRQs are assigned the argument handler and opaque data.
 */
qemu_irq *qemu_extend_irqs_with_trace(qemu_irq *old, int n_old,
                                      qemu_irq_handler handler, void *opaque,
                                      int n, const char *targetFunc);

/*Tracking irq function*/

void qemu_free_irqs(qemu_irq *s, int n);
void qemu_free_irq(qemu_irq irq);

/* Returns a new IRQ with opposite polarity.  */
qemu_irq qemu_irq_invert(qemu_irq irq);

/* Returns a new IRQ which feeds into both the passed IRQs.
 * It's probably better to use the TYPE_SPLIT_IRQ device instead.
 */
qemu_irq qemu_irq_split(qemu_irq irq1, qemu_irq irq2);

/* For internal use in qtest.  Similar to qemu_irq_split, but operating
   on an existing vector of qemu_irq.  */
void qemu_irq_intercept_in(qemu_irq *gpio_in, qemu_irq_handler handler, int n);

/**
 * qemu_irq_is_connected: Return true if IRQ line is wired up
 *
 * If a qemu_irq has a device on the other (receiving) end of it,
 * return true; otherwise return false.
 *
 * Usually device models don't need to care whether the machine model
 * has wired up their outbound qemu_irq lines, because functions like
 * qemu_set_irq() silently do nothing if there is nothing on the other
 * end of the line. However occasionally a device model will want to
 * provide default behaviour if its output is left floating, and
 * it can use this function to identify when that is the case.
 */
static inline bool qemu_irq_is_connected(qemu_irq irq)
{
    return irq != NULL;
}

#endif
