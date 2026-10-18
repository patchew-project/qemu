/*
 * QTest testcase for the BitBLT engine of the Cirrus VGA emulation
 *
 * The tests program the BitBLT registers through the ports 0x3ce/0x3cf
 * and check video memory through the linear frame buffer (BAR 0).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "libqtest.h"
#include "libqos/pci.h"
#include "libqos/pci-pc.h"

#define VRAM_SIZE       (4 * MiB)           /* default vgamem_mb=4 */

#define FILL            0x11                /* video memory before a BitBLT */
#define FG              0xee                /* foreground: GR1, GR11 */
#define BG              0x22                /* background: GR0, GR10 */

/* GR30, BLT mode */
#define MODE_SYSSRC     0x04
#define MODE_TRANSP     0x08
#define MODE_16BPP      0x10
#define MODE_PATTERN    0x40
#define MODE_COLOREXP   0x80

/* GR33, BLT mode extensions */
#define MODEEXT_DWORD   0x01

typedef struct {
    QTestState *qts;
    QPCIBus *bus;
    QPCIDevice *dev;
    QPCIBar fb;
} Cirrus;

typedef struct {
    uint8_t mode, modeext;
    int width, height;              /* width in bytes */
    int dstpitch, srcpitch;
    uint32_t dst, src;
} Blt;

static void sr(Cirrus *c, uint8_t index, uint8_t val)
{
    qtest_outb(c->qts, 0x3c4, index);
    qtest_outb(c->qts, 0x3c5, val);
}

static void gr(Cirrus *c, uint8_t index, uint8_t val)
{
    qtest_outb(c->qts, 0x3ce, index);
    qtest_outb(c->qts, 0x3cf, val);
}

static void cirrus_start(Cirrus *c)
{
    c->qts = qtest_init("-vga none -device cirrus-vga,addr=04.0");
    c->bus = qpci_new_pc(c->qts, NULL);
    c->dev = qpci_device_find(c->bus, QPCI_DEVFN(0x4, 0x0));
    g_assert(c->dev);
    qpci_device_enable(c->dev);
    c->fb = qpci_iomap(c->dev, 0, NULL);

    sr(c, 0x06, 0x12);              /* unlock the extension registers */
    sr(c, 0x07, 0x01);              /* extended mode, 8 bpp */
    gr(c, 0x00, BG);
    gr(c, 0x01, FG);
    gr(c, 0x10, BG);
    gr(c, 0x11, FG);
}

static void cirrus_stop(Cirrus *c)
{
    g_free(c->dev);
    qpci_free_pc(c->bus);
    qtest_quit(c->qts);
}

static void vram_write(Cirrus *c, uint32_t addr, const void *buf, size_t len)
{
    qpci_memwrite(c->dev, c->fb, addr, buf, len);
}

static void vram_read(Cirrus *c, uint32_t addr, void *buf, size_t len)
{
    qpci_memread(c->dev, c->fb, addr, buf, len);
}

/* Fill the destination rectangle of @b with FILL */
static void clear_dst(Cirrus *c, const Blt *b)
{
    g_autofree uint8_t *line = g_malloc(b->width);

    memset(line, FILL, b->width);
    for (int y = 0; y < b->height; y++) {
        vram_write(c, b->dst + y * b->dstpitch, line, b->width);
    }
}

static void blt_start(Cirrus *c, const Blt *b)
{
    gr(c, 0x20, (b->width - 1) & 0xff);
    gr(c, 0x21, (b->width - 1) >> 8);
    gr(c, 0x22, (b->height - 1) & 0xff);
    gr(c, 0x23, (b->height - 1) >> 8);
    gr(c, 0x24, b->dstpitch & 0xff);
    gr(c, 0x25, b->dstpitch >> 8);
    gr(c, 0x26, b->srcpitch & 0xff);
    gr(c, 0x27, b->srcpitch >> 8);
    gr(c, 0x28, b->dst & 0xff);
    gr(c, 0x29, (b->dst >> 8) & 0xff);
    gr(c, 0x2a, b->dst >> 16);
    gr(c, 0x2c, b->src & 0xff);
    gr(c, 0x2d, (b->src >> 8) & 0xff);
    gr(c, 0x2e, b->src >> 16);
    gr(c, 0x2f, 0x00);
    gr(c, 0x30, b->mode);
    gr(c, 0x32, 0x0d);              /* ROP: copy the source */
    gr(c, 0x33, b->modeext);
    gr(c, 0x31, 0x02);              /* start */
}

/*
 * Check the destination of a color expansion: line y of the destination
 * shows the bits of bits[y], most significant bit first.  Without
 * transparency, 0 bits are drawn in the background color; with
 * transparency, they leave FILL in place.
 */
static void check_expansion(Cirrus *c, const Blt *b, const uint8_t *bits)
{
    int bpp = b->mode & MODE_16BPP ? 2 : 1;
    g_autofree uint8_t *line = g_malloc(b->width);

    for (int y = 0; y < b->height; y++) {
        vram_read(c, b->dst + y * b->dstpitch, line, b->width);
        for (int x = 0; x < b->width; x++) {
            bool set = bits[y] & (0x80 >> (x / bpp));
            uint8_t expect = set ? FG : (b->mode & MODE_TRANSP) ? FILL : BG;

            g_assert_cmphex(line[x], ==, expect);
        }
    }
}

/*
 * A monochrome pattern starts on an 8-byte boundary, and the three low
 * bits of the source address select its first line.
 */
static void test_mono_pattern_first_line(void)
{
    static const uint8_t pattern[8] = {
        0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01
    };
    Blt b = {
        .mode = MODE_COLOREXP | MODE_PATTERN,
        .width = 8, .height = 8, .dstpitch = 64,
        .dst = 0, .src = 0x10008 + 3,
    };
    uint8_t bits[8];
    Cirrus c;

    cirrus_start(&c);
    vram_write(&c, 0x10008, pattern, sizeof(pattern));
    clear_dst(&c, &b);
    blt_start(&c, &b);
    for (int y = 0; y < 8; y++) {
        bits[y] = pattern[(3 + y) & 7];
    }
    check_expansion(&c, &b, bits);
    cirrus_stop(&c);
}

/*
 * An 8 bpp color pattern has 64 bytes, and the three low bits of the
 * source address select its first line.
 */
static void test_color_pattern_first_line(void)
{
    Blt b = {
        .mode = MODE_PATTERN,
        .width = 8, .height = 8, .dstpitch = 64,
        .dst = 0, .src = 0x10040 + 5,
    };
    uint8_t pattern[64], line[8];
    Cirrus c;

    for (int i = 0; i < 64; i++) {
        pattern[i] = 0x40 + i;
    }
    cirrus_start(&c);
    vram_write(&c, 0x10040, pattern, sizeof(pattern));
    clear_dst(&c, &b);
    blt_start(&c, &b);
    for (int y = 0; y < 8; y++) {
        vram_read(&c, b.dst + y * b.dstpitch, line, sizeof(line));
        g_assert_cmpmem(line, sizeof(line), &pattern[((5 + y) & 7) * 8], 8);
    }
    cirrus_stop(&c);
}

/*
 * With DWORD granularity (GR33 bit 0), each line of color expansion data
 * from the CPU starts with a new dword.
 */
static void test_sysmem_expansion_dword(void)
{
    static const uint8_t bits[4] = { 0xf0, 0x81, 0x3c, 0x55 };
    Blt b = {
        .mode = MODE_COLOREXP | MODE_SYSSRC, .modeext = MODEEXT_DWORD,
        .width = 8, .height = 4, .dstpitch = 64,
        .dst = 0,
    };
    uint8_t data[16];
    Cirrus c;

    /* one byte of pixels per line, then 3 bytes of padding */
    for (int y = 0; y < 4; y++) {
        data[y * 4] = bits[y];
        data[y * 4 + 1] = data[y * 4 + 2] = data[y * 4 + 3] = 0xff;
    }
    cirrus_start(&c);
    clear_dst(&c, &b);
    blt_start(&c, &b);
    vram_write(&c, 0, data, sizeof(data));
    check_expansion(&c, &b, bits);
    cirrus_stop(&c);
}

/*
 * The source pitch has no meaning for a color expansion from video
 * memory: the source is a linear string of bytes.  Check it with a source
 * pitch of 0x40 and of 0, opaque and transparent, in 8 and 16 bpp.
 */
static void test_vram_expansion_pitch(void)
{
    static const uint8_t glyph[8] = {
        0xff, 0x81, 0xbd, 0xa5, 0xa5, 0xbd, 0x81, 0xff
    };
    static const uint8_t modes[] = {
        MODE_COLOREXP,
        MODE_COLOREXP | MODE_TRANSP,
        MODE_COLOREXP | MODE_16BPP,
        MODE_COLOREXP | MODE_16BPP | MODE_TRANSP,
    };
    static const int srcpitches[] = { 0x40, 0 };
    Cirrus c;

    cirrus_start(&c);
    vram_write(&c, 0x4000, glyph, sizeof(glyph));
    for (int i = 0; i < ARRAY_SIZE(modes); i++) {
        for (int j = 0; j < ARRAY_SIZE(srcpitches); j++) {
            Blt b = {
                .mode = modes[i],
                .width = modes[i] & MODE_16BPP ? 16 : 8, .height = 8,
                .dstpitch = 64, .srcpitch = srcpitches[j],
                .dst = 0, .src = 0x4000,
            };

            clear_dst(&c, &b);
            blt_start(&c, &b);
            check_expansion(&c, &b, glyph);
        }
    }
    cirrus_stop(&c);
}

/*
 * QEMU drops a color expansion whose source would run past the end of
 * video memory.
 */
static void test_vram_expansion_beyond_end(void)
{
    static const uint8_t bits[4] = { 0xff, 0xff, 0xff, 0xff };
    Blt b = {
        .mode = MODE_COLOREXP,
        .width = 8, .height = 8, .dstpitch = 64,
        .dst = 0, .src = VRAM_SIZE - 4,
    };
    uint8_t line[8], fill[8];
    Cirrus c;

    memset(fill, FILL, sizeof(fill));
    cirrus_start(&c);
    vram_write(&c, VRAM_SIZE - 4, bits, sizeof(bits));
    clear_dst(&c, &b);
    blt_start(&c, &b);
    for (int y = 0; y < b.height; y++) {
        vram_read(&c, b.dst + y * b.dstpitch, line, sizeof(line));
        g_assert_cmpmem(line, sizeof(line), fill, sizeof(fill));
    }
    cirrus_stop(&c);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    if (!qtest_has_device("cirrus-vga")) {
        return g_test_run();
    }

    qtest_add_func("/cirrus/blt/mono-pattern-first-line",
                   test_mono_pattern_first_line);
    qtest_add_func("/cirrus/blt/color-pattern-first-line",
                   test_color_pattern_first_line);
    qtest_add_func("/cirrus/blt/sysmem-expansion-dword",
                   test_sysmem_expansion_dword);
    qtest_add_func("/cirrus/blt/vram-expansion-pitch",
                   test_vram_expansion_pitch);
    qtest_add_func("/cirrus/blt/vram-expansion-beyond-end",
                   test_vram_expansion_beyond_end);

    return g_test_run();
}
