#ifndef QEMU_VHOST_PCI_SLAVE_H
#define QEMU_VHOST_PCI_SLAVE_H

#include "linux-headers/linux/vhost.h"

extern int vp_slave_can_read(void *opaque);

extern void vp_slave_read(void *opaque, const uint8_t *buf, int size);

extern void vp_slave_event(void *opaque, int event);

#endif
