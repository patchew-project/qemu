/*
 *  qemu bsd user mode definition
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef QEMU_H
#define QEMU_H

#include "qemu-user-common.h"
#include "exec/exec-all.h"

#undef DEBUG_REMAP
#ifdef DEBUG_REMAP
#endif /* DEBUG_REMAP */

#include "exec/user/abitypes.h"

enum BSDType {
    target_freebsd,
    target_netbsd,
    target_openbsd,
};
extern enum BSDType bsd_type;

#include "syscall_defs.h"
#include "target_syscall.h"
#include "target_signal.h"
#include "exec/gdbstub.h"

#if defined(CONFIG_USE_NPTL)
#define THREAD __thread
#else
#define THREAD
#endif

/* This struct is used to hold certain information about the image.
 * Basically, it replicates in user space what would be certain
 * task_struct fields in the kernel
 */
struct image_info {
    abi_ulong load_addr;
    abi_ulong start_code;
    abi_ulong end_code;
    abi_ulong start_data;
    abi_ulong end_data;
    abi_ulong start_brk;
    abi_ulong brk;
    abi_ulong start_mmap;
    abi_ulong mmap;
    abi_ulong rss;
    abi_ulong start_stack;
    abi_ulong entry;
    abi_ulong code_offset;
    abi_ulong data_offset;
    int       personality;
};

#define MAX_SIGQUEUE_SIZE 1024

struct sigqueue {
    struct sigqueue *next;
    //target_siginfo_t info;
};

struct emulated_sigtable {
    int pending; /* true if signal is pending */
    struct sigqueue *first;
    struct sigqueue info; /* in order to always have memory for the
                             first signal, we put it here */
};

/* NOTE: we force a big alignment so that the stack stored after is
   aligned too */
typedef struct TaskState {
    struct TaskState *next;
    int used; /* non zero if used */
    struct image_info *info;

    struct emulated_sigtable sigtab[TARGET_NSIG];
    struct sigqueue sigqueue_table[MAX_SIGQUEUE_SIZE]; /* siginfo queue */
    struct sigqueue *first_free; /* first free siginfo queue entry */
    int signal_pending; /* non zero if a signal may be pending */

    uint8_t stack[0];
} __attribute__((aligned(16))) TaskState;

void init_task_state(TaskState *ts);
extern const char *qemu_uname_release;
extern unsigned long mmap_min_addr;

/* ??? See if we can avoid exposing so much of the loader internals.  */
/*
 * MAX_ARG_PAGES defines the number of pages allocated for arguments
 * and envelope for the new program. 32 should suffice, this gives
 * a maximum env+arg of 128kB w/4KB pages!
 */
#define MAX_ARG_PAGES 32

/*
 * This structure is used to hold the arguments that are
 * used when loading binaries.
 */
struct linux_binprm {
        char buf[128];
        void *page[MAX_ARG_PAGES];
        abi_ulong p;
        int fd;
        int e_uid, e_gid;
        int argc, envc;
        char **argv;
        char **envp;
        char * filename;        /* Name of binary */
};

void do_init_thread(struct target_pt_regs *regs, struct image_info *infop);
abi_ulong loader_build_argptr(int envc, int argc, abi_ulong sp,
                              abi_ulong stringp, int push_ptr);
int loader_exec(const char * filename, char ** argv, char ** envp,
             struct target_pt_regs * regs, struct image_info *infop);

int load_elf_binary(struct linux_binprm * bprm, struct target_pt_regs * regs,
                    struct image_info * info);
int load_flt_binary(struct linux_binprm * bprm, struct target_pt_regs * regs,
                    struct image_info * info);

abi_long memcpy_to_target(abi_ulong dest, const void *src,
                          unsigned long len);
void target_set_brk(abi_ulong new_brk);
abi_long do_brk(abi_ulong new_brk);
void syscall_init(void);
abi_long do_freebsd_syscall(void *cpu_env, int num, abi_long arg1,
                            abi_long arg2, abi_long arg3, abi_long arg4,
                            abi_long arg5, abi_long arg6, abi_long arg7,
                            abi_long arg8);
abi_long do_netbsd_syscall(void *cpu_env, int num, abi_long arg1,
                           abi_long arg2, abi_long arg3, abi_long arg4,
                           abi_long arg5, abi_long arg6);
abi_long do_openbsd_syscall(void *cpu_env, int num, abi_long arg1,
                            abi_long arg2, abi_long arg3, abi_long arg4,
                            abi_long arg5, abi_long arg6);
void gemu_log(const char *fmt, ...) GCC_FMT_ATTR(1, 2);
extern THREAD CPUState *thread_cpu;
void cpu_loop(CPUArchState *env);
char *target_strerror(int err);
int get_osversion(void);
void fork_start(void);
void fork_end(int child);

#include "qemu/log.h"

/* strace.c */
struct syscallname {
    int nr;
    const char *name;
    const char *format;
    void (*call)(const struct syscallname *,
                 abi_long, abi_long, abi_long,
                 abi_long, abi_long, abi_long);
    void (*result)(const struct syscallname *, abi_long);
};

void
print_freebsd_syscall(int num,
                      abi_long arg1, abi_long arg2, abi_long arg3,
                      abi_long arg4, abi_long arg5, abi_long arg6);
void print_freebsd_syscall_ret(int num, abi_long ret);
void
print_netbsd_syscall(int num,
                     abi_long arg1, abi_long arg2, abi_long arg3,
                     abi_long arg4, abi_long arg5, abi_long arg6);
void print_netbsd_syscall_ret(int num, abi_long ret);
void
print_openbsd_syscall(int num,
                      abi_long arg1, abi_long arg2, abi_long arg3,
                      abi_long arg4, abi_long arg5, abi_long arg6);
void print_openbsd_syscall_ret(int num, abi_long ret);
extern int do_strace;

/* signal.c */
void process_pending_signals(CPUArchState *cpu_env);
void signal_init(void);
//int queue_signal(CPUArchState *env, int sig, target_siginfo_t *info);
//void host_to_target_siginfo(target_siginfo_t *tinfo, const siginfo_t *info);
//void target_to_host_siginfo(siginfo_t *info, const target_siginfo_t *tinfo);
long do_sigreturn(CPUArchState *env);
long do_rt_sigreturn(CPUArchState *env);
abi_long do_sigaltstack(abi_ulong uss_addr, abi_ulong uoss_addr, abi_ulong sp);

/* mmap.c */
int target_mprotect(abi_ulong start, abi_ulong len, int prot);
abi_long target_mmap(abi_ulong start, abi_ulong len, int prot,
                     int flags, int fd, abi_ulong offset);
int target_munmap(abi_ulong start, abi_ulong len);
abi_long target_mremap(abi_ulong old_addr, abi_ulong old_size,
                       abi_ulong new_size, unsigned long flags,
                       abi_ulong new_addr);
int target_msync(abi_ulong start, abi_ulong len, int flags);
extern unsigned long last_brk;
#if defined(CONFIG_USE_NPTL)
void mmap_fork_start(void);
void mmap_fork_end(int child);
#endif

/* main.c */
extern unsigned long x86_stack_size;

#if defined(CONFIG_USE_NPTL)
#include <pthread.h>
#endif

#endif /* QEMU_H */
