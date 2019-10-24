#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/uuid.h"
#include "sysemu/sysemu.h"
#include "exec/cpu-common.h"
#include "exec/gdbstub.h"
#include "sysemu/replay.h"
#include "disas/disas.h"
#include "sysemu/runstate.h"

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
