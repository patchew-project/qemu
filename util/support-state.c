#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/support-state.h"

void qemu_warn_support_state(const char *type, const char *name,
                             QemuSupportState *state)
{
    warn_report("%s %s is %s%s%s%s", type, name,
                SupportState_str(state->state),
                state->reason ? " ("          : "",
                state->reason ? state->reason : "",
                state->reason ? ")"           : "");
}

bool qemu_is_deprecated(QemuSupportState *state)
{
    return state->state == SUPPORT_STATE_DEPRECATED;
}

bool qemu_is_obsolete(QemuSupportState *state)
{
    return state->state == SUPPORT_STATE_OBSOLETE;
}
