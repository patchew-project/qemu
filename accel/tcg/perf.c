/*
 * Linux perf perf-<pid>.map and jit-<pid>.dump integration.
 *
 * The jitdump spec can be found at [1].
 *
 * [1] https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/plain/tools/perf/Documentation/jitdump-specification.txt
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "elf.h"
#include "qemu/timer.h"
#include "tcg/tcg.h"

#include "debuginfo.h"
#include "perf.h"

static FILE *safe_fopen_w(const char *path)
{
    int saved_errno;
    FILE *f;
    int fd;

    /* Delete the old file, if any. */
    unlink(path);

    /* Avoid symlink attacks by using O_CREAT | O_EXCL. */
    fd = open(path, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        return NULL;
    }

    /* Convert fd to FILE*. */
    f = fdopen(fd, "w");
    if (f == NULL) {
        saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return NULL;
    }

    return f;
}

static FILE *perfmap;

void perf_enable_perfmap(void)
{
    char map_file[32];

    snprintf(map_file, sizeof(map_file), "/tmp/perf-%d.map", getpid());
    perfmap = safe_fopen_w(map_file);
    if (perfmap == NULL) {
        warn_report("Could not open %s: %s, proceeding without perfmap",
                     map_file, strerror(errno));
    }
}

static FILE *jitdump;

#define JITHEADER_MAGIC 0x4A695444
#define JITHEADER_VERSION 1

struct jitheader {
    uint32_t magic;
    uint32_t version;
    uint32_t total_size;
    uint32_t elf_mach;
    uint32_t pad1;
    uint32_t pid;
    uint64_t timestamp;
    uint64_t flags;
};

enum jit_record_type {
    JIT_CODE_LOAD = 0,
    JIT_CODE_DEBUG_INFO = 2,
};

struct jr_prefix {
    uint32_t id;
    uint32_t total_size;
    uint64_t timestamp;
};

struct jr_code_load {
    struct jr_prefix p;

    uint32_t pid;
    uint32_t tid;
    uint64_t vma;
    uint64_t code_addr;
    uint64_t code_size;
    uint64_t code_index;
};

struct debug_entry {
    uint64_t addr;
    int lineno;
    int discrim;
    const char name[];
};

struct jr_code_debug_info {
    struct jr_prefix p;

    uint64_t code_addr;
    uint64_t nr_entry;
    struct debug_entry entries[];
};

static uint32_t get_e_machine(void)
{
    Elf64_Ehdr elf_header;
    FILE *exe;
    size_t n;

    QEMU_BUILD_BUG_ON(offsetof(Elf32_Ehdr, e_machine) !=
                      offsetof(Elf64_Ehdr, e_machine));

    exe = fopen("/proc/self/exe", "r");
    if (exe == NULL) {
        return EM_NONE;
    }

    n = fread(&elf_header, sizeof(elf_header), 1, exe);
    fclose(exe);
    if (n != 1) {
        return EM_NONE;
    }

    return elf_header.e_machine;
}

void perf_enable_jitdump(void)
{
    struct jitheader header;
    char jitdump_file[32];
#ifdef CONFIG_LINUX
    void *perf_marker;
#endif

    if (!use_rt_clock) {
        warn_report("CLOCK_MONOTONIC is not available, proceeding without jitdump");
        return;
    }

    snprintf(jitdump_file, sizeof(jitdump_file), "jit-%d.dump", getpid());
    jitdump = safe_fopen_w(jitdump_file);
    if (jitdump == NULL) {
        warn_report("Could not open %s: %s, proceeding without jitdump",
                     jitdump_file, strerror(errno));
        return;
    }

#ifdef CONFIG_LINUX
    /*
     * `perf inject` will see that the mapped file name in the corresponding
     * PERF_RECORD_MMAP or PERF_RECORD_MMAP2 event is of the form jit-%d.dump
     * and will process it as a jitdump file.
     */
    perf_marker = mmap(NULL, qemu_real_host_page_size(), PROT_READ | PROT_EXEC,
                       MAP_PRIVATE, fileno(jitdump), 0);
    if (perf_marker == MAP_FAILED) {
        warn_report("Could not map %s: %s, proceeding without jitdump",
                     jitdump_file, strerror(errno));
        fclose(jitdump);
        jitdump = NULL;
        return;
    }
#endif

    header.magic = JITHEADER_MAGIC;
    header.version = JITHEADER_VERSION;
    header.total_size = sizeof(header);
    header.elf_mach = get_e_machine();
    header.pad1 = 0;
    header.pid = getpid();
    header.timestamp = get_clock();
    header.flags = 0;
    fwrite(&header, sizeof(header), 1, jitdump);
}

void perf_report_prologue(const void *start, size_t size)
{
    if (perfmap) {
        fprintf(perfmap, "%"PRIxPTR" %zx tcg-prologue-buffer\n",
                (uintptr_t)start, size);
    }
}

/*
 * Append a single line mapping to a JIT_CODE_DEBUG_INFO jitdump entry.
 * Return 1 on success, 0 if there is no line number information for guest_pc.
 */
static int append_debug_entry(GArray *raw, const void *host_pc,
                              target_ulong guest_pc)
{
    struct debug_entry ent;
    const char *file;
    int line;

    if (!debuginfo_get_line(guest_pc, &file, &line)) {
        return 0;
    }

    ent.addr = (uint64_t)host_pc;
    ent.lineno = line;
    ent.discrim = 0;
    g_array_append_vals(raw, &ent, sizeof(ent));
    g_array_append_vals(raw, file, strlen(file) + 1);
    return 1;
}

/* Write a JIT_CODE_DEBUG_INFO jitdump entry. */
static void write_jr_code_debug_info(const void *start, size_t size,
                                     int icount)
{
    GArray *raw = g_array_new(false, false, 1);
    struct jr_code_debug_info rec;
    struct debug_entry ent;
    target_ulong guest_pc;
    const void *host_pc;
    int insn;

    /* Reserve space for the header. */
    g_array_set_size(raw, sizeof(rec));

    /* Create debug entries. */
    rec.nr_entry = 0;
    for (insn = 0; insn < icount; insn++) {
        host_pc = start;
        if (insn != 0) {
            host_pc += tcg_ctx->gen_insn_end_off[insn - 1];
        }
        guest_pc = tcg_ctx->gen_insn_data[insn][0];
        rec.nr_entry += append_debug_entry(raw, host_pc, guest_pc);
    }

    /* Trailing debug_entry. */
    ent.addr = (uint64_t)start + size;
    ent.lineno = 0;
    ent.discrim = 0;
    g_array_append_vals(raw, &ent, sizeof(ent));
    g_array_append_vals(raw, "", 1);
    rec.nr_entry++;

    /* Create header. */
    rec.p.id = JIT_CODE_DEBUG_INFO;
    rec.p.total_size = raw->len;
    rec.p.timestamp = get_clock();
    rec.code_addr = (uint64_t)start;
    memcpy(raw->data, &rec, sizeof(rec));

    /* Flush. */
    fwrite(raw->data, raw->len, 1, jitdump);
    g_array_unref(raw);
}

/* Write a JIT_CODE_LOAD jitdump entry. */
static void write_jr_code_load(const void *start, size_t size,
                               const char *symbol, const char *suffix)
{
    static uint64_t code_index;
    struct jr_code_load rec;
    size_t suffix_size;
    size_t name_size;

    name_size = strlen(symbol);
    suffix_size = strlen(suffix) + 1;
    rec.p.id = JIT_CODE_LOAD;
    rec.p.total_size = sizeof(rec) + name_size + suffix_size + size;
    rec.p.timestamp = get_clock();
    rec.pid = getpid();
    rec.tid = gettid();
    rec.vma = (uint64_t)start;
    rec.code_addr = (uint64_t)start;
    rec.code_size = size;
    rec.code_index = code_index++;
    fwrite(&rec, sizeof(rec), 1, jitdump);
    fwrite(symbol, name_size, 1, jitdump);
    fwrite(suffix, suffix_size, 1, jitdump);
    fwrite(start, size, 1, jitdump);
}

void perf_report_code(const void *start, size_t size, int icount, uint64_t pc)
{
    char suffix[32] = "";
    char symbol_buf[32];
    const char *symbol;
    unsigned long long offset;

    /* Symbolize guest PC. */
    if (perfmap || jitdump) {
        if (!debuginfo_get_symbol(pc, &symbol, &offset)) {
            snprintf(symbol_buf, sizeof(symbol_buf), "subject-%"PRIx64, pc);
            symbol = symbol_buf;
            offset = 0;
        }
        if (offset != 0) {
            snprintf(suffix, sizeof(suffix), "+0x%"PRIx64, (uint64_t)offset);
        }
    }

    /* Emit a perfmap entry if needed. */
    if (perfmap) {
        flockfile(perfmap);
        fprintf(perfmap, "%"PRIxPTR" %zx %s%s\n",
                (uintptr_t)start, size, symbol, suffix);
        funlockfile(perfmap);
    }

    /* Emit jitdump entries if needed. */
    if (jitdump) {
        flockfile(jitdump);
        write_jr_code_debug_info(start, size, icount);
        write_jr_code_load(start, size, symbol, suffix);
        funlockfile(jitdump);
    }
}

void perf_exit(void)
{
    if (perfmap) {
        fclose(perfmap);
        perfmap = NULL;
    }

    if (jitdump) {
        fclose(jitdump);
        jitdump = NULL;
    }
}
