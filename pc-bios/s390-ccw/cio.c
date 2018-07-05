/*
 * S390 Channel I/O
 *
 * Copyright (c) 2018 Jason J. Herne <jjherne@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "libc.h"
#include "s390-ccw.h"
#include "s390-arch.h"
#include "cio.h"

static char chsc_page[PAGE_SIZE] __attribute__((__aligned__(PAGE_SIZE)));

int enable_mss_facility(void)
{
    int ret;
    ChscAreaSda *sda_area = (ChscAreaSda *) chsc_page;

    memset(sda_area, 0, PAGE_SIZE);
    sda_area->request.length = 0x0400;
    sda_area->request.code = 0x0031;
    sda_area->operation_code = 0x2;

    ret = chsc(sda_area);
    if ((ret == 0) && (sda_area->response.code == 0x0001)) {
        return 0;
    }
    return -EIO;
}

void enable_subchannel(SubChannelId schid)
{
    Schib schib;

    stsch_err(schid, &schib);
    schib.pmcw.ena = 1;
    msch(schid, &schib);
}

__u16 cu_type(SubChannelId schid)
{
    Ccw1 senseIdCcw;
    SenseId senseData;

    senseIdCcw.cmd_code = CCW_CMD_SENSE_ID;
    senseIdCcw.cda = ptr2u32(&senseData);
    senseIdCcw.count = sizeof(senseData);

    if (do_cio(schid, ptr2u32(&senseIdCcw), CCW_FMT1)) {
        panic("Failed to run SenseID CCw\n");
    }

    return senseData.cu_type;
}

static bool irb_error(Irb *irb)
{
    /* We have to ignore Incorrect Length (cstat == 0x40) indicators because
     * real devices expect a 24 byte SenseID  buffer, and virtio devices expect
     * a much larger buffer. Neither device type can tolerate a buffer size
     * different from what they expect so they set this indicator.
     */
    if (irb->scsw.cstat != 0x00 && irb->scsw.cstat != 0x40) {
        return true;
    }
    return irb->scsw.dstat != 0xc;
}

/* Executes a channel program at a given subchannel. The request to run the
 * channel program is sent to the subchannel, we then wait for the interrupt
 * singaling completion of the I/O operation(s) perfomed by the channel
 * program. Lastly we verify that the i/o operation completed without error and
 * that the interrupt we received was for the subchannel used to run the
 * channel program.
 *
 * Note: This function assumes it is running in an environment where no other
 * cpus are generating or receiving I/O interrupts. So either run it in a
 * single-cpu environment or make sure all other cpus are not doing I/O and
 * have I/O interrupts masked off.
 */
int do_cio(SubChannelId schid, uint32_t ccw_addr, int fmt)
{
    Ccw0 *this_ccw, *prev_ccw;
    CmdOrb orb = {};
    Irb irb = {};
    int rc;

    IPL_assert(fmt == 0 || fmt == 1, "Invalid ccw format");

    /* ccw_addr must be <= 24 bits and point to at least one whole ccw. */
    if (fmt == 0) {
        IPL_assert(ccw_addr <= 0xFFFFFF - 8, "Invalid ccw address");
    }

    orb.fmt = fmt ;
    orb.pfch = 1;  /* QEMU's cio implementation requires prefetch */
    orb.c64 = 1;   /* QEMU's cio implementation requires 64-bit idaws */
    orb.lpm = 0xFF; /* All paths allowed */
    orb.cpa = ccw_addr;

    rc = ssch(schid, &orb);
    if (rc) {
        print_int("ssch failed with rc=", rc);
        return rc;
    }

    await_io_int(schid.sch_no);

    /* Clear read */
    rc = tsch(schid, &irb);
    if (rc) {
        print_int("tsch failed with rc=", rc);
        return rc;
    }

    if (irb_error(&irb)) {
        this_ccw = u32toptr(irb.scsw.cpa);
        prev_ccw = u32toptr(irb.scsw.cpa - 8);

        print_int("irb_error: cstat=", irb.scsw.cstat);
        print_int("           dstat=", irb.scsw.dstat);
        print_int("           cpa=", irb.scsw.cpa);
        print_int("           prev_ccw=", *((uint64_t *)prev_ccw));
        print_int("           this_ccw=", *((uint64_t *)this_ccw));
    }

    return 0;
}

void await_io_int(uint16_t sch_no)
{
    /*
     * wait_psw and ctl6 must be static to avoid stack allocation as gcc cannot
     * align stack variables. The stctg, lctlg and lpswe instructions require
     * that their operands be aligned on an 8-byte boundary.
    */
    static uint64_t ctl6 __attribute__((__aligned__(8)));
    static PSW wait_psw;

    /* PSW to load when I/O interrupt happens */
    lowcore->io_new_psw.mask = PSW_MASK_ZMODE;
    lowcore->io_new_psw.addr = (uint64_t)&&IOIntWakeup; /* Wake-up address */

    /* Enable io interrupts subclass mask */
    asm volatile("stctg 6,6,%0" : "=S" (ctl6) : : "memory");
    ctl6 |= 0x00000000FF000000;
    asm volatile("lctlg 6,6,%0" : : "S" (ctl6));

    /* Set wait psw enabled for io interrupt */
    wait_psw.mask = (PSW_MASK_ZMODE | PSW_MASK_IOINT | PSW_MASK_WAIT);
    asm volatile("lpswe %0" : : "Q" (wait_psw) : "cc");

    panic("await_io_int: lpswe failed!!\n");

IOIntWakeup:
    /* Should never happen - all other subchannels are disabled by default */
    IPL_assert(lowcore->subchannel_nr == sch_no,
               "Interrupt from unexpected device");

    /* Disable all subclasses of I/O interrupts for this cpu */
    asm volatile("stctg 6,6,%0" : "=S" (ctl6) : : "memory");
    ctl6 &= ~(0x00000000FF000000);
    asm volatile("lctlg 6,6,%0" : : "S" (ctl6));
}
