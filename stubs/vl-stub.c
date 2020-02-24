#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/uuid.h"
#include "sysemu/sysemu.h"
#include "exec/cpu-common.h"
#include "exec/gdbstub.h"
#include "sysemu/replay.h"
#include "disas/disas.h"
#include "sysemu/runstate.h"

#include "qapi/qapi-commands-ui.h"
#include "qapi/qapi-commands-run-state.h"
#include "sysemu/watchdog.h"
#include "disas/disas.h"
#include "audio/audio.h"

bool tcg_allowed;
bool xen_allowed;
bool boot_strict;
bool qemu_uuid_set;

int mem_prealloc;
int smp_cpus;
int vga_interface_type = VGA_NONE;
int smp_cores = 1;
int smp_threads = 1;
int icount_align_option;
int boot_menu;

#pragma weak arch_type

unsigned int max_cpus;
const uint32_t arch_type;
const char *mem_path;
uint8_t qemu_extra_params_fw[2];
uint8_t *boot_splash_filedata;
size_t boot_splash_filedata_size;
struct syminfo *syminfos;

ram_addr_t ram_size;
MachineState *current_machine;
QemuUUID qemu_uuid;

int singlestep;
const char *qemu_name;
int no_shutdown;
int autostart;

int runstate_is_running(void)
{
    return 0;
}

void runstate_set(RunState new_state)
{
}

void vm_state_notify(int running, RunState state)
{
}

bool qemu_vmstop_requested(RunState *r)
{
    return false;
}

void qemu_system_debug_request(void)
{
}

char *qemu_find_file(int type, const char *name)
{
    return NULL;
}

void gdb_set_stop_cpu(CPUState *cpu)
{
}

void replay_enable_events(void)
{
}

void replay_disable_events(void)
{
}

#ifdef TARGET_I386
void x86_cpu_list(void)
{
}
#endif

void qemu_system_shutdown_request(ShutdownCause reason)
{
    qemu_debug_assert(0);
}

void qemu_system_reset_request(ShutdownCause reason)
{
    qemu_debug_assert(0);
}

void qemu_system_powerdown_request(void)
{
    qemu_debug_assert(0);
}

void qemu_exit_preconfig_request(void)
{
    qemu_debug_assert(0);
}

bool runstate_needs_reset(void)
{
    qemu_debug_assert(0);

    return FALSE;
}

bool qemu_wakeup_suspend_enabled(void)
{
    qemu_debug_assert(0);

    return FALSE;
}

void qemu_system_wakeup_request(WakeupReason reason, Error **errp)
{
    qemu_debug_assert(0);
}

DisplayOptions *qmp_query_display_options(Error **errp)
{
    qemu_debug_assert(0);

    return NULL;
}

StatusInfo *qmp_query_status(Error **errp)
{
    qemu_debug_assert(0);

    return NULL;
}

void qmp_watchdog_set_action(WatchdogAction action, Error **errp)
{
    qemu_debug_assert(0);
}

int select_watchdog_action(const char *p)
{
    qemu_debug_assert(0);

    return -1;
}

void monitor_disas(Monitor *mon, CPUState *cpu,
                   target_ulong pc, int nb_insn, int is_physical)
{
    qemu_debug_assert(0);
}

int wav_start_capture(AudioState *state, CaptureState *s, const char *path,
                      int freq, int bits, int nchannels)
{
    qemu_debug_assert(0);

    return -1;
}
