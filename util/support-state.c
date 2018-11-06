#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/support-state.h"
#include "qom/object.h"

void qemu_warn_support_state(const char *type, const char *name,
                             ObjectClass *oc)
{
    const char *help = oc->supported.help;

    warn_report("%s %s is %s%s%s%s", type, name,
                SupportState_str(oc->supported.state),
                help ? " (" : "",
                help ? help : "",
                help ? ")"  : "");
}

bool qemu_is_deprecated(ObjectClass *oc)
{
    return oc->supported.state == SUPPORT_STATE_DEPRECATED;
}

bool qemu_is_obsolete(ObjectClass *oc)
{
    return oc->supported.state == SUPPORT_STATE_OBSOLETE;
}
