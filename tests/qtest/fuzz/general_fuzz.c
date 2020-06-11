/*
 * General Fuzzing Target
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

#include "cpu.h"
#include "standard-headers/linux/virtio_config.h"
#include "tests/qtest/libqtest.h"
#include "tests/qtest/libqos/virtio-net.h"
#include "fuzz.h"
#include "fork_fuzz.h"
#include "qos_fuzz.h"
#include "libqos/pci-pc.h"
#include "string.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "hw/qdev-core.h"
#include "hw/pci/pci_regs.h"
#include "hw/boards.h"

/*
 * CMD_SEP is a random 32-bit value used to separate "commands" in the fuzz
 * input
 */
#define CMD_SEP "\x84\x05\x5C\x5E"
#define MAX_DMA_FILL_SIZE 0x10000

typedef struct {
    size_t addr;
    size_t len; /* The number of bytes until the end of the I/O region */
} address_range;

/*
 * A pattern used to populate a DMA region or perform a memwrite. This is
 * useful for e.g. populating tables of unique addresses.
 * Example {.index = 1; .stride = 2; .len = 3; .data = "\x00\x01\x02"}
 * Renders as: 00 01 02   00 03 03   00 05 03   00 07 03 ...
 */
typedef struct {
    uint8_t index;      /* Index of a byte to increment by stride */
    uint8_t stride;     /* Increment each index'th byte by this amount */
    size_t len;
    const uint8_t *data;
} pattern;

/*
 * Only fuzz an IO region if its name contains a word in region_whitelist.
 * Lazy way to limit the fuzzer to a particular device.
 */
char **region_whitelist;

/*
 * List of dma regions populated since the last fuzzing command. Used to ensure
 * that we only write to each DMA address once, to avoid race conditions when
 * building reproducers.
 */
static GArray *dma_regions;

static GArray *dma_patterns;
int dma_pattern_index;

void dma_read_cb(size_t addr, size_t len);

/*
 * Allocate a block of memory and populate it with a pattern.
 */
static void *pattern_alloc(pattern p, size_t len)
{
    int i;
    uint8_t *buf = g_malloc(len);
    uint8_t sum = 0;

    for (i = 0; i < len; ++i) {
        buf[i] = p.data[i % p.len];
        if ((i % p.len) == p.index) {
            buf[i] += sum;
            sum += p.stride;
        }
    }
    return buf;
}

/*
 * Call-back for functions that perform DMA reads from guest memory. Confirm
 * that the region has not already been populated since the last loop in
 * general_fuzz(), avoiding potential race-conditions, which we don't have
 * a good way for reproducing right now.
 */
void dma_read_cb(size_t addr, size_t len)
{
    int i;

    /* Return immediately if we have no data to fill the dma region */
    if (dma_patterns->len == 0) {
        return;
    }

    /* Return immediately if the address is greater than the RAM size */
    if (addr > current_machine->ram_size) {
        return;
    }

    /* Cap the length of the DMA access to something reasonable */
    len = MIN(len, MAX_DMA_FILL_SIZE);

    /*
     * If we overlap with any existing dma_regions, split the range and only
     * populate the non-overlapping parts.
     */
    for (i = 0; i < dma_regions->len; ++i) {
        address_range *region = &g_array_index(dma_regions, address_range, i);
        if (addr < region->addr + region->len && addr + len > region->addr) {
            if (addr < region->addr) {
                dma_read_cb(addr, region->addr - addr);
            }
            if (addr + len > region->addr + region->len) {
                dma_read_cb(region->addr + region->len,
                        addr + len - (region->addr + region->len));
            }
            return;
        }
    }

    /*
     * Otherwise, populate the region using address_space_write_rom to avoid
     * writing to any IO MemoryRegions
     */
    address_range ar = {addr, len};
    g_array_append_val(dma_regions, ar);
    void *buf = pattern_alloc(g_array_index(dma_patterns, pattern,
                              dma_pattern_index), ar.len);
    address_space_write_rom(first_cpu->as, ar.addr, MEMTXATTRS_UNSPECIFIED,
                            buf, ar.len);
    free(buf);

    /* Increment the index of the pattern for the next DMA access */
    dma_pattern_index = (dma_pattern_index + 1) % dma_patterns->len;
}

/*
 * Here we want to convert a fuzzer-provided [io-region-index, offset] to
 * a physical address.
 */
static address_range get_io_address(MemoryRegion *io,  uint8_t index,
                                    uint16_t offset, bool root) {
    /* The index of the candidate MemoryRegions iterated in preorder */
    static int i;
    MemoryRegion *child, *mr = NULL;
    /*
     * This loop should run at most twice:
     * 1.) if index > num regions, to calculate num regions to calculate index
     * % num_regions.
     * 2.) to actually select the mr.
     */
    while (!mr) {
        /* If we are recursing over a subregion, don't reset i */
        if (root) {
            i = 0;
        }
        QTAILQ_FOREACH(child, &io->subregions, subregions_link) {
            int found = *region_whitelist ? 0 : 1;
            char **wl_ptr = region_whitelist;
            while (*wl_ptr != NULL) {
                if (strstr(child->name, *wl_ptr) != NULL) {
                    found = 1;
                    break;
                }
                wl_ptr++;
            }
            if (found) {
                if (index == i++) {
                    mr = child;
                    break;
                }
            }
            address_range addr = get_io_address(child, index, offset, false);
            if (addr.addr != -1) {
                return (address_range){child->addr + addr.addr, addr.len};
            }
        }
        if (!mr) {
            if (i == 0 || !root) {
                return (address_range){-1, 0};
            }
            index = index % i;
        }
    }
    if (mr->size == 0) {
        return (address_range){mr->addr, 0};
    } else {
        return (address_range){mr->addr + (offset % mr->size),
                               mr->size - (offset % mr->size)};
    }
}

static address_range get_pio_address(uint8_t index, uint16_t offset)
{
    return get_io_address(get_system_io(), index, offset, true);
}
static address_range get_mmio_address(uint8_t index, uint16_t offset)
{
    return get_io_address(get_system_memory(), index, offset, true);
}

static void op_in(QTestState *s, const unsigned char * data, size_t len)
{
    enum Sizes {Byte, Word, Long, end_sizes};
    struct {
        uint8_t size;
        uint8_t base;
        uint16_t offset;
    } a;

    if (len < sizeof(a)) {
        return;
    }
    memcpy(&a, data, sizeof(a));

    size_t addr = get_pio_address(a.base, a.offset).addr;
    switch (a.size %= end_sizes) {
    case Byte:
        qtest_inb(s, addr);
        break;
    case Word:
        qtest_inw(s, addr);
        break;
    case Long:
        qtest_inl(s, addr);
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

    if (len < sizeof(a)) {
        return;
    }
    memcpy(&a, data, sizeof(a));

    size_t addr = get_pio_address(a.base, a.offset).addr;
    if (addr == -1) {
        return;
    }
    switch (a.size %= end_sizes) {
    case Byte:
        qtest_outb(s, addr, a.value & 0xFF);
        break;
    case Word:
        qtest_outw(s, addr, a.value & 0xFFFF);
        break;
    case Long:
        qtest_outl(s, addr, a.value);
        break;
    }
}

static void op_read(QTestState *s, const unsigned char * data, size_t len)
{
    enum Sizes {Byte, Word, Long, Quad, end_sizes};
    struct {
        uint8_t size;
        uint8_t base;
        uint16_t offset;
    } a;

    if (len < sizeof(a)) {
        return;
    }
    memcpy(&a, data, sizeof(a));

    size_t addr = get_mmio_address(a.base, a.offset).addr;
    if (addr == -1) {
        return;
    }
    switch (a.size %= end_sizes) {
    case Byte:
        qtest_readb(s, addr);
        break;
    case Word:
        qtest_readw(s, addr);
        break;
    case Long:
        qtest_readl(s, addr);
        break;
    case Quad:
        qtest_readq(s, addr);
        break;
    }
}

static void op_write(QTestState *s, const unsigned char * data, size_t len)
{
    enum Sizes {Byte, Word, Long, Quad, end_sizes};
    struct {
        uint8_t size;
        uint8_t base;
        uint16_t offset;
        uint64_t value;
    } a;
    if (len < sizeof(a)) {
        return;
    }
    memcpy(&a, data, sizeof(a));
    size_t addr = get_mmio_address(a.base, a.offset).addr;
    if (addr == -1) {
        return;
    }
    switch (a.size %= end_sizes) {
    case Byte:
        qtest_writeb(s, addr, a.value & 0xFF);
        break;
    case Word:
        qtest_writew(s, addr, a.value & 0xFFFF);
        break;
    case Long:
        qtest_writel(s, addr, a.value & 0xFFFFFFFF);
        break;
    case Quad:
        qtest_writeq(s, addr, a.value);
        break;
    }
}

static void op_add_dma_pattern(QTestState *s,
                               const unsigned char *data, size_t len)
{
    struct {
        /*
         * index and stride can be used to increment the index-th byte of the
         * pattern by the value stride, for each loop of the pattern.
         */
        uint8_t index;
        uint8_t stride;
    } a;

    if (len < sizeof(a) + 1) {
        return;
    }
    memcpy(&a, data, sizeof(a));
    pattern p = {a.index, a.stride, len - sizeof(a), data + sizeof(a)};
    g_array_append_val(dma_patterns, p);
    return;
}

static void op_clear_dma_patterns(QTestState *s,
                                  const unsigned char *data, size_t len)
{
    g_array_set_size(dma_patterns, 0);
}

static void op_write_pattern(QTestState *s, const unsigned char * data,
                             size_t len)
{
    struct {
        uint8_t base;
        uint32_t offset;
        uint16_t length;
        uint8_t index;
        uint8_t stride;
    } a;

    /*  Need at least one byte for the actual pattern */
    if (len < sizeof(a) + 1) {
        return;
    }

    memcpy(&a, data, sizeof(a));
    pattern p = {
        .data = data + sizeof(a),
        .len = len - sizeof(a),
        .index = a.index,
        .stride = a.stride
    };

    address_range addr = get_mmio_address(a.base, a.offset);
    if (addr.addr == -1) {
        return;
    }
    /* Cap the length and make sure it doesn't extend past the IO region. */
    size_t write_length = MIN(MIN(0x1000, a.length), addr.len);

    void *buf = pattern_alloc(p, write_length);
    qtest_memwrite(s, addr.addr, buf, write_length);
    free(buf);
}

static void op_clock_step(QTestState *s, const unsigned char *data, size_t len)
{
    qtest_clock_step_next(s);
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
    void (*ops[]) (QTestState* s, const unsigned char* , size_t) = {
        op_in,
        op_out,
        op_read,
        op_write,
        op_add_dma_pattern,
        op_clear_dma_patterns,
        op_write_pattern,
        op_clock_step
    };
    const unsigned char *cmd = Data;
    const unsigned char *nextcmd;
    size_t cmd_len;
    uint8_t op;
    g_array_set_size(dma_patterns, 0);
    dma_pattern_index = 0;

    if (fork() == 0) {
        while (cmd && Size) {
            g_array_set_size(dma_regions, 0);
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
        flush_events(s);
        _Exit(0);
    } else {
        flush_events(s);
        wait(NULL);
    }
}

/*
 * Adapted from tests/qtest/libqos/pci.c
 */
static void pcidev_foreach_callback(QPCIDevice *dev, int devfn, void *data)
{
    static const int bar_reg_map[] = {
        PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_1, PCI_BASE_ADDRESS_2,
        PCI_BASE_ADDRESS_3, PCI_BASE_ADDRESS_4, PCI_BASE_ADDRESS_5,
    };
    int bar_reg;
    uint32_t addr;
    uint32_t io_type;

    for (int i = 0; i < 6; i++) {
        bar_reg = bar_reg_map[i];
        qpci_config_writel(dev, bar_reg, 0xFFFFFFFF);
        addr = qpci_config_readl(dev, bar_reg);

        io_type = addr & PCI_BASE_ADDRESS_SPACE;
        if (io_type == PCI_BASE_ADDRESS_SPACE_IO) {
            addr &= PCI_BASE_ADDRESS_IO_MASK;
        } else {
            addr &= PCI_BASE_ADDRESS_MEM_MASK;
        }
        if (addr) {
            qpci_iomap(dev, i, NULL);
        }
    }

    qpci_device_enable(dev);
    if (qpci_find_capability(dev, PCI_CAP_ID_MSIX, 0)) {
        qpci_msix_enable(dev);
    }
}


static void general_pre_qos_fuzz(QTestState *s)
{
    if (getenv("FUZZ_REGION_WHITELIST")) {
        region_whitelist = g_strsplit(getenv("FUZZ_REGION_WHITELIST"), " ", 0);
    }
    counter_shm_init();

    dma_regions = g_array_new(false, false, sizeof(address_range));
    dma_patterns = g_array_new(false, false, sizeof(pattern));

    qos_init_path(s);

    /* Enumerate PCI devices and map BARs */
    qpci_device_foreach(fuzz_qos_obj, -1, -1, pcidev_foreach_callback, NULL);
}


static void *qos_general_cmdline(GString *cmd_line, void *arg)
{
    if (!getenv("QEMU_FUZZ_ARGS")) {
        printf("Please specify qemu args for fuzzing with the QEMU_FUZZ_ARGS"
               " environment variable. "
               " (e.g. QEMU_FUZZ_ARGS='-device virtio-net')\n");
        exit(0);
    }
    g_string_append_printf(cmd_line, " %s ", getenv("QEMU_FUZZ_ARGS"));
    return arg;
}

static void register_general_fuzz_targets(void)
{
    fuzz_add_qos_target(&(FuzzTarget){
            .name = "general-pci-enum-fuzz",
            .description = "Fuzz based on any qemu command-line args. "
                           "Try to map all PCI Device BARs. prior to fuzzing",
            .pre_fuzz = &general_pre_qos_fuzz,
            .fuzz = general_fuzz},
            "pci-bus",
            &(QOSGraphTestOptions){.before = qos_general_cmdline}
            );
}

fuzz_target_init(register_general_fuzz_targets);
