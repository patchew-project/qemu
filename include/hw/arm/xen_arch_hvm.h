#ifndef HW_XEN_ARCH_ARM_HVM_H
#define HW_XEN_ARCH_ARM_HVM_H

#include <xen/hvm/ioreq.h>
void xen_arch_handle_ioreq(XenIOState *state, ioreq_t *req);
void xen_arch_set_memory(XenIOState *state,
                         MemoryRegionSection *section,
                         bool add);
#endif
