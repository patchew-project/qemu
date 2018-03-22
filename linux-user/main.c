/*
 *  qemu user main
 *
 *  Copyright (c) 2003-2008 Fabrice Bellard
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
#include "qemu/osdep.h"
#include "qemu-version.h"
#include <sys/syscall.h>
#include <sys/resource.h>

#include "qapi/error.h"
#include "qemu.h"
#include "qemu/path.h"
#include "qemu/config-file.h"
#include "qemu/cutils.h"
#include "qemu/help_option.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "tcg.h"
#include "qemu/timer.h"
#include "qemu/envlist.h"
#include "elf.h"
#include "exec/log.h"
#include "trace/control.h"
#include "target_elf.h"

char *exec_path;

int singlestep;
static const char *filename;
static const char *argv0;
static int gdbstub_port;
static envlist_t *envlist;
static const char *cpu_model;
static const char *cpu_type;
unsigned long mmap_min_addr;
unsigned long guest_base;
int have_guest_base;

#define EXCP_DUMP(env, fmt, ...)                                        \
do {                                                                    \
    CPUState *cs = ENV_GET_CPU(env);                                    \
    fprintf(stderr, fmt , ## __VA_ARGS__);                              \
    cpu_dump_state(cs, stderr, fprintf, 0);                             \
    if (qemu_log_separate()) {                                          \
        qemu_log(fmt, ## __VA_ARGS__);                                  \
        log_cpu_state(cs, 0);                                           \
    }                                                                   \
} while (0)

/*
 * When running 32-on-64 we should make sure we can fit all of the possible
 * guest address space into a contiguous chunk of virtual host memory.
 *
 * This way we will never overlap with our own libraries or binaries or stack
 * or anything else that QEMU maps.
 *
 * Many cpus reserve the high bit (or more than one for some 64-bit cpus)
 * of the address for the kernel.  Some cpus rely on this and user space
 * uses the high bit(s) for pointer tagging and the like.  For them, we
 * must preserve the expected address space.
 */
#ifndef MAX_RESERVED_VA
# if HOST_LONG_BITS > TARGET_VIRT_ADDR_SPACE_BITS
#  if TARGET_VIRT_ADDR_SPACE_BITS == 32 && \
      (TARGET_LONG_BITS == 32 || defined(TARGET_ABI32))
/* There are a number of places where we assign reserved_va to a variable
   of type abi_ulong and expect it to fit.  Avoid the last page.  */
#   define MAX_RESERVED_VA  (0xfffffffful & TARGET_PAGE_MASK)
#  else
#   define MAX_RESERVED_VA  (1ul << TARGET_VIRT_ADDR_SPACE_BITS)
#  endif
# else
#  define MAX_RESERVED_VA  0
# endif
#endif

/* That said, reserving *too* much vm space via mmap can run into problems
   with rlimits, oom due to page table creation, etc.  We will still try it,
   if directed by the command-line option, but not by default.  */
#if HOST_LONG_BITS == 64 && TARGET_VIRT_ADDR_SPACE_BITS <= 32
unsigned long reserved_va = MAX_RESERVED_VA;
#else
unsigned long reserved_va;
#endif

static void usage(int exitcode);

static const char *interp_prefix = CONFIG_QEMU_INTERP_PREFIX;
const char *qemu_uname_release;

/* XXX: on x86 MAP_GROWSDOWN only works if ESP <= address + 32, so
   we allocate a bigger stack. Need a better solution, for example
   by remapping the process stack directly at the right place */
unsigned long guest_stack_size = 8 * 1024 * 1024UL;

void gemu_log(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

#if defined(TARGET_I386)
int cpu_get_pic_interrupt(CPUX86State *env)
{
    return -1;
}
#endif

/***********************************************************/
/* Helper routines for implementing atomic operations.  */

/* Make sure everything is in a consistent state for calling fork().  */
void fork_start(void)
{
    start_exclusive();
    mmap_fork_start();
    qemu_mutex_lock(&tb_ctx.tb_lock);
    cpu_list_lock();
}

void fork_end(int child)
{
    mmap_fork_end(child);
    if (child) {
        CPUState *cpu, *next_cpu;
        /* Child processes created by fork() only have a single thread.
           Discard information about the parent threads.  */
        CPU_FOREACH_SAFE(cpu, next_cpu) {
            if (cpu != thread_cpu) {
                QTAILQ_REMOVE(&cpus, cpu, node);
            }
        }
        qemu_mutex_init(&tb_ctx.tb_lock);
        qemu_init_cpu_list();
        gdbserver_fork(thread_cpu);
        /* qemu_init_cpu_list() takes care of reinitializing the
         * exclusive state, so we don't need to end_exclusive() here.
         */
    } else {
        qemu_mutex_unlock(&tb_ctx.tb_lock);
        cpu_list_unlock();
        end_exclusive();
    }
}

#include "cpu_loop.inc.c"

__thread CPUState *thread_cpu;

bool qemu_cpu_is_self(CPUState *cpu)
{
    return thread_cpu == cpu;
}

void qemu_cpu_kick(CPUState *cpu)
{
    cpu_exit(cpu);
}

void task_settid(TaskState *ts)
{
    if (ts->ts_tid == 0) {
        ts->ts_tid = (pid_t)syscall(SYS_gettid);
    }
}

void stop_all_tasks(void)
{
    /*
     * We trust that when using NPTL, start_exclusive()
     * handles thread stopping correctly.
     */
    start_exclusive();
}

/* Assumes contents are already zeroed.  */
void init_task_state(TaskState *ts)
{
    ts->used = 1;
}

CPUArchState *cpu_copy(CPUArchState *env)
{
    CPUState *cpu = ENV_GET_CPU(env);
    CPUState *new_cpu = cpu_create(cpu_type);
    CPUArchState *new_env = new_cpu->env_ptr;
    CPUBreakpoint *bp;
    CPUWatchpoint *wp;

    /* Reset non arch specific state */
    cpu_reset(new_cpu);

    memcpy(new_env, env, sizeof(CPUArchState));

    /* Clone all break/watchpoints.
       Note: Once we support ptrace with hw-debug register access, make sure
       BP_CPU break/watchpoints are handled correctly on clone. */
    QTAILQ_INIT(&new_cpu->breakpoints);
    QTAILQ_INIT(&new_cpu->watchpoints);
    QTAILQ_FOREACH(bp, &cpu->breakpoints, entry) {
        cpu_breakpoint_insert(new_cpu, bp->pc, bp->flags, NULL);
    }
    QTAILQ_FOREACH(wp, &cpu->watchpoints, entry) {
        cpu_watchpoint_insert(new_cpu, wp->vaddr, wp->len, wp->flags, NULL);
    }

    return new_env;
}

static void handle_arg_help(const char *arg)
{
    usage(EXIT_SUCCESS);
}

static void handle_arg_log(const char *arg)
{
    int mask;

    mask = qemu_str_to_log_mask(arg);
    if (!mask) {
        qemu_print_log_usage(stdout);
        exit(EXIT_FAILURE);
    }
    qemu_log_needs_buffers();
    qemu_set_log(mask);
}

static void handle_arg_dfilter(const char *arg)
{
    qemu_set_dfilter_ranges(arg, NULL);
}

static void handle_arg_log_filename(const char *arg)
{
    qemu_set_log_filename(arg, &error_fatal);
}

static void handle_arg_set_env(const char *arg)
{
    char *r, *p, *token;
    r = p = strdup(arg);
    while ((token = strsep(&p, ",")) != NULL) {
        if (envlist_setenv(envlist, token) != 0) {
            usage(EXIT_FAILURE);
        }
    }
    free(r);
}

static void handle_arg_unset_env(const char *arg)
{
    char *r, *p, *token;
    r = p = strdup(arg);
    while ((token = strsep(&p, ",")) != NULL) {
        if (envlist_unsetenv(envlist, token) != 0) {
            usage(EXIT_FAILURE);
        }
    }
    free(r);
}

static void handle_arg_argv0(const char *arg)
{
    argv0 = strdup(arg);
}

static void handle_arg_stack_size(const char *arg)
{
    char *p;
    guest_stack_size = strtoul(arg, &p, 0);
    if (guest_stack_size == 0) {
        usage(EXIT_FAILURE);
    }

    if (*p == 'M') {
        guest_stack_size *= 1024 * 1024;
    } else if (*p == 'k' || *p == 'K') {
        guest_stack_size *= 1024;
    }
}

static void handle_arg_ld_prefix(const char *arg)
{
    interp_prefix = strdup(arg);
}

static void handle_arg_pagesize(const char *arg)
{
    qemu_host_page_size = atoi(arg);
    if (qemu_host_page_size == 0 ||
        (qemu_host_page_size & (qemu_host_page_size - 1)) != 0) {
        fprintf(stderr, "page size must be a power of two\n");
        exit(EXIT_FAILURE);
    }
}

static void handle_arg_randseed(const char *arg)
{
    unsigned long long seed;

    if (parse_uint_full(arg, &seed, 0) != 0 || seed > UINT_MAX) {
        fprintf(stderr, "Invalid seed number: %s\n", arg);
        exit(EXIT_FAILURE);
    }
    srand(seed);
}

static void handle_arg_gdb(const char *arg)
{
    gdbstub_port = atoi(arg);
}

static void handle_arg_uname(const char *arg)
{
    qemu_uname_release = strdup(arg);
}

static void handle_arg_cpu(const char *arg)
{
    cpu_model = strdup(arg);
    if (cpu_model == NULL || is_help_option(cpu_model)) {
        /* XXX: implement xxx_cpu_list for targets that still miss it */
#if defined(cpu_list)
        cpu_list(stdout, &fprintf);
#endif
        exit(EXIT_FAILURE);
    }
}

static void handle_arg_guest_base(const char *arg)
{
    guest_base = strtol(arg, NULL, 0);
    have_guest_base = 1;
}

static void handle_arg_reserved_va(const char *arg)
{
    char *p;
    int shift = 0;
    reserved_va = strtoul(arg, &p, 0);
    switch (*p) {
    case 'k':
    case 'K':
        shift = 10;
        break;
    case 'M':
        shift = 20;
        break;
    case 'G':
        shift = 30;
        break;
    }
    if (shift) {
        unsigned long unshifted = reserved_va;
        p++;
        reserved_va <<= shift;
        if (reserved_va >> shift != unshifted
            || (MAX_RESERVED_VA && reserved_va > MAX_RESERVED_VA)) {
            fprintf(stderr, "Reserved virtual address too big\n");
            exit(EXIT_FAILURE);
        }
    }
    if (*p) {
        fprintf(stderr, "Unrecognised -R size suffix '%s'\n", p);
        exit(EXIT_FAILURE);
    }
}

static void handle_arg_singlestep(const char *arg)
{
    singlestep = 1;
}

static void handle_arg_strace(const char *arg)
{
    do_strace = 1;
}

static void handle_arg_version(const char *arg)
{
    printf("qemu-" TARGET_NAME " version " QEMU_FULL_VERSION
           "\n" QEMU_COPYRIGHT "\n");
    exit(EXIT_SUCCESS);
}

static char *trace_file;
static void handle_arg_trace(const char *arg)
{
    g_free(trace_file);
    trace_file = trace_opt_parse(arg);
}

struct qemu_argument {
    const char *argv;
    const char *env;
    bool has_arg;
    void (*handle_opt)(const char *arg);
    const char *example;
    const char *help;
};

static const struct qemu_argument arg_table[] = {
    {"h",          "",                 false, handle_arg_help,
     "",           "print this help"},
    {"help",       "",                 false, handle_arg_help,
     "",           ""},
    {"g",          "QEMU_GDB",         true,  handle_arg_gdb,
     "port",       "wait gdb connection to 'port'"},
    {"L",          "QEMU_LD_PREFIX",   true,  handle_arg_ld_prefix,
     "path",       "set the elf interpreter prefix to 'path'"},
    {"s",          "QEMU_STACK_SIZE",  true,  handle_arg_stack_size,
     "size",       "set the stack size to 'size' bytes"},
    {"cpu",        "QEMU_CPU",         true,  handle_arg_cpu,
     "model",      "select CPU (-cpu help for list)"},
    {"E",          "QEMU_SET_ENV",     true,  handle_arg_set_env,
     "var=value",  "sets targets environment variable (see below)"},
    {"U",          "QEMU_UNSET_ENV",   true,  handle_arg_unset_env,
     "var",        "unsets targets environment variable (see below)"},
    {"0",          "QEMU_ARGV0",       true,  handle_arg_argv0,
     "argv0",      "forces target process argv[0] to be 'argv0'"},
    {"r",          "QEMU_UNAME",       true,  handle_arg_uname,
     "uname",      "set qemu uname release string to 'uname'"},
    {"B",          "QEMU_GUEST_BASE",  true,  handle_arg_guest_base,
     "address",    "set guest_base address to 'address'"},
    {"R",          "QEMU_RESERVED_VA", true,  handle_arg_reserved_va,
     "size",       "reserve 'size' bytes for guest virtual address space"},
    {"d",          "QEMU_LOG",         true,  handle_arg_log,
     "item[,...]", "enable logging of specified items "
     "(use '-d help' for a list of items)"},
    {"dfilter",    "QEMU_DFILTER",     true,  handle_arg_dfilter,
     "range[,...]","filter logging based on address range"},
    {"D",          "QEMU_LOG_FILENAME", true, handle_arg_log_filename,
     "logfile",     "write logs to 'logfile' (default stderr)"},
    {"p",          "QEMU_PAGESIZE",    true,  handle_arg_pagesize,
     "pagesize",   "set the host page size to 'pagesize'"},
    {"singlestep", "QEMU_SINGLESTEP",  false, handle_arg_singlestep,
     "",           "run in singlestep mode"},
    {"strace",     "QEMU_STRACE",      false, handle_arg_strace,
     "",           "log system calls"},
    {"seed",       "QEMU_RAND_SEED",   true,  handle_arg_randseed,
     "",           "Seed for pseudo-random number generator"},
    {"trace",      "QEMU_TRACE",       true,  handle_arg_trace,
     "",           "[[enable=]<pattern>][,events=<file>][,file=<file>]"},
    {"version",    "QEMU_VERSION",     false, handle_arg_version,
     "",           "display version information and exit"},
    {NULL, NULL, false, NULL, NULL, NULL}
};

static void usage(int exitcode)
{
    const struct qemu_argument *arginfo;
    int maxarglen;
    int maxenvlen;

    printf("usage: qemu-" TARGET_NAME " [options] program [arguments...]\n"
           "Linux CPU emulator (compiled for " TARGET_NAME " emulation)\n"
           "\n"
           "Options and associated environment variables:\n"
           "\n");

    /* Calculate column widths. We must always have at least enough space
     * for the column header.
     */
    maxarglen = strlen("Argument");
    maxenvlen = strlen("Env-variable");

    for (arginfo = arg_table; arginfo->handle_opt != NULL; arginfo++) {
        int arglen = strlen(arginfo->argv);
        if (arginfo->has_arg) {
            arglen += strlen(arginfo->example) + 1;
        }
        if (strlen(arginfo->env) > maxenvlen) {
            maxenvlen = strlen(arginfo->env);
        }
        if (arglen > maxarglen) {
            maxarglen = arglen;
        }
    }

    printf("%-*s %-*s Description\n", maxarglen+1, "Argument",
            maxenvlen, "Env-variable");

    for (arginfo = arg_table; arginfo->handle_opt != NULL; arginfo++) {
        if (arginfo->has_arg) {
            printf("-%s %-*s %-*s %s\n", arginfo->argv,
                   (int)(maxarglen - strlen(arginfo->argv) - 1),
                   arginfo->example, maxenvlen, arginfo->env, arginfo->help);
        } else {
            printf("-%-*s %-*s %s\n", maxarglen, arginfo->argv,
                    maxenvlen, arginfo->env,
                    arginfo->help);
        }
    }

    printf("\n"
           "Defaults:\n"
           "QEMU_LD_PREFIX  = %s\n"
           "QEMU_STACK_SIZE = %ld byte\n",
           interp_prefix,
           guest_stack_size);

    printf("\n"
           "You can use -E and -U options or the QEMU_SET_ENV and\n"
           "QEMU_UNSET_ENV environment variables to set and unset\n"
           "environment variables for the target process.\n"
           "It is possible to provide several variables by separating them\n"
           "by commas in getsubopt(3) style. Additionally it is possible to\n"
           "provide the -E and -U options multiple times.\n"
           "The following lines are equivalent:\n"
           "    -E var1=val2 -E var2=val2 -U LD_PRELOAD -U LD_DEBUG\n"
           "    -E var1=val2,var2=val2 -U LD_PRELOAD,LD_DEBUG\n"
           "    QEMU_SET_ENV=var1=val2,var2=val2 QEMU_UNSET_ENV=LD_PRELOAD,LD_DEBUG\n"
           "Note that if you provide several changes to a single variable\n"
           "the last change will stay in effect.\n"
           "\n"
           QEMU_HELP_BOTTOM "\n");

    exit(exitcode);
}

static int parse_args(int argc, char **argv)
{
    const char *r;
    int optind;
    const struct qemu_argument *arginfo;

    for (arginfo = arg_table; arginfo->handle_opt != NULL; arginfo++) {
        if (arginfo->env == NULL) {
            continue;
        }

        r = getenv(arginfo->env);
        if (r != NULL) {
            arginfo->handle_opt(r);
        }
    }

    optind = 1;
    for (;;) {
        if (optind >= argc) {
            break;
        }
        r = argv[optind];
        if (r[0] != '-') {
            break;
        }
        optind++;
        r++;
        if (!strcmp(r, "-")) {
            break;
        }
        /* Treat --foo the same as -foo.  */
        if (r[0] == '-') {
            r++;
        }

        for (arginfo = arg_table; arginfo->handle_opt != NULL; arginfo++) {
            if (!strcmp(r, arginfo->argv)) {
                if (arginfo->has_arg) {
                    if (optind >= argc) {
                        (void) fprintf(stderr,
                            "qemu: missing argument for option '%s'\n", r);
                        exit(EXIT_FAILURE);
                    }
                    arginfo->handle_opt(argv[optind]);
                    optind++;
                } else {
                    arginfo->handle_opt(NULL);
                }
                break;
            }
        }

        /* no option matched the current argv */
        if (arginfo->handle_opt == NULL) {
            (void) fprintf(stderr, "qemu: unknown option '%s'\n", r);
            exit(EXIT_FAILURE);
        }
    }

    if (optind >= argc) {
        (void) fprintf(stderr, "qemu: no user program specified\n");
        exit(EXIT_FAILURE);
    }

    filename = argv[optind];
    exec_path = argv[optind];

    return optind;
}

int main(int argc, char **argv, char **envp)
{
    struct target_pt_regs regs1, *regs = &regs1;
    struct image_info info1, *info = &info1;
    struct linux_binprm bprm;
    TaskState *ts;
    CPUArchState *env;
    CPUState *cpu;
    int optind;
    char **target_environ, **wrk;
    char **target_argv;
    int target_argc;
    int i;
    int ret;
    int execfd;

    module_call_init(MODULE_INIT_TRACE);
    qemu_init_cpu_list();
    module_call_init(MODULE_INIT_QOM);

    envlist = envlist_create();

    /* add current environment into the list */
    for (wrk = environ; *wrk != NULL; wrk++) {
        (void) envlist_setenv(envlist, *wrk);
    }

    /* Read the stack limit from the kernel.  If it's "unlimited",
       then we can do little else besides use the default.  */
    {
        struct rlimit lim;
        if (getrlimit(RLIMIT_STACK, &lim) == 0
            && lim.rlim_cur != RLIM_INFINITY
            && lim.rlim_cur == (target_long)lim.rlim_cur) {
            guest_stack_size = lim.rlim_cur;
        }
    }

    cpu_model = NULL;

    srand(time(NULL));

    qemu_add_opts(&qemu_trace_opts);

    optind = parse_args(argc, argv);

    if (!trace_init_backends()) {
        exit(1);
    }
    trace_init_file(trace_file);

    /* Zero out regs */
    memset(regs, 0, sizeof(struct target_pt_regs));

    /* Zero out image_info */
    memset(info, 0, sizeof(struct image_info));

    memset(&bprm, 0, sizeof (bprm));

    /* Scan interp_prefix dir for replacement files. */
    init_paths(interp_prefix);

    init_qemu_uname_release();

    execfd = qemu_getauxval(AT_EXECFD);
    if (execfd == 0) {
        execfd = open(filename, O_RDONLY);
        if (execfd < 0) {
            printf("Error while loading %s: %s\n", filename, strerror(errno));
            _exit(EXIT_FAILURE);
        }
    }

    if (cpu_model == NULL) {
        cpu_model = cpu_get_model(get_elf_eflags(execfd));
    }
    cpu_type = parse_cpu_model(cpu_model);

    tcg_exec_init(0);
    /* NOTE: we need to init the CPU at this stage to get
       qemu_host_page_size */

    cpu = cpu_create(cpu_type);
    env = cpu->env_ptr;
    cpu_reset(cpu);

    thread_cpu = cpu;

    if (getenv("QEMU_STRACE")) {
        do_strace = 1;
    }

    if (getenv("QEMU_RAND_SEED")) {
        handle_arg_randseed(getenv("QEMU_RAND_SEED"));
    }

    target_environ = envlist_to_environ(envlist, NULL);
    envlist_free(envlist);

    /*
     * Now that page sizes are configured in cpu_init() we can do
     * proper page alignment for guest_base.
     */
    guest_base = HOST_PAGE_ALIGN(guest_base);

    if (reserved_va || have_guest_base) {
        guest_base = init_guest_space(guest_base, reserved_va, 0,
                                      have_guest_base);
        if (guest_base == (unsigned long)-1) {
            fprintf(stderr, "Unable to reserve 0x%lx bytes of virtual address "
                    "space for use as guest address space (check your virtual "
                    "memory ulimit setting or reserve less using -R option)\n",
                    reserved_va);
            exit(EXIT_FAILURE);
        }

        if (reserved_va) {
            mmap_next_start = reserved_va;
        }
    }

    /*
     * Read in mmap_min_addr kernel parameter.  This value is used
     * When loading the ELF image to determine whether guest_base
     * is needed.  It is also used in mmap_find_vma.
     */
    {
        FILE *fp;

        if ((fp = fopen("/proc/sys/vm/mmap_min_addr", "r")) != NULL) {
            unsigned long tmp;
            if (fscanf(fp, "%lu", &tmp) == 1) {
                mmap_min_addr = tmp;
                qemu_log_mask(CPU_LOG_PAGE, "host mmap_min_addr=0x%lx\n", mmap_min_addr);
            }
            fclose(fp);
        }
    }

    /*
     * Prepare copy of argv vector for target.
     */
    target_argc = argc - optind;
    target_argv = calloc(target_argc + 1, sizeof (char *));
    if (target_argv == NULL) {
	(void) fprintf(stderr, "Unable to allocate memory for target_argv\n");
	exit(EXIT_FAILURE);
    }

    /*
     * If argv0 is specified (using '-0' switch) we replace
     * argv[0] pointer with the given one.
     */
    i = 0;
    if (argv0 != NULL) {
        target_argv[i++] = strdup(argv0);
    }
    for (; i < target_argc; i++) {
        target_argv[i] = strdup(argv[optind + i]);
    }
    target_argv[target_argc] = NULL;

    ts = g_new0(TaskState, 1);
    init_task_state(ts);
    /* build Task State */
    ts->info = info;
    ts->bprm = &bprm;
    cpu->opaque = ts;
    task_settid(ts);

    ret = loader_exec(execfd, filename, target_argv, target_environ, regs,
        info, &bprm);
    if (ret != 0) {
        printf("Error while loading %s: %s\n", filename, strerror(-ret));
        _exit(EXIT_FAILURE);
    }

    for (wrk = target_environ; *wrk; wrk++) {
        g_free(*wrk);
    }

    g_free(target_environ);

    if (qemu_loglevel_mask(CPU_LOG_PAGE)) {
        qemu_log("guest_base  0x%lx\n", guest_base);
        log_page_dump();

        qemu_log("start_brk   0x" TARGET_ABI_FMT_lx "\n", info->start_brk);
        qemu_log("end_code    0x" TARGET_ABI_FMT_lx "\n", info->end_code);
        qemu_log("start_code  0x" TARGET_ABI_FMT_lx "\n", info->start_code);
        qemu_log("start_data  0x" TARGET_ABI_FMT_lx "\n", info->start_data);
        qemu_log("end_data    0x" TARGET_ABI_FMT_lx "\n", info->end_data);
        qemu_log("start_stack 0x" TARGET_ABI_FMT_lx "\n", info->start_stack);
        qemu_log("brk         0x" TARGET_ABI_FMT_lx "\n", info->brk);
        qemu_log("entry       0x" TARGET_ABI_FMT_lx "\n", info->entry);
        qemu_log("argv_start  0x" TARGET_ABI_FMT_lx "\n", info->arg_start);
        qemu_log("env_start   0x" TARGET_ABI_FMT_lx "\n",
                 info->arg_end + (abi_ulong)sizeof(abi_ulong));
        qemu_log("auxv_start  0x" TARGET_ABI_FMT_lx "\n", info->saved_auxv);
    }

    target_set_brk(info->brk);
    syscall_init();
    signal_init();

    /* Now that we've loaded the binary, GUEST_BASE is fixed.  Delay
       generating the prologue until now so that the prologue can take
       the real value of GUEST_BASE into account.  */
    tcg_prologue_init(tcg_ctx);
    tcg_region_init();

#if defined(TARGET_I386)
    env->cr[0] = CR0_PG_MASK | CR0_WP_MASK | CR0_PE_MASK;
    env->hflags |= HF_PE_MASK | HF_CPL_MASK;
    if (env->features[FEAT_1_EDX] & CPUID_SSE) {
        env->cr[4] |= CR4_OSFXSR_MASK;
        env->hflags |= HF_OSFXSR_MASK;
    }
#ifndef TARGET_ABI32
    /* enable 64 bit mode if possible */
    if (!(env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_LM)) {
        fprintf(stderr, "The selected x86 CPU does not support 64 bit mode\n");
        exit(EXIT_FAILURE);
    }
    env->cr[4] |= CR4_PAE_MASK;
    env->efer |= MSR_EFER_LMA | MSR_EFER_LME;
    env->hflags |= HF_LMA_MASK;
#endif

    /* flags setup : we activate the IRQs by default as in user mode */
    env->eflags |= IF_MASK;

    /* linux register setup */
#ifndef TARGET_ABI32
    env->regs[R_EAX] = regs->rax;
    env->regs[R_EBX] = regs->rbx;
    env->regs[R_ECX] = regs->rcx;
    env->regs[R_EDX] = regs->rdx;
    env->regs[R_ESI] = regs->rsi;
    env->regs[R_EDI] = regs->rdi;
    env->regs[R_EBP] = regs->rbp;
    env->regs[R_ESP] = regs->rsp;
    env->eip = regs->rip;
#else
    env->regs[R_EAX] = regs->eax;
    env->regs[R_EBX] = regs->ebx;
    env->regs[R_ECX] = regs->ecx;
    env->regs[R_EDX] = regs->edx;
    env->regs[R_ESI] = regs->esi;
    env->regs[R_EDI] = regs->edi;
    env->regs[R_EBP] = regs->ebp;
    env->regs[R_ESP] = regs->esp;
    env->eip = regs->eip;
#endif

    /* linux interrupt setup */
#ifndef TARGET_ABI32
    env->idt.limit = 511;
#else
    env->idt.limit = 255;
#endif
    env->idt.base = target_mmap(0, sizeof(uint64_t) * (env->idt.limit + 1),
                                PROT_READ|PROT_WRITE,
                                MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    idt_table = g2h(env->idt.base);
    set_idt(0, 0);
    set_idt(1, 0);
    set_idt(2, 0);
    set_idt(3, 3);
    set_idt(4, 3);
    set_idt(5, 0);
    set_idt(6, 0);
    set_idt(7, 0);
    set_idt(8, 0);
    set_idt(9, 0);
    set_idt(10, 0);
    set_idt(11, 0);
    set_idt(12, 0);
    set_idt(13, 0);
    set_idt(14, 0);
    set_idt(15, 0);
    set_idt(16, 0);
    set_idt(17, 0);
    set_idt(18, 0);
    set_idt(19, 0);
    set_idt(0x80, 3);

    /* linux segment setup */
    {
        uint64_t *gdt_table;
        env->gdt.base = target_mmap(0, sizeof(uint64_t) * TARGET_GDT_ENTRIES,
                                    PROT_READ|PROT_WRITE,
                                    MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
        env->gdt.limit = sizeof(uint64_t) * TARGET_GDT_ENTRIES - 1;
        gdt_table = g2h(env->gdt.base);
#ifdef TARGET_ABI32
        write_dt(&gdt_table[__USER_CS >> 3], 0, 0xfffff,
                 DESC_G_MASK | DESC_B_MASK | DESC_P_MASK | DESC_S_MASK |
                 (3 << DESC_DPL_SHIFT) | (0xa << DESC_TYPE_SHIFT));
#else
        /* 64 bit code segment */
        write_dt(&gdt_table[__USER_CS >> 3], 0, 0xfffff,
                 DESC_G_MASK | DESC_B_MASK | DESC_P_MASK | DESC_S_MASK |
                 DESC_L_MASK |
                 (3 << DESC_DPL_SHIFT) | (0xa << DESC_TYPE_SHIFT));
#endif
        write_dt(&gdt_table[__USER_DS >> 3], 0, 0xfffff,
                 DESC_G_MASK | DESC_B_MASK | DESC_P_MASK | DESC_S_MASK |
                 (3 << DESC_DPL_SHIFT) | (0x2 << DESC_TYPE_SHIFT));
    }
    cpu_x86_load_seg(env, R_CS, __USER_CS);
    cpu_x86_load_seg(env, R_SS, __USER_DS);
#ifdef TARGET_ABI32
    cpu_x86_load_seg(env, R_DS, __USER_DS);
    cpu_x86_load_seg(env, R_ES, __USER_DS);
    cpu_x86_load_seg(env, R_FS, __USER_DS);
    cpu_x86_load_seg(env, R_GS, __USER_DS);
    /* This hack makes Wine work... */
    env->segs[R_FS].selector = 0;
#else
    cpu_x86_load_seg(env, R_DS, 0);
    cpu_x86_load_seg(env, R_ES, 0);
    cpu_x86_load_seg(env, R_FS, 0);
    cpu_x86_load_seg(env, R_GS, 0);
#endif
#elif defined(TARGET_AARCH64)
    {
        int i;

        if (!(arm_feature(env, ARM_FEATURE_AARCH64))) {
            fprintf(stderr,
                    "The selected ARM CPU does not support 64 bit mode\n");
            exit(EXIT_FAILURE);
        }

        for (i = 0; i < 31; i++) {
            env->xregs[i] = regs->regs[i];
        }
        env->pc = regs->pc;
        env->xregs[31] = regs->sp;
#ifdef TARGET_WORDS_BIGENDIAN
        env->cp15.sctlr_el[1] |= SCTLR_E0E;
        for (i = 1; i < 4; ++i) {
            env->cp15.sctlr_el[i] |= SCTLR_EE;
        }
#endif
    }
#elif defined(TARGET_ARM)
    {
        int i;
        cpsr_write(env, regs->uregs[16], CPSR_USER | CPSR_EXEC,
                   CPSRWriteByInstr);
        for(i = 0; i < 16; i++) {
            env->regs[i] = regs->uregs[i];
        }
#ifdef TARGET_WORDS_BIGENDIAN
        /* Enable BE8.  */
        if (EF_ARM_EABI_VERSION(info->elf_flags) >= EF_ARM_EABI_VER4
            && (info->elf_flags & EF_ARM_BE8)) {
            env->uncached_cpsr |= CPSR_E;
            env->cp15.sctlr_el[1] |= SCTLR_E0E;
        } else {
            env->cp15.sctlr_el[1] |= SCTLR_B;
        }
#endif
    }
#elif defined(TARGET_SPARC)
    {
        int i;
	env->pc = regs->pc;
	env->npc = regs->npc;
        env->y = regs->y;
        for(i = 0; i < 8; i++)
            env->gregs[i] = regs->u_regs[i];
        for(i = 0; i < 8; i++)
            env->regwptr[i] = regs->u_regs[i + 8];
    }
#elif defined(TARGET_PPC)
    {
        int i;

#if defined(TARGET_PPC64)
        int flag = (env->insns_flags2 & PPC2_BOOKE206) ? MSR_CM : MSR_SF;
#if defined(TARGET_ABI32)
        env->msr &= ~((target_ulong)1 << flag);
#else
        env->msr |= (target_ulong)1 << flag;
#endif
#endif
        env->nip = regs->nip;
        for(i = 0; i < 32; i++) {
            env->gpr[i] = regs->gpr[i];
        }
    }
#elif defined(TARGET_M68K)
    {
        env->pc = regs->pc;
        env->dregs[0] = regs->d0;
        env->dregs[1] = regs->d1;
        env->dregs[2] = regs->d2;
        env->dregs[3] = regs->d3;
        env->dregs[4] = regs->d4;
        env->dregs[5] = regs->d5;
        env->dregs[6] = regs->d6;
        env->dregs[7] = regs->d7;
        env->aregs[0] = regs->a0;
        env->aregs[1] = regs->a1;
        env->aregs[2] = regs->a2;
        env->aregs[3] = regs->a3;
        env->aregs[4] = regs->a4;
        env->aregs[5] = regs->a5;
        env->aregs[6] = regs->a6;
        env->aregs[7] = regs->usp;
        env->sr = regs->sr;
        ts->sim_syscalls = 1;
    }
#elif defined(TARGET_MICROBLAZE)
    {
        env->regs[0] = regs->r0;
        env->regs[1] = regs->r1;
        env->regs[2] = regs->r2;
        env->regs[3] = regs->r3;
        env->regs[4] = regs->r4;
        env->regs[5] = regs->r5;
        env->regs[6] = regs->r6;
        env->regs[7] = regs->r7;
        env->regs[8] = regs->r8;
        env->regs[9] = regs->r9;
        env->regs[10] = regs->r10;
        env->regs[11] = regs->r11;
        env->regs[12] = regs->r12;
        env->regs[13] = regs->r13;
        env->regs[14] = regs->r14;
        env->regs[15] = regs->r15;	    
        env->regs[16] = regs->r16;	    
        env->regs[17] = regs->r17;	    
        env->regs[18] = regs->r18;	    
        env->regs[19] = regs->r19;	    
        env->regs[20] = regs->r20;	    
        env->regs[21] = regs->r21;	    
        env->regs[22] = regs->r22;	    
        env->regs[23] = regs->r23;	    
        env->regs[24] = regs->r24;	    
        env->regs[25] = regs->r25;	    
        env->regs[26] = regs->r26;	    
        env->regs[27] = regs->r27;	    
        env->regs[28] = regs->r28;	    
        env->regs[29] = regs->r29;	    
        env->regs[30] = regs->r30;	    
        env->regs[31] = regs->r31;	    
        env->sregs[SR_PC] = regs->pc;
    }
#elif defined(TARGET_MIPS)
    {
        int i;

        for(i = 0; i < 32; i++) {
            env->active_tc.gpr[i] = regs->regs[i];
        }
        env->active_tc.PC = regs->cp0_epc & ~(target_ulong)1;
        if (regs->cp0_epc & 1) {
            env->hflags |= MIPS_HFLAG_M16;
        }
        if (((info->elf_flags & EF_MIPS_NAN2008) != 0) !=
            ((env->active_fpu.fcr31 & (1 << FCR31_NAN2008)) != 0)) {
            if ((env->active_fpu.fcr31_rw_bitmask &
                  (1 << FCR31_NAN2008)) == 0) {
                fprintf(stderr, "ELF binary's NaN mode not supported by CPU\n");
                exit(1);
            }
            if ((info->elf_flags & EF_MIPS_NAN2008) != 0) {
                env->active_fpu.fcr31 |= (1 << FCR31_NAN2008);
            } else {
                env->active_fpu.fcr31 &= ~(1 << FCR31_NAN2008);
            }
            restore_snan_bit_mode(env);
        }
    }
#elif defined(TARGET_NIOS2)
    {
        env->regs[0] = 0;
        env->regs[1] = regs->r1;
        env->regs[2] = regs->r2;
        env->regs[3] = regs->r3;
        env->regs[4] = regs->r4;
        env->regs[5] = regs->r5;
        env->regs[6] = regs->r6;
        env->regs[7] = regs->r7;
        env->regs[8] = regs->r8;
        env->regs[9] = regs->r9;
        env->regs[10] = regs->r10;
        env->regs[11] = regs->r11;
        env->regs[12] = regs->r12;
        env->regs[13] = regs->r13;
        env->regs[14] = regs->r14;
        env->regs[15] = regs->r15;
        /* TODO: unsigned long  orig_r2; */
        env->regs[R_RA] = regs->ra;
        env->regs[R_FP] = regs->fp;
        env->regs[R_SP] = regs->sp;
        env->regs[R_GP] = regs->gp;
        env->regs[CR_ESTATUS] = regs->estatus;
        env->regs[R_EA] = regs->ea;
        /* TODO: unsigned long  orig_r7; */

        /* Emulate eret when starting thread. */
        env->regs[R_PC] = regs->ea;
    }
#elif defined(TARGET_OPENRISC)
    {
        int i;

        for (i = 0; i < 32; i++) {
            cpu_set_gpr(env, i, regs->gpr[i]);
        }
        env->pc = regs->pc;
        cpu_set_sr(env, regs->sr);
    }
#elif defined(TARGET_RISCV)
    {
        env->pc = regs->sepc;
        env->gpr[xSP] = regs->sp;
    }
#elif defined(TARGET_SH4)
    {
        int i;

        for(i = 0; i < 16; i++) {
            env->gregs[i] = regs->regs[i];
        }
        env->pc = regs->pc;
    }
#elif defined(TARGET_ALPHA)
    {
        int i;

        for(i = 0; i < 28; i++) {
            env->ir[i] = ((abi_ulong *)regs)[i];
        }
        env->ir[IR_SP] = regs->usp;
        env->pc = regs->pc;
    }
#elif defined(TARGET_CRIS)
    {
	    env->regs[0] = regs->r0;
	    env->regs[1] = regs->r1;
	    env->regs[2] = regs->r2;
	    env->regs[3] = regs->r3;
	    env->regs[4] = regs->r4;
	    env->regs[5] = regs->r5;
	    env->regs[6] = regs->r6;
	    env->regs[7] = regs->r7;
	    env->regs[8] = regs->r8;
	    env->regs[9] = regs->r9;
	    env->regs[10] = regs->r10;
	    env->regs[11] = regs->r11;
	    env->regs[12] = regs->r12;
	    env->regs[13] = regs->r13;
	    env->regs[14] = info->start_stack;
	    env->regs[15] = regs->acr;	    
	    env->pc = regs->erp;
    }
#elif defined(TARGET_S390X)
    {
            int i;
            for (i = 0; i < 16; i++) {
                env->regs[i] = regs->gprs[i];
            }
            env->psw.mask = regs->psw.mask;
            env->psw.addr = regs->psw.addr;
    }
#elif defined(TARGET_TILEGX)
    {
        int i;
        for (i = 0; i < TILEGX_R_COUNT; i++) {
            env->regs[i] = regs->regs[i];
        }
        for (i = 0; i < TILEGX_SPR_COUNT; i++) {
            env->spregs[i] = 0;
        }
        env->pc = regs->pc;
    }
#elif defined(TARGET_HPPA)
    {
        int i;
        for (i = 1; i < 32; i++) {
            env->gr[i] = regs->gr[i];
        }
        env->iaoq_f = regs->iaoq[0];
        env->iaoq_b = regs->iaoq[1];
    }
#elif defined(TARGET_XTENSA)
    {
        int i;
        for (i = 0; i < 16; ++i) {
            env->regs[i] = regs->areg[i];
        }
        env->sregs[WINDOW_START] = regs->windowstart;
        env->pc = regs->pc;
    }
#else
#error unsupported target CPU
#endif

#if defined(TARGET_ARM) || defined(TARGET_M68K)
    ts->stack_base = info->start_stack;
    ts->heap_base = info->brk;
    /* This will be filled in on the first SYS_HEAPINFO call.  */
    ts->heap_limit = 0;
#endif

    if (gdbstub_port) {
        if (gdbserver_start(gdbstub_port) < 0) {
            fprintf(stderr, "qemu: could not open gdbserver on port %d\n",
                    gdbstub_port);
            exit(EXIT_FAILURE);
        }
        gdb_handlesig(cpu, 0);
    }
    cpu_loop(env);
    /* never exits */
    return 0;
}
