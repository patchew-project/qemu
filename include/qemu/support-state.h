#ifndef QEMU_SUPPORT_STATE_H
#define QEMU_SUPPORT_STATE_H

#include "qapi/qapi-types-common.h"

typedef struct QemuSupportState {
    SupportState state;
    const char *reason;
} QemuSupportState;

void qemu_warn_support_state(const char *type, const char *name,
                             QemuSupportState *state);

bool qemu_is_deprecated(QemuSupportState *state);
bool qemu_is_obsolete(QemuSupportState *state);

#endif /* QEMU_SUPPORT_STATE_H */
