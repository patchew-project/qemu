/*
 * General Virtual-Device Fuzzing Target
 *
 * Copyright Red Hat Inc., 2020
 *
 * Authors:
 *  Alexander Bulekov   <alxndr@bu.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include <wordexp.h>

#include "cpu.h"
#include "tests/qtest/libqtest.h"
#include "fuzz.h"
#include "fork_fuzz.h"
#include "exec/address-spaces.h"
#include "string.h"
#include "exec/memory.h"
#include "exec/ramblock.h"
#include "exec/address-spaces.h"
#include "hw/qdev-core.h"

/*
 * CMD_SEP is a random 32-bit value used to separate "commands" in the fuzz
 * input
 */
#define CMD_SEP "\x84\x05\x5C\x5E"
#define DEFAULT_TIMEOUT_US 100000

typedef struct {
    size_t addr;
    size_t len; /* The number of bytes until the end of the I/O region */
} address_range;

static useconds_t timeout = 100000;
/*
 * List of memory regions that are children of QOM objects specified by the
 * user for fuzzing.
 */
static GPtrArray *fuzzable_memoryregions;
/*
 * Here we want to convert a fuzzer-provided [io-region-index, offset] to
 * a physical address. To do this, we iterate over all of the matched
 * MemoryRegions. Check whether each region exists within the particular io
 * space. Return the absolute address of the offset within the index'th region
 * that is a subregion of the io_space and the distance until the end of the
 * memory region.
 */
static bool get_io_address(address_range *result,
                                    MemoryRegion *io_space,
                                    uint8_t index,
                                    uint32_t offset) {
    MemoryRegion *mr, *root;
    index = index % fuzzable_memoryregions->len;
    int candidate_regions = 0;
    int i = 0;
    int ind = index;
    size_t abs_addr;

    while (ind >= 0 && fuzzable_memoryregions->len) {
        *result = (address_range){0, 0};
        mr = g_ptr_array_index(fuzzable_memoryregions, i);
        if (mr->enabled) {
            abs_addr = mr->addr;
            for (root = mr; root->container; ) {
                root = root->container;
                abs_addr += root->addr;
            }
            /*
             * Only consider the region if it is rooted at the io_space we want
             */
            if (root == io_space) {
                ind--;
                candidate_regions++;
                result->addr = abs_addr + (offset % mr->size);
                result->len = mr->size - (offset % mr->size);
            }
        }
        ++i;
        /* Loop around */
        if (i == fuzzable_memoryregions->len) {
            /* No enabled regions in our io_space? */
            if (candidate_regions == 0) {
                break;
            }
            i = 0;
        }
    }
    return candidate_regions != 0;
}
static bool get_pio_address(address_range *result,
                                     uint8_t index, uint16_t offset)
{
    /*
     * PIO BARs can be set past the maximum port address (0xFFFF). Thus, result
     * can contain an addr that extends past the PIO space. When we pass this
     * address to qtest_in/qtest_out, it is cast to a uint16_t, so we might end
     * up fuzzing a completely different MemoryRegion/Device. Therefore, check
     * that the address here is within the PIO space limits.
     */

    bool success = get_io_address(result, get_system_io(), index, offset);
    return success && result->addr <= 0xFFFF;
}
static bool get_mmio_address(address_range *result,
                                      uint8_t index, uint32_t offset)
{
    return get_io_address(result, get_system_memory(), index, offset);
}

static void op_in(QTestState *s, const unsigned char * data, size_t len)
{
    enum Sizes {Byte, Word, Long, end_sizes};
    struct {
        uint8_t size;
        uint8_t base;
        uint16_t offset;
    } a;
    address_range abs;

    if (len < sizeof(a)) {
        return;
    }
    memcpy(&a, data, sizeof(a));
    if (get_pio_address(&abs, a.base, a.offset) == 0) {
        return;
    }

    switch (a.size %= end_sizes) {
    case Byte:
        qtest_inb(s, abs.addr);
        break;
    case Word:
        if (abs.len >= 2) {
            qtest_inw(s, abs.addr);
        }
        break;
    case Long:
        if (abs.len >= 4) {
            qtest_inl(s, abs.addr);
        }
        break;
    }
}

static void op_out(QTestState *s, const unsigned char * data, size_t len)
{
    enum Sizes {Byte, Word, Long, end_sizes};
    struct {
        uint8_t size;
        uint8_t base;
        uint16_t offset;
        uint32_t value;
    } a;
    address_range abs;

    if (len < sizeof(a)) {
        return;
    }
    memcpy(&a, data, sizeof(a));

    if (get_pio_address(&abs, a.base, a.offset) == 0) {
        return;
    }

    switch (a.size %= end_sizes) {
    case Byte:
        qtest_outb(s, abs.addr, a.value & 0xFF);
        break;
    case Word:
        if (abs.len >= 2) {
            qtest_outw(s, abs.addr, a.value & 0xFFFF);
        }
        break;
    case Long:
        if (abs.len >= 4) {
            qtest_outl(s, abs.addr, a.value);
        }
        break;
    }
}

static void op_read(QTestState *s, const unsigned char * data, size_t len)
{
    enum Sizes {Byte, Word, Long, Quad, end_sizes};
    struct {
        uint8_t size;
        uint8_t base;
        uint32_t offset;
    } a;
    address_range abs;

    if (len < sizeof(a)) {
        return;
    }
    memcpy(&a, data, sizeof(a));

    if (get_mmio_address(&abs, a.base, a.offset) == 0) {
        return;
    }

    switch (a.size %= end_sizes) {
    case Byte:
        qtest_readb(s, abs.addr);
        break;
    case Word:
        if (abs.len >= 2) {
            qtest_readw(s, abs.addr);
        }
        break;
    case Long:
        if (abs.len >= 4) {
            qtest_readl(s, abs.addr);
        }
        break;
    case Quad:
        if (abs.len >= 8) {
            qtest_readq(s, abs.addr);
        }
        break;
    }
}

static void op_write(QTestState *s, const unsigned char * data, size_t len)
{
    enum Sizes {Byte, Word, Long, Quad, end_sizes};
    struct {
        uint8_t size;
        uint8_t base;
        uint32_t offset;
        uint64_t value;
    } a;
    address_range abs;

    if (len < sizeof(a)) {
        return;
    }
    memcpy(&a, data, sizeof(a));

    if (get_mmio_address(&abs, a.base, a.offset) == 0) {
        return;
    }

    switch (a.size %= end_sizes) {
    case Byte:
            qtest_writeb(s, abs.addr, a.value & 0xFF);
        break;
    case Word:
        if (abs.len >= 2) {
            qtest_writew(s, abs.addr, a.value & 0xFFFF);
        }
        break;
    case Long:
        if (abs.len >= 4) {
            qtest_writel(s, abs.addr, a.value & 0xFFFFFFFF);
        }
        break;
    case Quad:
        if (abs.len >= 8) {
            qtest_writeq(s, abs.addr, a.value);
        }
        break;
    }
}
static void op_clock_step(QTestState *s, const unsigned char *data, size_t len)
{
    qtest_clock_step_next(s);
}

static void handle_timeout(int sig)
{
    if (getenv("QTEST_LOG")) {
        fprintf(stderr, "[Timeout]\n");
        fflush(stderr);
    }
    _Exit(0);
}

/*
 * Here, we interpret random bytes from the fuzzer, as a sequence of commands.
 * Our commands are variable-width, so we use a separator, CMD_SEP, to specify
 * the boundaries between commands. This is just a random 32-bit value, which
 * is easily identified by libfuzzer+AddressSanitizer, as long as we use
 * memmem. It can also be included in the fuzzer's dictionary. More details
 * here:
 * https://github.com/google/fuzzing/blob/master/docs/split-inputs.md
 *
 * As a result, the stream of bytes is converted into a sequence of commands.
 * In a simplified example where CMD_SEP is 0xFF:
 * 00 01 02 FF 03 04 05 06 FF 01 FF ...
 * becomes this sequence of commands:
 * 00 01 02    -> op00 (0102)   -> in (0102, 2)
 * 03 04 05 06 -> op03 (040506) -> write (040506, 3)
 * 01          -> op01 (-,0)    -> out (-,0)
 * ...
 *
 * Note here that it is the job of the individual opcode functions to check
 * that enough data was provided. I.e. in the last command out (,0), out needs
 * to check that there is not enough data provided to select an address/value
 * for the operation.
 */
static void general_fuzz(QTestState *s, const unsigned char *Data, size_t Size)
{
    void (*ops[]) (QTestState *s, const unsigned char* , size_t) = {
        op_in,
        op_out,
        op_read,
        op_write,
        op_clock_step,
    };
    const unsigned char *cmd = Data;
    const unsigned char *nextcmd;
    size_t cmd_len;
    uint8_t op;

    if (fork() == 0) {
        /*
         * Sometimes the fuzzer will find inputs that take quite a long time to
         * process. Often times, these inputs do not result in new coverage.
         * Even if these inputs might be interesting, they can slow down the
         * fuzzer, overall. Set a timeout to avoid hurting performance, too much
         */
        if (timeout) {
            struct sigaction sact;
            sigemptyset(&sact.sa_mask);
            sact.sa_flags = 0;
            sact.sa_handler = handle_timeout;
            sigaction(SIGALRM, &sact, NULL);
            ualarm(timeout, 0);
        }

        while (cmd && Size) {
            /* Get the length until the next command or end of input */
            nextcmd = memmem(cmd, Size, CMD_SEP, strlen(CMD_SEP));
            cmd_len = nextcmd ? nextcmd - cmd : Size;

            if (cmd_len > 0) {
                /* Interpret the first byte of the command as an opcode */
                op = *cmd % (sizeof(ops) / sizeof((ops)[0]));
                ops[op](s, cmd + 1, cmd_len - 1);

                /* Run the main loop */
                flush_events(s);
            }
            /* Advance to the next command */
            cmd = nextcmd ? nextcmd + sizeof(CMD_SEP) - 1 : nextcmd;
            Size = Size - (cmd_len + sizeof(CMD_SEP) - 1);
        }
        _Exit(0);
    } else {
        flush_events(s);
        wait(NULL);
    }
}

static void usage(void)
{
    printf("Please specify the following environment variables:\n");
    printf("QEMU_FUZZ_ARGS= the command line arguments passed to qemu\n");
    printf("QEMU_FUZZ_OBJECTS= "
            "a space separated list of QOM type names for objects to fuzz\n");
    printf("Optionally: QEMU_FUZZ_TIMEOUT= Specify a custom timeout (us). "
            "0 to disable. %d by default\n", timeout);
    exit(0);
}

static int locate_fuzz_memory_regions(Object *child, void *opaque)
{
    const char *name;
    MemoryRegion *mr;
    if (object_dynamic_cast(child, TYPE_MEMORY_REGION)) {
        mr = MEMORY_REGION(child);
        if ((memory_region_is_ram(mr) ||
            memory_region_is_ram_device(mr) ||
            memory_region_is_rom(mr) ||
            memory_region_is_romd(mr)) == false) {
            name = object_get_canonical_path_component(child);
            /*
             * We don't want duplicate pointers to the same MemoryRegion, so
             * try to remove copies of the pointer, before adding it.
             */
            g_ptr_array_remove_fast(fuzzable_memoryregions, mr);
            g_ptr_array_add(fuzzable_memoryregions, mr);
        }
    }
    return 0;
}
static int locate_fuzz_objects(Object *child, void *opaque)
{
    char *pattern = opaque;
    if (g_pattern_match_simple(pattern, object_get_typename(child))) {
        printf("Matched Object by Type: %s\n", object_get_typename(child));
        /* Find and save ptrs to any child MemoryRegions */
        object_child_foreach_recursive(child, locate_fuzz_memory_regions, NULL);
    } else if (object_dynamic_cast(OBJECT(child), TYPE_MEMORY_REGION)) {
        if (g_pattern_match_simple(pattern,
            object_get_canonical_path_component(child))) {
            MemoryRegion *mr;
            mr = MEMORY_REGION(child);
            if ((memory_region_is_ram(mr) ||
                 memory_region_is_ram_device(mr) ||
                 memory_region_is_rom(mr) ||
                 memory_region_is_romd(mr)) == false) {
                g_ptr_array_remove_fast(fuzzable_memoryregions, mr);
                g_ptr_array_add(fuzzable_memoryregions, mr);
            }
        }
    }
    return 0;
}

static void general_pre_fuzz(QTestState *s)
{
    if (!getenv("QEMU_FUZZ_OBJECTS")) {
        usage();
    }
    if (getenv("QEMU_FUZZ_TIMEOUT")) {
        timeout = g_ascii_strtoll(getenv("QEMU_FUZZ_TIMEOUT"), NULL, 0);
    }

    fuzzable_memoryregions = g_ptr_array_new();
    wordexp_t result;
    wordexp(getenv("QEMU_FUZZ_OBJECTS"), &result, 0);
    for (int i = 0; i < result.we_wordc; i++) {
        object_child_foreach_recursive(qdev_get_machine(),
                                    locate_fuzz_objects,
                                    result.we_wordv[i]);
    }

    printf("This process will try to fuzz the following MemoryRegions:\n");
    for (int i = 0; i < fuzzable_memoryregions->len; i++) {
        MemoryRegion *mr;
        mr = g_ptr_array_index(fuzzable_memoryregions, i);
        printf("  * %s (size %lx)\n",
               object_get_canonical_path_component(&(mr->parent_obj)),
               mr->addr);
    }
    counter_shm_init();
}
static GString *general_fuzz_cmdline(FuzzTarget *t)
{
    GString *cmd_line = g_string_new(TARGET_NAME);
    if (!getenv("QEMU_FUZZ_ARGS")) {
        usage();
    }
    g_string_append_printf(cmd_line, " -display none \
                                      -machine accel=qtest, \
                                      -m 64 %s ", getenv("QEMU_FUZZ_ARGS"));
    return cmd_line;
}

static void register_general_fuzz_targets(void)
{
    fuzz_add_target(&(FuzzTarget){
            .name = "general-fuzz",
            .description = "Fuzz based on any qemu command-line args. ",
            .get_init_cmdline = general_fuzz_cmdline,
            .pre_fuzz = general_pre_fuzz,
            .fuzz = general_fuzz});
}

fuzz_target_init(register_general_fuzz_targets);
