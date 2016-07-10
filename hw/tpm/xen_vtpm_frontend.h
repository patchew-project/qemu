#ifndef TPM_XEN_VTPM_FRONTEND_H
#define TPM_XEN_VTPM_FRONTEND_H 1

struct XenDevice;
extern int xenstore_vtpm_dev;
/* Xen vtpm */
int vtpm_send(struct XenDevice *xendev, uint8_t* buf, size_t count);
int vtpm_recv(struct XenDevice *xendev, uint8_t* buf, size_t *count);

#endif /* TPM_XEN_VTPM_FRONTEND_H */
