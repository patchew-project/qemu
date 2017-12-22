#ifndef HW_VFIO_H
#define HW_VFIO_H

extern int vfio_kvm_device_fd;

bool vfio_eeh_as_ok(AddressSpace *as);
int vfio_eeh_as_op(AddressSpace *as, uint32_t op);

#endif
