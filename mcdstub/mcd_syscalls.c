#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "semihosting/semihost.h"
#include "sysemu/runstate.h"
#include "mcdstub/syscalls.h"
#include "mcdstub.h"

typedef struct {
    char syscall_buf[256];
    /* TODO: this needs to be get fixed mcd_syscall_complete_cb */
    int current_syscall_cb;
} MCDSyscallState;

static enum {
    MCD_SYS_UNKNOWN,
    MCD_SYS_ENABLED,
    MCD_SYS_DISABLED,
} mcd_syscall_mode;

static MCDSyscallState mcdserver_syscall_state;

void mcd_syscall_reset(void)
{
    mcdserver_syscall_state.current_syscall_cb = 0;
}

void mcd_disable_syscalls(void)
{
    mcd_syscall_mode = MCD_SYS_DISABLED;
}
