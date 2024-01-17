#ifndef QEMU_SYSEMU_RESET_H
#define QEMU_SYSEMU_RESET_H

#include "qapi/qapi-events-run-state.h"

typedef void QEMUResetHandler(void *opaque);

#define  QEMU_RESET_STAGES_N  2

void qemu_register_reset(QEMUResetHandler *func, void *opaque);
void qemu_register_reset_one(QEMUResetHandler *func, void *opaque,
                             bool skip_snap, int stage);
void qemu_register_reset_nosnapshotload(QEMUResetHandler *func, void *opaque);
void qemu_unregister_reset(QEMUResetHandler *func, void *opaque);
void qemu_unregister_reset_one(QEMUResetHandler *func, void *opaque, int stage);
void qemu_devices_reset(ShutdownCause reason);

#endif
