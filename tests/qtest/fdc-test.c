/*
 * Floppy test cases.
 *
 * Copyright (c) 2012 Kevin Wolf <kwolf@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"


#include "libqtest-single.h"
#include "qobject/qdict.h"

#define DRIVE_FLOPPY_BLANK \
    "-drive if=floppy,file=null-co://,file.read-zeroes=on,format=raw,size=1440k"

#define TEST_IMAGE_1440KB (1440 * 1024)
#define TEST_IMAGE_720KB (720 * 1024)

#define FLOPPY_BASE 0x3f0
#define FLOPPY_IRQ 6

enum {
    reg_sra         = 0x0,
    reg_srb         = 0x1,
    reg_dor         = 0x2,
    reg_msr         = 0x4,
    reg_dsr         = 0x4,
    reg_fifo        = 0x5,
    reg_dir         = 0x7,
};

enum {
    CMD_SENSE_INT           = 0x08,
    CMD_READ_ID             = 0x0a,
    CMD_FORMAT_TRACK        = 0x4d,
    CMD_SEEK                = 0x0f,
    CMD_VERIFY              = 0x16,
    CMD_SAVE                = 0x2e,
    CMD_RESTORE             = 0x4e,
    CMD_READ                = 0xe6,
    CMD_RELATIVE_SEEK_OUT   = 0x8f,
    CMD_RELATIVE_SEEK_IN    = 0xcf,
};

enum {
    BUSY    = 0x10,
    NONDMA  = 0x20,
    RQM     = 0x80,
    DIO     = 0x40,

    DSKCHG  = 0x80,
};
enum {
    ST0_IC_MASK  = 0xc0,    /* interrupt code */
    ST0_IC_ABNTERM = 0x40,  /* abnormal termination */

    ST1_MA       = 0x01,    /* missing address mark */
    ST1_EC       = 0x80,    /* end of cylinder / sector past last_sect */
};

static char *test_image;
static char *test_image_720k;

#define assert_bit_set(data, mask) g_assert_cmphex((data) & (mask), ==, (mask))
#define assert_bit_clear(data, mask) g_assert_cmphex((data) & (mask), ==, 0)

static uint8_t base = 0x70;

enum {
    CMOS_FLOPPY     = 0x10,
};

static void floppy_send(uint8_t byte)
{
    uint8_t msr;

    msr = inb(FLOPPY_BASE + reg_msr);
    assert_bit_set(msr, RQM);
    assert_bit_clear(msr, DIO);

    outb(FLOPPY_BASE + reg_fifo, byte);
}

static uint8_t floppy_recv(void)
{
    uint8_t msr;

    msr = inb(FLOPPY_BASE + reg_msr);
    assert_bit_set(msr, RQM | DIO);

    return inb(FLOPPY_BASE + reg_fifo);
}

/* pcn: Present Cylinder Number */
static void ack_irq(uint8_t *pcn)
{
    uint8_t ret;

    g_assert(get_irq(FLOPPY_IRQ));
    floppy_send(CMD_SENSE_INT);
    floppy_recv();

    ret = floppy_recv();
    if (pcn != NULL) {
        *pcn = ret;
    }

    g_assert(!get_irq(FLOPPY_IRQ));
}

static uint8_t send_read_command(uint8_t cmd)
{
    uint8_t drive = 0;
    uint8_t head = 0;
    uint8_t cyl = 0;
    uint8_t sect_addr = 1;
    uint8_t sect_size = 2;
    uint8_t eot = 1;
    uint8_t gap = 0x1b;
    uint8_t gpl = 0xff;

    uint8_t msr = 0;
    uint8_t st0;

    uint8_t ret = 0;

    floppy_send(cmd);
    floppy_send(head << 2 | drive);
    g_assert(!get_irq(FLOPPY_IRQ));
    floppy_send(cyl);
    floppy_send(head);
    floppy_send(sect_addr);
    floppy_send(sect_size);
    floppy_send(eot);
    floppy_send(gap);
    floppy_send(gpl);

    uint8_t i = 0;
    uint8_t n = 2;
    for (; i < n; i++) {
        msr = inb(FLOPPY_BASE + reg_msr);
        if (msr == 0xd0) {
            break;
        }
        sleep(1);
    }

    if (i >= n) {
        return 1;
    }

    st0 = floppy_recv();
    if (st0 != 0x40) {
        ret = 1;
    }

    floppy_recv();
    floppy_recv();
    floppy_recv();
    floppy_recv();
    floppy_recv();
    floppy_recv();

    return ret;
}

static uint8_t send_read_no_dma_command(int nb_sect, uint8_t expected_st0)
{
    uint8_t drive = 0;
    uint8_t head = 0;
    uint8_t cyl = 0;
    uint8_t sect_addr = 1;
    uint8_t sect_size = 2;
    uint8_t eot = nb_sect;
    uint8_t gap = 0x1b;
    uint8_t gpl = 0xff;

    uint8_t msr = 0;
    uint8_t st0;

    uint8_t ret = 0;

    floppy_send(CMD_READ);
    floppy_send(head << 2 | drive);
    g_assert(!get_irq(FLOPPY_IRQ));
    floppy_send(cyl);
    floppy_send(head);
    floppy_send(sect_addr);
    floppy_send(sect_size);
    floppy_send(eot);
    floppy_send(gap);
    floppy_send(gpl);

    uint16_t i = 0;
    uint8_t n = 2;
    for (; i < n; i++) {
        msr = inb(FLOPPY_BASE + reg_msr);
        if (msr == (BUSY | NONDMA | DIO | RQM)) {
            break;
        }
        sleep(1);
    }

    if (i >= n) {
        return 1;
    }

    /* Non-DMA mode */
    for (i = 0; i < 512 * 2 * nb_sect; i++) {
        msr = inb(FLOPPY_BASE + reg_msr);
        assert_bit_set(msr, BUSY | RQM | DIO);
        inb(FLOPPY_BASE + reg_fifo);
    }

    msr = inb(FLOPPY_BASE + reg_msr);
    assert_bit_set(msr, BUSY | RQM | DIO);
    g_assert(get_irq(FLOPPY_IRQ));

    st0 = floppy_recv();
    if (st0 != expected_st0) {
        ret = 1;
    }

    floppy_recv();
    floppy_recv();
    floppy_recv();
    floppy_recv();
    floppy_recv();
    g_assert(get_irq(FLOPPY_IRQ));
    floppy_recv();

    /* Check that we're back in command phase */
    msr = inb(FLOPPY_BASE + reg_msr);
    assert_bit_clear(msr, BUSY | DIO);
    assert_bit_set(msr, RQM);
    g_assert(!get_irq(FLOPPY_IRQ));

    return ret;
}

static void send_seek(int cyl)
{
    int drive = 0;
    int head = 0;

    floppy_send(CMD_SEEK);
    floppy_send(head << 2 | drive);
    g_assert(!get_irq(FLOPPY_IRQ));
    floppy_send(cyl);
    ack_irq(NULL);
}

static uint8_t cmos_read(uint8_t reg)
{
    outb(base + 0, reg);
    return inb(base + 1);
}

static void test_cmos(void)
{
    uint8_t cmos;

    cmos = cmos_read(CMOS_FLOPPY);
    g_assert(cmos == 0x40 || cmos == 0x50);
}

static void media_insert_path(const char *path)
{
    qtest_qmp_assert_success(global_qtest,
                             "{'execute':'blockdev-change-medium', 'arguments':{"
                             " 'id':'floppy0', 'filename': %s, 'format': 'raw' }}",
                             path);
}

static void media_insert(void)
{
    media_insert_path(test_image);
}

static void media_eject(void)
{
    qtest_qmp_assert_success(global_qtest,
                             "{'execute':'eject', 'arguments':{"
                             " 'id':'floppy0' }}");
}

static void test_no_media_on_start(void)
{
    uint8_t dir;

    /* Media changed bit must be set all time after start if there is
     * no media in drive. */
    dir = inb(FLOPPY_BASE + reg_dir);
    assert_bit_set(dir, DSKCHG);
    dir = inb(FLOPPY_BASE + reg_dir);
    assert_bit_set(dir, DSKCHG);
    send_seek(1);
    dir = inb(FLOPPY_BASE + reg_dir);
    assert_bit_set(dir, DSKCHG);
    dir = inb(FLOPPY_BASE + reg_dir);
    assert_bit_set(dir, DSKCHG);
}

static void test_read_without_media(void)
{
    uint8_t ret;

    ret = send_read_command(CMD_READ);
    g_assert(ret == 0);
}

static void test_media_insert(void)
{
    uint8_t dir;

    /* Insert media in drive. DSKCHK should not be reset until a step pulse
     * is sent. */
    media_insert();

    dir = inb(FLOPPY_BASE + reg_dir);
    assert_bit_set(dir, DSKCHG);
    dir = inb(FLOPPY_BASE + reg_dir);
    assert_bit_set(dir, DSKCHG);

    send_seek(0);
    dir = inb(FLOPPY_BASE + reg_dir);
    assert_bit_set(dir, DSKCHG);
    dir = inb(FLOPPY_BASE + reg_dir);
    assert_bit_set(dir, DSKCHG);

    /* Step to next track should clear DSKCHG bit. */
    send_seek(1);
    dir = inb(FLOPPY_BASE + reg_dir);
    assert_bit_clear(dir, DSKCHG);
    dir = inb(FLOPPY_BASE + reg_dir);
    assert_bit_clear(dir, DSKCHG);
}

static void test_media_change(void)
{
    uint8_t dir;

    test_media_insert();

    /* Eject the floppy and check that DSKCHG is set. Reading it out doesn't
     * reset the bit. */
    media_eject();

    dir = inb(FLOPPY_BASE + reg_dir);
    assert_bit_set(dir, DSKCHG);
    dir = inb(FLOPPY_BASE + reg_dir);
    assert_bit_set(dir, DSKCHG);

    send_seek(0);
    dir = inb(FLOPPY_BASE + reg_dir);
    assert_bit_set(dir, DSKCHG);
    dir = inb(FLOPPY_BASE + reg_dir);
    assert_bit_set(dir, DSKCHG);

    send_seek(1);
    dir = inb(FLOPPY_BASE + reg_dir);
    assert_bit_set(dir, DSKCHG);
    dir = inb(FLOPPY_BASE + reg_dir);
    assert_bit_set(dir, DSKCHG);
}

static void test_sense_interrupt(void)
{
    int drive = 0;
    int head = 0;
    int cyl = 0;
    int ret = 0;

    floppy_send(CMD_SENSE_INT);
    ret = floppy_recv();
    g_assert(ret == 0x80);

    floppy_send(CMD_SEEK);
    floppy_send(head << 2 | drive);
    g_assert(!get_irq(FLOPPY_IRQ));
    floppy_send(cyl);

    floppy_send(CMD_SENSE_INT);
    ret = floppy_recv();
    g_assert(ret == 0x20);
    floppy_recv();
}

static void test_relative_seek(void)
{
    uint8_t drive = 0;
    uint8_t head = 0;
    uint8_t cyl = 1;
    uint8_t pcn;

    /* Send seek to track 0 */
    send_seek(0);

    /* Send relative seek to increase track by 1 */
    floppy_send(CMD_RELATIVE_SEEK_IN);
    floppy_send(head << 2 | drive);
    g_assert(!get_irq(FLOPPY_IRQ));
    floppy_send(cyl);

    ack_irq(&pcn);
    g_assert(pcn == 1);

    /* Send relative seek to decrease track by 1 */
    floppy_send(CMD_RELATIVE_SEEK_OUT);
    floppy_send(head << 2 | drive);
    g_assert(!get_irq(FLOPPY_IRQ));
    floppy_send(cyl);

    ack_irq(&pcn);
    g_assert(pcn == 0);
}

static void test_read_id(void)
{
    uint8_t drive = 0;
    uint8_t head = 0;
    uint8_t cyl;
    uint8_t st0;
    uint8_t msr;

    /* READ ID reads an address mark, so it needs a medium in the drive. */
    media_insert();

    /* Seek to track 0 and check with READ ID */
    send_seek(0);

    floppy_send(CMD_READ_ID);
    g_assert(!get_irq(FLOPPY_IRQ));
    floppy_send(head << 2 | drive);

    msr = inb(FLOPPY_BASE + reg_msr);
    if (!get_irq(FLOPPY_IRQ)) {
        assert_bit_set(msr, BUSY);
        assert_bit_clear(msr, RQM);
    }

    while (!get_irq(FLOPPY_IRQ)) {
        /* qemu involves a timer with READ ID... */
        clock_step(1000000000LL / 50);
    }

    msr = inb(FLOPPY_BASE + reg_msr);
    assert_bit_set(msr, BUSY | RQM | DIO);

    st0 = floppy_recv();
    floppy_recv();
    floppy_recv();
    cyl = floppy_recv();
    head = floppy_recv();
    floppy_recv();
    g_assert(get_irq(FLOPPY_IRQ));
    floppy_recv();
    g_assert(!get_irq(FLOPPY_IRQ));

    g_assert_cmpint(cyl, ==, 0);
    g_assert_cmpint(head, ==, 0);
    g_assert_cmpint(st0, ==, head << 2);

    /* Seek to track 8 on head 1 and check with READ ID */
    head = 1;
    cyl = 8;

    floppy_send(CMD_SEEK);
    floppy_send(head << 2 | drive);
    g_assert(!get_irq(FLOPPY_IRQ));
    floppy_send(cyl);
    g_assert(get_irq(FLOPPY_IRQ));
    ack_irq(NULL);

    floppy_send(CMD_READ_ID);
    g_assert(!get_irq(FLOPPY_IRQ));
    floppy_send(head << 2 | drive);

    msr = inb(FLOPPY_BASE + reg_msr);
    if (!get_irq(FLOPPY_IRQ)) {
        assert_bit_set(msr, BUSY);
        assert_bit_clear(msr, RQM);
    }

    while (!get_irq(FLOPPY_IRQ)) {
        /* qemu involves a timer with READ ID... */
        clock_step(1000000000LL / 50);
    }

    msr = inb(FLOPPY_BASE + reg_msr);
    assert_bit_set(msr, BUSY | RQM | DIO);

    st0 = floppy_recv();
    floppy_recv();
    floppy_recv();
    cyl = floppy_recv();
    head = floppy_recv();
    floppy_recv();
    g_assert(get_irq(FLOPPY_IRQ));
    floppy_recv();
    g_assert(!get_irq(FLOPPY_IRQ));

    g_assert_cmpint(cyl, ==, 8);
    g_assert_cmpint(head, ==, 1);
    g_assert_cmpint(st0, ==, head << 2);

    /* Leave the drive empty, the way the machine starts up. */
    media_eject();
}

/*
 * An empty drive spins no diskette, so READ ID finds no address mark and must
 * terminate abnormally.  Reporting success (with a made-up sector ID) would
 * tell the guest that a medium is still present after it has been ejected.
 */
static void test_read_id_no_media(void)
{
    uint8_t drive = 0;
    uint8_t head = 0;
    uint8_t st0, st1;

    floppy_send(CMD_READ_ID);
    g_assert(!get_irq(FLOPPY_IRQ));
    floppy_send(head << 2 | drive);

    while (!get_irq(FLOPPY_IRQ)) {
        clock_step(1000000000LL / 50);
    }

    st0 = floppy_recv();
    st1 = floppy_recv();
    floppy_recv();                  /* ST2 */
    floppy_recv();                  /* cylinder */
    floppy_recv();                  /* head */
    floppy_recv();                  /* sector */
    g_assert(get_irq(FLOPPY_IRQ));
    floppy_recv();                  /* sector size */
    g_assert(!get_irq(FLOPPY_IRQ));

    g_assert_cmpint(st0 & ST0_IC_MASK, ==, ST0_IC_ABNTERM);
    g_assert_cmpint(st1 & ST1_MA, ==, ST1_MA);
}

static void test_read_no_dma_1(void)
{
    uint8_t ret;

    outb(FLOPPY_BASE + reg_dor, inb(FLOPPY_BASE + reg_dor) & ~0x08);
    send_seek(0);
    ret = send_read_no_dma_command(1, 0x04);
    g_assert(ret == 0);
}

static void test_read_no_dma_18(void)
{
    uint8_t ret;

    outb(FLOPPY_BASE + reg_dor, inb(FLOPPY_BASE + reg_dor) & ~0x08);
    send_seek(0);
    ret = send_read_no_dma_command(18, 0x04);
    g_assert(ret == 0);
}

static void test_read_no_dma_19(void)
{
    uint8_t ret;

    outb(FLOPPY_BASE + reg_dor, inb(FLOPPY_BASE + reg_dor) & ~0x08);
    send_seek(0);
    ret = send_read_no_dma_command(19, 0x20);
    g_assert(ret == 0);
}

static void test_verify(void)
{
    uint8_t ret;

    ret = send_read_command(CMD_VERIFY);
    g_assert(ret == 0);
}

/*
 * Query cur_drv->last_sect using the SAVE command (CMD_SAVE, 0x2e).
 * Byte 8 of the 15 result bytes returned by CMD_SAVE holds last_sect.
 */
static uint8_t get_lastsect(void)
{
    uint8_t res[15];
    int i;

    floppy_send(CMD_SAVE);
    for (i = 0; i < 15; i++) {
        res[i] = floppy_recv();
    }
    return res[8];
}

/*
 * Attempt to set cur_drv->last_sect directly using the RESTORE command
 * (CMD_RESTORE, 0x4e).
 * While the 82078 datasheet describes RESTORE for restoring a previously
 * saved state, a guest can issue raw RESTORE commands with arbitrary
 * parameters without having issued SAVE. Parameter byte 9 is used by the
 * controller to restore cur_drv->last_sect.
 */
static void fake_lastsect(uint8_t last_sect)
{
    floppy_send(CMD_RESTORE);
    floppy_send(0); /* fifo[1] */
    floppy_send(0); /* fifo[2] */
    floppy_send(0); /* fifo[3]: drv0 track */
    floppy_send(0); /* fifo[4]: drv1 track */
    floppy_send(0); /* fifo[5]: drv2 track */
    floppy_send(0); /* fifo[6]: drv3 track */
    floppy_send(0); /* fifo[7]: timer0 */
    floppy_send(0); /* fifo[8]: timer1 */
    floppy_send(last_sect); /* fifo[9]: last_sect */
    floppy_send(0); /* fifo[10]: lock/perpendicular */
    floppy_send(0); /* fifo[11]: config */
    floppy_send(0); /* fifo[12]: precomp_trk */
    floppy_send(0); /* fifo[13]: pwrd */
    floppy_send(0); /* fifo[14] */
    floppy_send(0); /* fifo[15] */
    floppy_send(0); /* fifo[16] */
    floppy_send(0); /* fifo[17] */
}

static void send_format_track(uint8_t drive, uint8_t head, uint8_t last_sect,
                              uint8_t *st0_out, uint8_t *st1_out)
{
    uint8_t st0, st1;

    floppy_send(CMD_FORMAT_TRACK);
    floppy_send((head << 2) | drive);
    floppy_send(2);          /* 512 bytes per sector */
    floppy_send(last_sect);  /* sectors per track */
    floppy_send(0x1b);       /* GAP length */
    floppy_send(0x00);       /* filler byte */

    g_assert(get_irq(FLOPPY_IRQ));
    st0 = floppy_recv();
    st1 = floppy_recv();
    floppy_recv(); /* st2 */
    floppy_recv(); /* track */
    floppy_recv(); /* head */
    floppy_recv(); /* sect */
    g_assert(get_irq(FLOPPY_IRQ));
    floppy_recv(); /* sz */
    g_assert(!get_irq(FLOPPY_IRQ));

    if (st0_out) {
        *st0_out = st0;
    }
    if (st1_out) {
        *st1_out = st1;
    }
}

/*
 * Test that guest cannot set last_sect beyond the probed media size
 * via RESTORE or FORMAT TRACK commands (gitlab issue #3800).
 */
static void test_last_sect_bounds(void)
{
    uint8_t st0, st1;

    /* Start with 1.44 MB media inserted (last_sect = 18) */
    media_insert();
    send_seek(1);
    send_seek(0);

    /* Valid last_sect values (<= 18) should succeed */
    fake_lastsect(18);
    g_assert(!get_irq(FLOPPY_IRQ));
    g_assert_cmpint(get_lastsect(), ==, 18);

    fake_lastsect(9);
    g_assert(!get_irq(FLOPPY_IRQ));
    g_assert_cmpint(get_lastsect(), ==, 9);

    /* Restoring to the default 18 */
    fake_lastsect(18);
    g_assert(!get_irq(FLOPPY_IRQ));
    g_assert_cmpint(get_lastsect(), ==, 18);

    /* Invalid last_sect value (> 18) must fail */
    fake_lastsect(19);
    g_assert(get_irq(FLOPPY_IRQ));
    st0 = floppy_recv();
    st1 = floppy_recv();
    floppy_recv(); /* st2 */
    floppy_recv(); /* track */
    floppy_recv(); /* head */
    floppy_recv(); /* sect */
    g_assert(get_irq(FLOPPY_IRQ));
    floppy_recv(); /* sz */
    g_assert(!get_irq(FLOPPY_IRQ));
    g_assert_cmpint(st0 & ST0_IC_MASK, ==, ST0_IC_ABNTERM);
    g_assert_cmpint(st1 & ST1_EC, ==, ST1_EC);

    /* Verify last_sect was not changed to 19 */
    g_assert_cmpint(get_lastsect(), ==, 18);

    /* FORMAT TRACK with valid last_sect (18) should succeed */
    send_format_track(0, 0, 18, &st0, &st1);
    g_assert_cmpint(st0 & ST0_IC_MASK, ==, 0);
    g_assert_cmpint(st1, ==, 0);

    /* FORMAT TRACK with invalid last_sect (19) must fail */
    send_format_track(0, 0, 19, &st0, &st1);
    g_assert_cmpint(st0 & ST0_IC_MASK, ==, ST0_IC_ABNTERM);
    g_assert_cmpint(st1 & ST1_EC, ==, ST1_EC);

    /* Change media to 720 kB floppy (last_sect = 9) */
    media_eject();
    media_insert_path(test_image_720k);
    send_seek(1);
    send_seek(0);

    /* Probed geometry now has last_sect = 9 */
    fake_lastsect(9);
    g_assert(!get_irq(FLOPPY_IRQ));
    g_assert_cmpint(get_lastsect(), ==, 9);

    /* Values exceeding 9 (e.g. 10 or 18) must now fail */
    fake_lastsect(10);
    g_assert(get_irq(FLOPPY_IRQ));
    st0 = floppy_recv();
    st1 = floppy_recv();
    floppy_recv();
    floppy_recv();
    floppy_recv();
    floppy_recv();
    g_assert(get_irq(FLOPPY_IRQ));
    floppy_recv();
    g_assert(!get_irq(FLOPPY_IRQ));
    g_assert_cmpint(st0 & ST0_IC_MASK, ==, ST0_IC_ABNTERM);
    g_assert_cmpint(st1 & ST1_EC, ==, ST1_EC);

    fake_lastsect(18);
    g_assert(get_irq(FLOPPY_IRQ));
    st0 = floppy_recv();
    st1 = floppy_recv();
    floppy_recv();
    floppy_recv();
    floppy_recv();
    floppy_recv();
    g_assert(get_irq(FLOPPY_IRQ));
    floppy_recv();
    g_assert(!get_irq(FLOPPY_IRQ));
    g_assert_cmpint(st0 & ST0_IC_MASK, ==, ST0_IC_ABNTERM);
    g_assert_cmpint(st1 & ST1_EC, ==, ST1_EC);

    /* FORMAT TRACK on 720 kB floppy */
    send_format_track(0, 0, 9, &st0, &st1);
    g_assert_cmpint(st0 & ST0_IC_MASK, ==, 0);
    g_assert_cmpint(st1, ==, 0);

    send_format_track(0, 0, 10, &st0, &st1);
    g_assert_cmpint(st0 & ST0_IC_MASK, ==, ST0_IC_ABNTERM);
    g_assert_cmpint(st1 & ST1_EC, ==, ST1_EC);

    send_format_track(0, 0, 18, &st0, &st1);
    g_assert_cmpint(st0 & ST0_IC_MASK, ==, ST0_IC_ABNTERM);
    g_assert_cmpint(st1 & ST1_EC, ==, ST1_EC);

    /*
     * Change back to 1.44 MB floppy and verify last_sect = 18 is allowed
     * again.
     */
    media_eject();
    media_insert();
    send_seek(1);
    send_seek(0);

    fake_lastsect(18);
    g_assert(!get_irq(FLOPPY_IRQ));
    g_assert_cmpint(get_lastsect(), ==, 18);

    fake_lastsect(19);
    g_assert(get_irq(FLOPPY_IRQ));
    st0 = floppy_recv();
    st1 = floppy_recv();
    floppy_recv();
    floppy_recv();
    floppy_recv();
    floppy_recv();
    g_assert(get_irq(FLOPPY_IRQ));
    floppy_recv();
    g_assert(!get_irq(FLOPPY_IRQ));
    g_assert_cmpint(st0 & ST0_IC_MASK, ==, ST0_IC_ABNTERM);
    g_assert_cmpint(st1 & ST1_EC, ==, ST1_EC);

    /* Leave drive empty */
    media_eject();
}

/* success if no crash or abort */
static void fuzz_registers(void)
{
    unsigned int i;

    for (i = 0; i < 1000; i++) {
        uint8_t reg, val;

        reg = (uint8_t)g_test_rand_int_range(0, 8);
        val = (uint8_t)g_test_rand_int_range(0, 256);

        outb(FLOPPY_BASE + reg, val);
        inb(FLOPPY_BASE + reg);
    }
}

static bool qtest_check_clang_sanitizer(void)
{
#ifdef QEMU_SANITIZE_ADDRESS
    return true;
#else
    g_test_skip("QEMU not configured using --enable-asan");
    return false;
#endif
}
static void test_cve_2021_20196(void)
{
    QTestState *s;

    if (!qtest_check_clang_sanitizer()) {
        return;
    }

    s = qtest_initf("-nographic -m 32M -nodefaults " DRIVE_FLOPPY_BLANK);

    qtest_outw(s, 0x3f4, 0x0500);
    qtest_outb(s, 0x3f5, 0x00);
    qtest_outb(s, 0x3f5, 0x00);
    qtest_outw(s, 0x3f4, 0x0000);
    qtest_outb(s, 0x3f5, 0x00);
    qtest_outw(s, 0x3f1, 0x0400);
    qtest_outw(s, 0x3f4, 0x0000);
    qtest_outw(s, 0x3f4, 0x0000);
    qtest_outb(s, 0x3f5, 0x00);
    qtest_outb(s, 0x3f5, 0x01);
    qtest_outw(s, 0x3f1, 0x0500);
    qtest_outb(s, 0x3f5, 0x00);
    qtest_quit(s);
}

static void test_cve_2021_3507(void)
{
    QTestState *s;

    s = qtest_initf("-nographic -m 32M -nodefaults "
                    "-drive file=%s,format=raw,if=floppy,snapshot=on",
                    test_image);
    qtest_outl(s, 0x9, 0x0a0206);
    qtest_outw(s, 0x3f4, 0x1600);
    qtest_outw(s, 0x3f4, 0x0000);
    qtest_outw(s, 0x3f4, 0x0000);
    qtest_outw(s, 0x3f4, 0x0000);
    qtest_outw(s, 0x3f4, 0x0200);
    qtest_outw(s, 0x3f4, 0x0200);
    qtest_outw(s, 0x3f4, 0x0000);
    qtest_outw(s, 0x3f4, 0x0000);
    qtest_outw(s, 0x3f4, 0x0000);
    qtest_quit(s);
}

int main(int argc, char **argv)
{
    int fd;
    int ret;

    /* Create temporary raw images */
    fd = g_file_open_tmp("qtest.XXXXXX", &test_image, NULL);
    g_assert(fd >= 0);
    ret = ftruncate(fd, TEST_IMAGE_1440KB);
    g_assert(ret == 0);
    close(fd);

    fd = g_file_open_tmp("qtest720.XXXXXX", &test_image_720k, NULL);
    g_assert(fd >= 0);
    ret = ftruncate(fd, TEST_IMAGE_720KB);
    g_assert(ret == 0);
    close(fd);

    /* Run the tests */
    g_test_init(&argc, &argv, NULL);

    qtest_start("-machine pc -device floppy,id=floppy0");
    qtest_irq_intercept_in(global_qtest, "ioapic");
    qtest_add_func("/fdc/cmos", test_cmos);
    qtest_add_func("/fdc/no_media_on_start", test_no_media_on_start);
    qtest_add_func("/fdc/read_without_media", test_read_without_media);
    qtest_add_func("/fdc/media_change", test_media_change);
    qtest_add_func("/fdc/sense_interrupt", test_sense_interrupt);
    qtest_add_func("/fdc/relative_seek", test_relative_seek);
    qtest_add_func("/fdc/read_id", test_read_id);
    qtest_add_func("/fdc/read_id_no_media", test_read_id_no_media);
    qtest_add_func("/fdc/verify", test_verify);
    qtest_add_func("/fdc/media_insert", test_media_insert);
    qtest_add_func("/fdc/read_no_dma_1", test_read_no_dma_1);
    qtest_add_func("/fdc/read_no_dma_18", test_read_no_dma_18);
    qtest_add_func("/fdc/read_no_dma_19", test_read_no_dma_19);
    qtest_add_func("/fdc/last_sect_bounds", test_last_sect_bounds);
    qtest_add_func("/fdc/fuzz-registers", fuzz_registers);
    qtest_add_func("/fdc/fuzz/cve_2021_20196", test_cve_2021_20196);
    qtest_add_func("/fdc/fuzz/cve_2021_3507", test_cve_2021_3507);

    ret = g_test_run();

    /* Cleanup */
    qtest_end();
    unlink(test_image);
    g_free(test_image);
    unlink(test_image_720k);
    g_free(test_image_720k);

    return ret;
}
