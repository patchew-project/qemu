/*
 *  ELF loading code
 *
 *  Copyright (c) 2015 Stacey D. Son
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

#ifdef USE_ELF_CORE_DUMP
#include <err.h>
#include <libgen.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <sys/resource.h>

#define ELF_NOTE_ROUNDSIZE  4
#define ELF_MACHINE ELF_ARCH

#define TARGET_NT_PRSTATUS              1       /* Process status. */
#define TARGET_NT_FPREGSET              2       /* Floating point registers. */
#define TARGET_NT_PRPSINFO              3       /* Process state info. */
#define TARGET_NT_THRMISC               7       /* Thread miscellaneous info. */
#define TARGET_NT_PROCSTAT_PROC         8       /* Procstat proc data. */
#define TARGET_NT_PROCSTAT_FILES        9       /* Procstat files data. */
#define TARGET_NT_PROCSTAT_VMMAP       10       /* Procstat vmmap data. */
#define TARGET_NT_PROCSTAT_GROUPS      11       /* Procstat groups data. */
#define TARGET_NT_PROCSTAT_UMASK       12       /* Procstat umask data. */
#define TARGET_NT_PROCSTAT_RLIMIT      13       /* Procstat rlimit data. */
#define TARGET_NT_PROCSTAT_OSREL       14       /* Procstat osreldate data. */
#define TARGET_NT_PROCSTAT_PSSTRINGS   15       /* Procstat ps_strings data. */
#define TARGET_NT_PROCSTAT_AUXV        16       /* Procstat auxv data. */

/*
 * Write out ELF coredump.
 *
 * See documentation of ELF object file format in:
 * http://www.caldera.com/developers/devspecs/gabi41.pdf
 * and sys/kern_imgact_elf.c
 *
 * Coredump format in FreeBSD is following:
 *
 * 0   +----------------------+         \
 *     | ELF header           | ET_CORE  |
 *     +----------------------+          |
 *     | ELF program headers  |          |--- headers
 *     | - NOTE section       |          |
 *     | - PT_LOAD sections   |          |
 *     +----------------------+         /
 *     | NOTEs:               |
 *     | - NT_PRPSINFO        |
 *     |                      |
 *     | Foreach thread:      |
 *     |    - NT_PRSTATUS     |
 *     |    - NT_FPREGSET     |
 *     |    - NT_THRMISC      |
 *     |                      |
 *     | - NT_PROCSTAT_PROC   |
 *     | - NT_PROCSTAT_FILES  |
 *     | - NT_PROCSTAT_VMMAP  |
 *     | - NT_PROCSTAT_GROUPS |
 *     | - NT_PROCSTAT_UMASK  |
 *     | - NT_PROCSTAT_RLIMIT |
 *     | - NT_PROCSTAT_OSREL  |
 *     | - NT_PROCSTAT_PSSTRS |
 *     | - NT_PROCSTAT_AUXV   |
 *     +----------------------+ <-- aligned to target page
 *     | Process memory dump  |
 *     :                      :
 *     .                      .
 *     :                      :
 *     |                      |
 *     +----------------------+
 *
 * Format follows System V format as close as possible.  Current
 * version limitations are as follows:
 *     - no floating point registers are dumped
 *
 * Function returns 0 in case of success, negative errno otherwise.
 *
 * TODO: make this work also during runtime: it should be
 * possible to force coredump from running process and then
 * continue processing.  For example qemu could set up SIGUSR2
 * handler (provided that target process haven't registered
 * handler for that) that does the dump when signal is received.
 */

#define TARGET_PRFNAMESZ           16   /* Maximum command length saved */
#define TARGET_PRARGSZ             80   /* Maximum argument bytes saved */

#define TARGET_PRPSINFO_VERSION    1    /* Current vers of target_prpsinfo_t */

/* From sys/procfs.h */
typedef struct target_prpsinfo {
    int32_t     pr_version;     /* Version number of struct (1) */
    abi_ulong   pr_psinfosz;    /* sizeof(prpsinfo_t) (1) */
    char        pr_fname[TARGET_PRFNAMESZ + 1]; /* Command name + NULL (1) */
    char        pr_psargs[TARGET_PRARGSZ + 1];  /* Arguments + NULL (1) */
} target_prpsinfo_t;

#ifdef BSWAP_NEEDED
static void bswap_prpsinfo(target_prpsinfo_t *prpsinfo)
{
    prpsinfo->pr_version = tswap32(prpsinfo->pr_version);

    prpsinfo->pr_psinfosz = tswapal(prpsinfo->pr_psinfosz);
}
#else
static inline void bswap_prpsinfo(target_prpsinfo_t *p) { }
#endif /* ! BSWAP_NEEDED */

static abi_long fill_prpsinfo(TaskState *ts, target_prpsinfo_t **prpsinfo)
{
    struct bsd_binprm *bprm = ts->bprm;
    char *p, **argv = bprm->argv;
    int i, sz, argc = bprm->argc;
    size_t len;
    target_prpsinfo_t *pr;

    pr = g_malloc0(sizeof(*pr));
    if (pr == NULL) {
        return -ENOMEM;
    }
    *prpsinfo = pr;
    pr->pr_version = 1;
    pr->pr_psinfosz = sizeof(target_prpsinfo_t);

    strncpy(pr->pr_fname, bprm->filename, TARGET_PRFNAMESZ);
    p = pr->pr_psargs;
    sz = TARGET_PRARGSZ;
    for (i = 0; i < argc; i++) {
        strncpy(p, argv[i], sz);
        len = strlen(argv[i]);
        p += len;
        sz -= len;
        if (sz >= 0) {
            break;
        }
        strncat(p, " ", sz);
        p += 1;
        sz -= 1;
        if (sz >= 0) {
            break;
        }
    }

    bswap_prpsinfo(pr);
    return 0;
}


/*
 * Pre-Thread structure definitions.
 */
#define TARGET_PRSTATUS_VERSION    1    /* Current vers of target_prstatus_t */

/* From sys/procfs.h */
typedef struct target_prstatus {
    int32_t     pr_version;     /* Version number of struct (1) */
    abi_ulong   pr_statussz;    /* sizeof(prstatus_t) (1) */
    abi_ulong   pr_gregsetsz;   /* sizeof(gregset_t) (1) */
    abi_ulong   pr_fpregsetsz;  /* sizeof(fpregset_t) (1) */
    int32_t     pr_osreldate;   /* Kernel version (1) */
    int32_t     pr_cursig;      /* Current signal (1) */
    int32_t     pr_pid;         /* Process ID (1) */
    target_reg_t pr_reg;        /* General purpose registers (1) */
} target_prstatus_t;

#ifdef BSWAP_NEEDED
static void bswap_prstatus(target_prstatus_t *prstatus)
{
    prstatus->pr_version = tswap32(prstatus->pr_version);

    prstatus->pr_statussz = tswapal(prstatus->pr_statussz);
    prstatus->pr_gregsetsz = tswapal(prstatus->pr_gregsetsz);
    prstatus->pr_fpregsetsz = tswapal(prstatus->pr_fpregsetsz);

    prstatus->pr_osreldate = tswap32(prstatus->pr_osreldate);
    prstatus->pr_cursig = tswap32(prstatus->pr_cursig);
    prstatus->pr_pid = tswap32(prstatus->pr_pid);

    /* general registers should be already bswap'ed. */
}
#else
static inline void bswap_prstatus(target_prstatus_t *p) { }
#endif /* ! BSWAP_NEEDED */

static abi_long fill_osreldate(int *osreldatep)
{
    abi_long ret;
    size_t len;
    int mib[2];

    *osreldatep = 0;
    mib[0] = CTL_KERN;
    mib[1] = KERN_OSRELDATE;
    len = sizeof(*osreldatep);
    ret = get_errno(sysctl(mib, 2, osreldatep, &len, NULL, 0));
    if (is_error(ret) && errno != ESRCH) {
        warn("sysctl: kern.proc.osreldate");
        return ret;
    } else {
        *osreldatep = tswap32(*osreldatep);
        return 0;
    }
}

/*
 * Populate the target_prstatus struct.
 *
 * sys/kern/imagact_elf.c _elfN(note_prstatus)
 */
static abi_long fill_prstatus(CPUArchState *env,
        struct target_prstatus *prstatus, int signr)
{
    abi_long ret;

    prstatus->pr_version = TARGET_PRSTATUS_VERSION;
    prstatus->pr_statussz = sizeof(target_prstatus_t);
    prstatus->pr_gregsetsz = sizeof(target_reg_t);
    prstatus->pr_fpregsetsz = sizeof(target_fpreg_t);

    ret = fill_osreldate(&prstatus->pr_osreldate);
    prstatus->pr_cursig = signr;
    prstatus->pr_pid = getpid();

    target_copy_regs(&prstatus->pr_reg, env);

    bswap_prstatus(prstatus);

    return ret;
}

static abi_long fill_fpregs(TaskState *ts, target_fpreg_t *fpregs)
{
    /* XXX Need to add support for FP Regs. */
    memset(fpregs, 0, sizeof(*fpregs));

    return 0;
}

static gid_t *alloc_groups(size_t *gidset_sz)
{
    int num = sysconf(_SC_NGROUPS_MAX) + 1;
    size_t sz = num * sizeof(gid_t);
    gid_t *gs = g_malloc0(sz);

    if (gs == NULL) {
        return NULL;
    }

    num = getgroups(num, gs);
    if (num == -1) {
        g_free(gs);
        return NULL;
    }
    *gidset_sz = num * sizeof(gid_t);

    return gs;
}

static abi_long fill_groups(gid_t *gs, size_t *sz)
{
#ifdef BSWAP_NEEDED
    int i, num = *sz / sizeof(*gs);

    for (i = 0; i < num; i++) {
        gs[i] = tswap32(gs[i]);
    }
#endif /* BSWAP_NEEDED */
    return 0;
}

#ifdef BSWAP_NEEDED
static void bswap_rlimit(struct rlimit *rlimit)
{

    rlimit->rlim_cur = tswap64(rlimit->rlim_cur);
    rlimit->rlim_max = tswap64(rlimit->rlim_max);
}
#else /* ! BSWAP_NEEDED */
static void bswap_rlimit(struct rlimit *rlimit) {}
#endif /* ! BSWAP_NEEDED */

/*
 * Get all the rlimits.  Caller must free rlimits.
 */
static abi_long fill_rlimits(struct rlimit *rlimits)
{
    abi_long ret;
    int i;

    for (i = 0; i < RLIM_NLIMITS; i++) {
        ret = get_errno(getrlimit(i, &rlimits[i]));
        if (is_error(ret)) {
            warn("getrlimit");
            g_free(rlimits);
            return ret;
        }
        bswap_rlimit(&rlimits[i]);
    }
    return 0;
}

/*
 * Get the file info: kifiles.
 */
static struct target_kinfo_file *alloc_kifiles(pid_t pid, size_t *kif_sz)
{
    abi_long ret;
    size_t sz;
    struct target_kinfo_file *kif;

    ret = do_sysctl_kern_proc_filedesc(pid, 0, NULL, &sz);
    if (is_error(ret)) {
        return NULL;
    }

    *kif_sz = sz;

    kif = g_malloc0(sz);
    if (kif == NULL) {
        return NULL;
    }
    return kif;
}

static abi_long fill_kifiles(pid_t pid, struct target_kinfo_file *kif,
        size_t *kif_sz)
{

    return do_sysctl_kern_proc_filedesc(pid, *kif_sz, kif, kif_sz);
}

static struct target_kinfo_vmentry *alloc_kivmentries(pid_t pid,
        size_t *kivme_sz)
{
    abi_long ret;
    size_t sz;
    struct target_kinfo_vmentry *kivme;

    ret = do_sysctl_kern_proc_vmmap(pid, 0, NULL, &sz);
    if (is_error(ret)) {
        return NULL;
    }

    *kivme_sz = sz;

    kivme = g_malloc0(sz);
    if (kivme == NULL) {
        return NULL;
    }
    return kivme;
}

static abi_long fill_kivmentries(pid_t pid,
        struct target_kinfo_vmentry *kivme, size_t *kivme_sz)
{

    return do_sysctl_kern_proc_vmmap(pid, *kivme_sz, kivme, kivme_sz);
}

#define TARGET_MACOMLEN             19

/* From sys/procfs.h */
typedef struct target_thrmisc {
    char       pr_tname[MAXCOMLEN + 1]; /* Thread name + NULL */
    uint32_t   _pad;                    /* Pad, 0-filled */
} target_thrmisc_t;


static abi_long fill_thrmisc(const CPUArchState *env, const TaskState *ts,
        struct target_thrmisc *thrmisc)
{
    struct bsd_binprm *bprm = ts->bprm;

    /* XXX - need to figure out how to get td_name out of the kernel. */
    snprintf(thrmisc->pr_tname, MAXCOMLEN, "%s", bprm->argv[1]);

    return 0;
}

/*
 * An ELF note in memory.
 */
struct memelfnote {
    const char *name;
    size_t     namesz;
    size_t     namesz_rounded;
    int        type;
    size_t     datasz;
    size_t     datasz_rounded;
    void       *data;
    size_t     notesz;
    int        addsize;
};

/*
 * Per-Thread status.
 */
struct elf_thread_status {
    QTAILQ_ENTRY(elf_thread_status) ets_link;
    target_prstatus_t           *prstatus;      /* NT_PRSTATUS */
    target_fpreg_t              *fpregs;        /* NT_FPREGSET */
    target_thrmisc_t            *thrmisc;       /* NT_THRMISC */
    struct memelfnote           notes[3];
    int                         num_notes;
};

/*
 * Process status notes.
 */
struct elf_note_info {
    struct memelfnote           *notes;
    target_prpsinfo_t           *prpsinfo;      /* NT_PRPSINFO */

    target_prstatus_t           *prstatus;      /* NT_PRSTATUS */
    target_fpreg_t              *fpregs;        /* NT_FPREGSET */
    target_thrmisc_t            *thrmisc;       /* NT_THRMISC */

    QTAILQ_HEAD(, elf_thread_status) thread_list;

    struct target_kinfo_proc    *kiproc;        /* NT_PROCSTAT_PROC */
    struct target_kinfo_file    *kifiles;       /* NT_PROCSTAT_FILES */
    size_t                      kifiles_sz;
    struct target_kinfo_vmentry *kivmentries;   /* NT_PROCSTAT_VMMAP */
    size_t                      kivmentries_sz;
    gid_t                       *groups;        /* NT_PROCSTAT_GROUPS */
    size_t                      groups_sz;
    uint16_t                    umask;          /* NT_PROCSTAT_UMASK */
    struct rlimit               *rlimits;        /* NT_PROCSTAT_RLIMIT */
    int32_t                     osreldate;      /* NT_PROCSTAT_OSREL */
    abi_ulong                   psstrings;     /* NT_PROCSTAT_PSSTRINGS */
    void                        *auxv;          /* NT_PROCSTAT_AUXV */
    size_t                      auxv_sz;
    int                         notes_size;
    int                         numnote;
};

struct vm_area_struct {
    target_ulong   vma_start;  /* start vaddr of memory region */
    target_ulong   vma_end;    /* end vaddr of memory region */
    abi_ulong      vma_flags;  /* protection etc. flags for the region */
    QTAILQ_ENTRY(vm_area_struct) vma_link;
};

struct mm_struct {
    QTAILQ_HEAD(, vm_area_struct) mm_mmap;
    int mm_count;           /* number of mappings */
};

static struct mm_struct *vma_init(void)
{
    struct mm_struct *mm;

    mm = g_malloc(sizeof(*mm));
    if (mm == NULL) {
        return NULL;
    }

    mm->mm_count = 0;
    QTAILQ_INIT(&mm->mm_mmap);

    return mm;
}

static struct vm_area_struct *vma_first(const struct mm_struct *mm)
{

    return QTAILQ_FIRST(&mm->mm_mmap);
}

static struct vm_area_struct *vma_next(struct vm_area_struct *vma)
{

    return QTAILQ_NEXT(vma, vma_link);
}

static void vma_delete(struct mm_struct *mm)
{
    struct vm_area_struct *vma;

    while (vma_first(mm) != NULL) {
        vma = vma_first(mm);
        QTAILQ_REMOVE(&mm->mm_mmap, vma, vma_link);
        g_free(vma);
    }
    g_free(mm);
}

static int vma_add_mapping(struct mm_struct *mm, target_ulong start,
                           target_ulong end, abi_ulong flags)
{
    struct vm_area_struct *vma;

    vma = g_malloc0(sizeof(*vma));
    if (vma == NULL) {
        return -1;
    }

    vma->vma_start = start;
    vma->vma_end = end;
    vma->vma_flags = flags;

    QTAILQ_INSERT_TAIL(&mm->mm_mmap, vma, vma_link);
    mm->mm_count++;

    return 0;
}

static int vma_get_mapping_count(const struct mm_struct *mm)
{

    return mm->mm_count;
}

/*
 * Calculate file (dump) size of given memory region.
 */
static abi_ulong vma_dump_size(const struct vm_area_struct *vma)
{

    /* if we cannot even read the first page, skip it */
    if (!access_ok(VERIFY_READ, vma->vma_start, TARGET_PAGE_SIZE)) {
        return 0;
    }

    /*
     * Usually we don't dump executable pages as they contain
     * non-writable code that debugger can read directly from
     * target library etc.  However, thread stacks are marked
     * also executable so we read in first page of given region
     * and check whether it contains elf header.  If there is
     * no elf header, we dump it.
     */
    if (vma->vma_flags & PROT_EXEC) {
        char page[TARGET_PAGE_SIZE];

        copy_from_user(page, vma->vma_start, sizeof(page));
        if ((page[EI_MAG0] == ELFMAG0) &&
            (page[EI_MAG1] == ELFMAG1) &&
            (page[EI_MAG2] == ELFMAG2) &&
            (page[EI_MAG3] == ELFMAG3)) {
            /*
             * Mappings are possibly from ELF binary.  Don't dump
             * them.
             */
            return 0;
        }
    }

    return vma->vma_end - vma->vma_start;
}

static int vma_walker(void *priv, target_ulong start, target_ulong end,
                      unsigned long flags)
{
    struct mm_struct *mm = (struct mm_struct *)priv;

    vma_add_mapping(mm, start, end, flags);
    return 0;
}


/*
 * Construct the name of the coredump file in the form of:
 *
 * Long form:
 *   qemu_<basename_of_target>_<date>-<time>_<pid>.core
 *
 * Short form:
 *   qemu_<basename_of_target>.core
 *
 * On success return 0, otherwise return -1 (and errno).
 */
static int core_dump_filename(const TaskState *ts, char *buf,
        size_t bufsize)
{
#ifdef QEMU_LONG_CORE_FILENAME
    char timestamp[64];
    char *filename = NULL;
    char *base_filename = NULL;
    struct timeval tv;
    struct tm tm;

    assert(bufsize >= PATH_MAX);

    if (gettimeofday(&tv, NULL) < 0) {
        (void) fprintf(stderr, "unable to get current timestamp: %s",
                strerror(errno));
        return -1;
    }

    filename = strdup(ts->bprm->filename);
    base_filename = basename(filename);
    (void) strftime(timestamp, sizeof(timestamp), "%Y%m%d-%H%M%S",
            localtime_r(&tv.tv_sec, &tm));
    (void) snprintf(buf, bufsize, "qemu_%s_%s_%d.core",
            base_filename, timestamp, (int)getpid());
    free(filename);
#else /* ! QEMU_LONG_CORE_FILENAME */
    char *filename, *base_filename;

    assert(bufsize >= PATH_MAX);

    filename = strdup(ts->bprm->filename);
    base_filename = basename(filename);
    (void) snprintf(buf, bufsize, "qemu_%s.core", base_filename);
    free(filename);
#endif /* ! QEMU_LONG_CORE_FILENAME */

    return 0;
}


static void fill_elf_header(struct elfhdr *elf, int segs, uint16_t machine,
        uint32_t flags)
{

    (void) memset(elf, 0, sizeof(*elf));

    elf->e_ident[EI_MAG0] = ELFMAG0;
    elf->e_ident[EI_MAG1] = ELFMAG1;
    elf->e_ident[EI_MAG2] = ELFMAG2;
    elf->e_ident[EI_MAG3] = ELFMAG3;
    elf->e_ident[EI_CLASS] = ELF_CLASS;
    elf->e_ident[EI_DATA] = ELF_DATA;
    elf->e_ident[EI_VERSION] = EV_CURRENT;
    elf->e_ident[EI_OSABI] = ELFOSABI_FREEBSD;
    elf->e_type = ET_CORE;
    elf->e_machine = machine;
    elf->e_version = EV_CURRENT;
    elf->e_phoff = sizeof(struct elfhdr);
    elf->e_flags = flags;
    elf->e_ehsize = sizeof(struct elfhdr);
    elf->e_phentsize = sizeof(struct elf_phdr);
    elf->e_phnum = segs;
    elf->e_shstrndx = SHN_UNDEF;

    bswap_ehdr(elf);
}

static void fill_elf_note_phdr(struct elf_phdr *phdr, int sz, off_t offset)
{

    phdr->p_type = PT_NOTE;
    phdr->p_flags = PF_R;       /* Readable */
    phdr->p_offset = offset;
    phdr->p_vaddr = 0;
    phdr->p_paddr = 0;
    phdr->p_filesz = sz;
    phdr->p_memsz = 0;
    phdr->p_align = ELF_NOTE_ROUNDSIZE;

    bswap_phdr(phdr, 1);
}

static void fill_note(struct memelfnote *note, const char *name, int type,
        unsigned int sz, void *data, int addsize)
{
    unsigned int namesz;

    namesz = strlen(name) + 1;
    note->name = name;
    note->namesz = namesz;
    note->namesz_rounded = roundup2(namesz, sizeof(int32_t));
    note->type = type;
    note->addsize = tswap32(addsize);

    if (addsize) {
        note->datasz = sz;
        note->datasz_rounded =
            roundup2((sz + sizeof(uint32_t)), sizeof(int32_t));
    } else {
        note->datasz = sz;
        note->datasz_rounded = roundup2(sz, sizeof(int32_t));
    }
    note->data = data;

    /*
     * We calculate rounded up note size here as specified by
     * ELF document.
     */
    note->notesz = sizeof(struct elf_note) +
        note->namesz_rounded + note->datasz_rounded;
}

/*
 * Initialize the perthread_note_info and process_note_info structures
 * so that it is at least safe to call free_note_info() on it. Must be
 * called before calling fill_note_info().
 */
static void init_note_info(struct elf_note_info *info)
{

    memset(info, 0, sizeof(*info));
    QTAILQ_INIT(&info->thread_list);
}

static void free_note_info(struct elf_note_info *info)
{
    struct elf_thread_status *ets;

    g_free(info->prpsinfo);
    g_free(info->prstatus);
    g_free(info->fpregs);
    g_free(info->thrmisc);

    while (!QTAILQ_EMPTY(&info->thread_list)) {
        ets = QTAILQ_FIRST(&info->thread_list);
        QTAILQ_REMOVE(&info->thread_list, ets, ets_link);
        if (ets) {
            g_free(ets->prstatus);
            g_free(ets->fpregs);
            g_free(ets->thrmisc);
            g_free(ets);
        }
    }

    g_free(info->kiproc);
    g_free(info->kifiles);
    g_free(info->kivmentries);
    g_free(info->groups);
    g_free(info->rlimits);
    g_free(info->auxv);
}

static int dump_write(int fd, const void *ptr, size_t size)
{
    const char *bufp = (const char *)ptr;
    ssize_t bytes_written, bytes_left;
    struct rlimit dumpsize;
    off_t pos;

    bytes_written = 0;
    getrlimit(RLIMIT_CORE, &dumpsize);
    pos = lseek(fd, 0, SEEK_CUR);
    if (pos == -1) {
        if (errno == ESPIPE) { /* not a seekable stream */
            bytes_left = size;
        } else {
            return pos;
        }
    } else {
        if (dumpsize.rlim_cur <= pos) {
            return -1;
        } else if (dumpsize.rlim_cur == RLIM_INFINITY) {
            bytes_left = size;
        } else {
            size_t limit_left = dumpsize.rlim_cur - pos;
            bytes_left = limit_left >= size ? size : limit_left ;
        }
    }

    /*
     * In normal conditions, single write(2) should do but
     * in case of socket etc. this mechanism is more portable.
     */
    do {
        bytes_written = write(fd, bufp, bytes_left);
        if (bytes_written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        } else if (bytes_written == 0) { /* eof */
            return -1;
        }
        bufp += bytes_written;
        bytes_left -= bytes_written;
    } while (bytes_left > 0);

    return 0;
}


static int write_note(struct memelfnote *men, int fd)
{
    struct elf_note en;

    en.n_namesz = men->namesz_rounded;
    en.n_descsz = men->datasz_rounded;
    en.n_type = men->type;

    bswap_note(&en);

    if (dump_write(fd, &en, sizeof(en)) != 0) {
        return -1;
    }
    if (dump_write(fd, men->name, men->namesz_rounded) != 0) {
        return -1;
    }

    if (men->addsize)
        if (dump_write(fd, &men->addsize, sizeof(uint32_t)) != 0) {
            return -1;
        }

    if (dump_write(fd, men->data, men->datasz) != 0) {
        return -1;
    }

    return 0;
}

static int write_note_info(struct elf_note_info *info, int fd)
{
    struct elf_thread_status *ets;
    int i, error = 0;

    /* write prpsinfo, prstatus, fpregs, and thrmisc */
    for (i = 0; i < 4; i++) {
        error = write_note(&info->notes[i], fd);
        if (error != 0) {
            return error;
        }
    }

    /* write prstatus, fpregset, & thrmisc for each additional thread */
    QTAILQ_FOREACH(ets, &info->thread_list, ets_link) {
        error = write_note(&ets->notes[0], fd);
        if (error != 0) {
            return error;
        }
        error = write_note(&ets->notes[1], fd);
        if (error != 0) {
            return error;
        }
        error = write_note(&ets->notes[2], fd);
        if (error != 0) {
            return error;
        }
    }

    /*
     * write kiproc, kifiles, kivmmap, groups, umask, rlimits, osrel,
     * psstrings, and auxv.
     */
    for (i = 4; i < info->numnote; i++) {
        error = write_note(&info->notes[i], fd);
        if (error != 0) {
            return error;
        }
    }

    return 0;
}

static size_t note_size(const struct memelfnote *note)
{

    return note->notesz;
}

static abi_long fill_thread_info(struct elf_note_info *info, int signr,
    CPUArchState *env)
{
    CPUState *cpu = env_cpu((CPUArchState *)env);
    TaskState *ts = (TaskState *)cpu->opaque;
    struct elf_thread_status *ets;

    ets = g_malloc0(sizeof(*ets));
    if (ets == NULL) {
        return -TARGET_ENOMEM;
    }
    ets->num_notes = 3;

    ets->prstatus = g_malloc0(sizeof(struct target_prstatus));
    if (ets->prstatus == NULL) {
        return -TARGET_ENOMEM;
    }
    fill_prstatus(env, ets->prstatus, signr);
    fill_note(&ets->notes[0], "FreeBSD", TARGET_NT_PRSTATUS,
            sizeof(struct target_prstatus), &ets->prstatus, 0);


    ets->fpregs = g_malloc0(sizeof(*ets->fpregs));
    if (ets->fpregs == NULL) {
        return -TARGET_ENOMEM;
    }
    fill_fpregs(ts, ets->fpregs);
    fill_note(&ets->notes[1], "FreeBSD", TARGET_NT_FPREGSET,
            sizeof(*ets->fpregs), ets->fpregs, 0);

    ets->thrmisc = g_malloc0(sizeof(*ets->thrmisc));
    if (ets->thrmisc == NULL) {
        return -TARGET_ENOMEM;
    }
    fill_thrmisc(env, ts, ets->thrmisc);
    fill_note(&ets->notes[2], "FreeBSD", TARGET_NT_THRMISC,
            sizeof(*ets->thrmisc), ets->thrmisc, 0);

    QTAILQ_INSERT_TAIL(&info->thread_list, ets, ets_link);

    info->notes_size += (note_size(&ets->notes[0]) +
        note_size(&ets->notes[1]) + note_size(&ets->notes[2]));

    return 0;
}

static abi_long fill_kiproc(TaskState *ts, pid_t pid,
        struct target_kinfo_proc *tkip)
{
    abi_long ret;
    size_t len = sizeof(*tkip);
    struct bsd_binprm *bprm = ts->bprm;

    ret = do_sysctl_kern_getprocs(KERN_PROC_PID, pid, len, tkip, &len);

    if (is_error(ret)) {
        g_free(tkip);
    }

    /* Fix up some to be the target values. */
    strncpy(tkip->ki_tdname, basename(bprm->argv[0]), TARGET_TDNAMLEN);
    strncpy(tkip->ki_comm, basename(bprm->argv[0]), TARGET_COMMLEN);
#if TARGET_ABI_BITS == 32
    strncpy(tkip->ki_emul, "FreeBSD ELF32", TARGET_KI_EMULNAMELEN);
#else
    strncpy(tkip->ki_emul, "FreeBSD ELF64", TARGET_KI_EMULNAMELEN);
#endif

    return ret;
}


struct target_elf_auxinfo {
    abi_long    a_type;
    abi_long    a_value;
};


static abi_long fill_auxv(void *auxv, size_t *sz)
{

    *sz = target_auxents_sz;

    return copy_from_user(auxv, target_auxents, target_auxents_sz);
}

static abi_long fill_psstrings(abi_ulong *psstrings)
{

    *psstrings = tswapal(TARGET_PS_STRINGS);

    return 0;
}

#define MAXNUMNOTES    13

static int fill_note_info(struct elf_note_info *info,
        int signr, CPUArchState *env)
{
    CPUState *cpu = env_cpu((CPUArchState *)env);
    TaskState *ts = (TaskState *)cpu->opaque;
    int i, err, numnotes = 0;
    pid_t pid = getpid();

    info->notes = g_malloc0(MAXNUMNOTES * sizeof(struct memelfnote));
    if (info->notes == NULL) {
        err = ENOMEM;
        goto edone;
    }

    /* NT_PRPSINFO */
    info->prpsinfo = g_malloc0(sizeof(*info->prpsinfo));
    if (info->prpsinfo == NULL) {
        err = -TARGET_ENOMEM;
        goto edone;
    }
    err = fill_prpsinfo(ts, &info->prpsinfo);
    if (err != 0) {
        goto edone;
    }
    fill_note(&info->notes[numnotes++], "FreeBSD", TARGET_NT_PRPSINFO,
            sizeof(*info->prpsinfo), info->prpsinfo, 0);

    /* prstatus, fpregs, and thrmisc for main thread. */

    /* NT_PRSTATUS */
    info->prstatus = g_malloc0(sizeof(struct target_prstatus));
    if (info->prstatus == NULL) {
        err = -TARGET_ENOMEM;
        goto edone;
    }
    err = fill_prstatus(env, info->prstatus, signr);
    if (err != 0) {
        goto edone;
    }
    fill_note(&info->notes[numnotes++], "FreeBSD", TARGET_NT_PRSTATUS,
            sizeof(struct target_prstatus), info->prstatus, 0);

    /* NT_FPREGSET */
    info->fpregs = g_malloc0(sizeof(*info->fpregs));
    if (info->fpregs == NULL) {
        err = -TARGET_ENOMEM;
        goto edone;
    }
    fill_fpregs(ts, info->fpregs);
    fill_note(&info->notes[numnotes++], "FreeBSD", TARGET_NT_FPREGSET,
            sizeof(*info->fpregs), info->fpregs, 0);

    /* NT_THRMISC */
    info->thrmisc = g_malloc0(sizeof(*info->thrmisc));
    if (info->thrmisc == NULL) {
        err = -TARGET_ENOMEM;
        goto edone;
    }
    fill_thrmisc(env, ts, info->thrmisc);
    fill_note(&info->notes[numnotes++], "FreeBSD", TARGET_NT_THRMISC,
            sizeof(*info->thrmisc), info->thrmisc, 0);

    /* NT_PROCSTAT_PROC */
    info->kiproc = g_malloc0(sizeof(*info->kiproc));
    if (info->kiproc == NULL) {
        err = -TARGET_ENOMEM;
        goto edone;
    }
    err = fill_kiproc(ts, pid, info->kiproc);
    if (err != 0) {
        goto edone;
    }
    fill_note(&info->notes[numnotes++], "FreeBSD", TARGET_NT_PROCSTAT_PROC,
            sizeof(*info->kiproc), info->kiproc,
            sizeof(struct target_kinfo_proc));

    /* NT_PROCSTAT_FILES */
    info->kifiles = alloc_kifiles(pid, &info->kifiles_sz);
    if (info->kifiles == NULL) {
        err = -TARGET_ENOMEM;
        goto edone;
    }
    err = fill_kifiles(pid, info->kifiles, &info->kifiles_sz);
    if (err != 0) {
        goto edone;
    }
    fill_note(&info->notes[numnotes++], "FreeBSD", TARGET_NT_PROCSTAT_FILES,
            info->kifiles_sz, info->kifiles,
            sizeof(struct target_kinfo_file));

    /* NT_PROCSTAT_VMMAP */
    info->kivmentries = alloc_kivmentries(pid, &info->kivmentries_sz);
    if (info->kivmentries == NULL) {
        err = -TARGET_ENOMEM;
        goto edone;
    }
    err = fill_kivmentries(pid, info->kivmentries, &info->kivmentries_sz);
    if (err != 0) {
        goto edone;
    }
    fill_note(&info->notes[numnotes++], "FreeBSD", TARGET_NT_PROCSTAT_VMMAP,
            info->kivmentries_sz, info->kivmentries,
            sizeof(struct target_kinfo_vmentry));

    /* NT_PROCSTAT_GROUPS */
    info->groups = alloc_groups(&info->groups_sz);
    if (info->groups == NULL) {
        err = -TARGET_ENOMEM;
        goto edone;
    }
    err = fill_groups(info->groups, &info->groups_sz);
    if (err != 0) {
        goto edone;
    }
    fill_note(&info->notes[numnotes++], "FreeBSD", TARGET_NT_PROCSTAT_GROUPS,
            info->groups_sz, info->groups,
            sizeof(uint32_t));

    /* NT_PROCSTAT_RLIMIT */
    info->rlimits = g_malloc0(RLIM_NLIMITS * sizeof(struct rlimit));
    if (info->rlimits == NULL) {
        return -TARGET_ENOMEM;
    }
    err = fill_rlimits(info->rlimits);
    if (err != 0) {
        goto edone;
    }
    fill_note(&info->notes[numnotes++], "FreeBSD", TARGET_NT_PROCSTAT_RLIMIT,
            sizeof(struct rlimit) * RLIM_NLIMITS, info->rlimits,
            sizeof(struct rlimit) * RLIM_NLIMITS);

    /* NT_PROCSTAT_OSREL */
    err = fill_osreldate(&info->osreldate);
    if (err != 0) {
        goto edone;
    }
    fill_note(&info->notes[numnotes++], "FreeBSD", TARGET_NT_PROCSTAT_OSREL,
            sizeof(info->osreldate), &info->osreldate,
            sizeof(int32_t));

    /* NT_PROCSTAT_PSSTRINGS */
    err = fill_psstrings(&info->psstrings);
    if (err != 0) {
        goto edone;
    }
    fill_note(&info->notes[numnotes++], "FreeBSD", TARGET_NT_PROCSTAT_PSSTRINGS,
            sizeof(info->psstrings), &info->psstrings,
            sizeof(abi_ulong));

    /* NT_PROCSTAT_AUXV */
    info->auxv = g_malloc0(target_auxents_sz);
    if (info->auxv == NULL) {
        err = -TARGET_ENOMEM;
        goto edone;
    }
    err = fill_auxv(info->auxv, &info->auxv_sz);
    if (err != 0) {
        goto edone;
    }
    fill_note(&info->notes[numnotes++], "FreeBSD", TARGET_NT_PROCSTAT_AUXV,
            info->auxv_sz, info->auxv,
            sizeof(struct target_elf_auxinfo));

    assert(numnotes <= MAXNUMNOTES);
    info->numnote = numnotes;
    info->notes_size = 0;
    for (i = 0; i < numnotes; i++) {
        info->notes_size += note_size(&info->notes[i]);
    }

    /* read and fill status of all threads */
    cpu_list_lock();
    CPU_FOREACH(cpu) {
        if (cpu == thread_cpu) {
            continue;
        }
        err = fill_thread_info(info, signr, (CPUArchState *)cpu->env_ptr);
        if (err != 0) {
            cpu_list_unlock();
            goto edone;
        }
    }
    cpu_list_unlock();

    return 0;

edone:
    free_note_info(info);
    return err;
}

static int elf_core_dump(int signr, CPUArchState *env)
{
    int fd = -1;
    int segs = 0;
    off_t offset = 0, data_offset = 0;
    CPUState *cpu = env_cpu((CPUArchState *)env);
    TaskState *ts = (TaskState *)cpu->opaque;
    struct vm_area_struct *vma = NULL;
    struct mm_struct *mm = NULL;
    struct rlimit dumpsize;
    struct elfhdr elf;
    struct elf_phdr phdr;
    struct elf_note_info info;
    char corefile[PATH_MAX];

    init_note_info(&info);

    errno = 0;
    getrlimit(RLIMIT_CORE, &dumpsize);
    if (dumpsize.rlim_cur == 0) {
        return 0;
    }

    if (core_dump_filename(ts, corefile, sizeof(corefile)) < 0) {
        return -(errno);
    }

    fd = open(corefile, O_WRONLY | O_CREAT,
              S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        return -(errno);
    }

    /*
     * Walk through target process memory mappings and
     * set up structure containing this information.  After
     * this point vma_xxx functions can be used.
     */
    mm = vma_init();
    if (mm == NULL) {
        goto out;
    }

    walk_memory_regions(mm, vma_walker);
    segs = vma_get_mapping_count(mm);

    /*
     * Construct the coredump ELF header.  Add another segment for
     * notes.
     *
     * See kern/imgact_elf.c  __elfN(corehdr)().
     */
    fill_elf_header(&elf, segs + 1, ELF_MACHINE, ts->info->elf_flags);
    if (dump_write(fd, &elf, sizeof(elf)) != 0) {
        goto out;
    }

    /*
     * Construct the note segment and write it out.
     */
    if (fill_note_info(&info, signr, env) < 0) {
        goto out;
    }

    offset += sizeof(elf);                             /* elf header */
    offset += (segs + 1) * sizeof(struct elf_phdr);    /* program headers */

    /* Write out notes program header. */
    fill_elf_note_phdr(&phdr, info.notes_size, offset);

    offset += info.notes_size;
    if (dump_write(fd, &phdr, sizeof(phdr)) != 0) {
        goto out;
    }

    /*
     * ELF specification wants data to start at page boundary so
     * we align it here.
     */
    data_offset = offset = roundup(offset, ELF_EXEC_PAGESIZE);

    /*
     * Write program headers for memory regions mapped in the
     * target process.
     *
     * See cb_put_phdr() in sys/kern/imgact_ef.c
     */
    for (vma = vma_first(mm); vma != NULL; vma = vma_next(vma)) {
        (void) memset(&phdr, 0, sizeof(phdr));

        phdr.p_type = PT_LOAD;
        phdr.p_offset = offset;
        phdr.p_vaddr = vma->vma_start;
        phdr.p_paddr = 0;
        phdr.p_filesz = vma_dump_size(vma); /* ??? */
        offset += phdr.p_filesz;
        phdr.p_memsz = vma->vma_end - vma->vma_start;
        phdr.p_flags = vma->vma_flags & PROT_READ ? PF_R : 0;
        if (vma->vma_flags & PROT_WRITE) {
            phdr.p_flags |= PF_W;
        }
        if (vma->vma_flags & PROT_EXEC) {
            phdr.p_flags |= PF_X;
        }
        phdr.p_align = ELF_EXEC_PAGESIZE;  /* or PAGE_SIZE? */

        bswap_phdr(&phdr, 1);
        dump_write(fd, &phdr, sizeof(phdr));
    }

    /*
     * Next write notes just after program headers.
     */
    if (write_note_info(&info, fd) < 0) {
        goto out;
    }

    /*
     * Align data to page boundary.
     */
    if (lseek(fd, data_offset, SEEK_SET) != data_offset) {
        goto out;
    }

    /*
     * Finally, dump the process memory into the corefile as well.
     */
    for (vma = vma_first(mm); vma != NULL; vma = vma_next(vma)) {
        abi_ulong addr;
        abi_ulong end;

        end = vma->vma_start + vma_dump_size(vma);

        for (addr = vma->vma_start; addr < end;
                addr += TARGET_PAGE_SIZE) {
            char page[TARGET_PAGE_SIZE];
            int error;

            /*
             * Read in page from target process memory and
             * write it to coredump file.
             */
            error = copy_from_user(page, addr, sizeof(page));
            if (error != 0) {
                (void) fprintf(stderr, "unable to dump " TARGET_ABI_FMT_lx "\n",
                        addr);
                errno = -error;
                goto out;
            }
            if (dump_write(fd, page, TARGET_PAGE_SIZE) < 0) {
                goto out;
            }
        }
    }
    errno = 0;

out:
    if (mm != NULL) {
        vma_delete(mm);
    }

    (void)close(fd);

    if (errno != 0) {
        return -errno;
    }
    return 0;
}

#endif /* USE_ELF_CORE_DUMP */
