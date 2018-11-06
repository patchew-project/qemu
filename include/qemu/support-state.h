#ifndef QEMU_SUPPORT_STATE_H
#define QEMU_SUPPORT_STATE_H

#include "qapi/qapi-types-common.h"

typedef struct QemuSupportState {
    SupportState state;
    UsageHints   hints;
    const char   *help;
} QemuSupportState;

void qemu_warn_support_state(const char *type, const char *name,
                             ObjectClass *oc);

bool qemu_is_deprecated(ObjectClass *oc);
bool qemu_is_obsolete(ObjectClass *oc);

#endif /* QEMU_SUPPORT_STATE_H */
