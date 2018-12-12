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

uint16_t cu_type(SubChannelId schid)
{
    Ccw1 sense_id_ccw;
    SenseId sense_data;

    sense_id_ccw.cmd_code = CCW_CMD_SENSE_ID;
    sense_id_ccw.cda = ptr2u32(&sense_data);
    sense_id_ccw.count = sizeof(sense_data);

    if (do_cio(schid, ptr2u32(&sense_id_ccw), CCW_FMT1)) {
        panic("Failed to run SenseID CCw\n");
    }

    return sense_data.cu_type;
}

void basic_sense(SubChannelId schid, SenseData *sd)
{
    Ccw1 senseCcw;

    senseCcw.cmd_code = CCW_CMD_BASIC_SENSE;
    senseCcw.cda = ptr2u32(sd);
    senseCcw.count = sizeof(*sd);

    if (do_cio(schid, ptr2u32(&senseCcw), CCW_FMT1)) {
        panic("Failed to run Basic Sense CCW\n");
    }
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

static void print_sense_data(SenseData *sd)
{
    char msgline[512];

    if (sd->config_info & 0x8000) {
        sclp_print("Sense Data (fmt 24-bytes):\n");
    } else {
        sclp_print("Sense Data (fmt 32-bytes):\n");
    }

    strcat(msgline, "    Sense Condition Flags :");
    if (sd->status[0] & SNS0_CMD_REJECT) {
        strcat(msgline, " [Cmd-Reject]");
    }
    if (sd->status[0] & SNS0_INTERVENTION_REQ) {
        strcat(msgline, " [Intervention-Required]");
    }
    if (sd->status[0] & SNS0_BUS_OUT_CHECK) {
        strcat(msgline, " [Bus-Out-Parity-Check]");
    }
    if (sd->status[0] & SNS0_EQUIPMENT_CHECK) {
        strcat(msgline, " [Equipment-Check]");
    }
    if (sd->status[0] & SNS0_DATA_CHECK) {
        strcat(msgline, " [Data-Check]");
    }
    if (sd->status[0] & SNS0_OVERRUN) {
        strcat(msgline, " [Overrun]");
    }
    if (sd->status[0] & SNS0_INCOMPL_DOMAIN) {
        strcat(msgline, " [Incomplete-Domain]");
    }

    if (sd->status[1] & SNS1_PERM_ERR) {
        strcat(msgline, " [Permanent-Error]");
    }
    if (sd->status[1] & SNS1_INV_TRACK_FORMAT) {
        strcat(msgline, " [Invalid-Track-Fmt]");
    }
    if (sd->status[1] & SNS1_EOC) {
        strcat(msgline, " [End-of-Cyl]");
    }
    if (sd->status[1] & SNS1_MESSAGE_TO_OPER) {
        strcat(msgline, " [Operator-Msg]");
    }
    if (sd->status[1] & SNS1_NO_REC_FOUND) {
        strcat(msgline, " [No-Record-Found]");
    }
    if (sd->status[1] & SNS1_FILE_PROTECTED) {
        strcat(msgline, " [File-Protected]");
    }
    if (sd->status[1] & SNS1_WRITE_INHIBITED) {
        strcat(msgline, " [Write-Inhibited]");
    }
    if (sd->status[1] & SNS1_INPRECISE_END) {
        strcat(msgline, " [Imprecise-Ending]");
    }

    if (sd->status[2] & SNS2_REQ_INH_WRITE) {
        strcat(msgline, " [Req-Inhibit-Write]");
    }
    if (sd->status[2] & SNS2_CORRECTABLE) {
        strcat(msgline, " [Correctable-Data-Check]");
    }
    if (sd->status[2] & SNS2_FIRST_LOG_ERR) {
        strcat(msgline, " [First-Error-Log]");
    }
    if (sd->status[2] & SNS2_ENV_DATA_PRESENT) {
        strcat(msgline, " [Env-Data-Present]");
    }
    if (sd->status[2] & SNS2_INPRECISE_END) {
        strcat(msgline, " [Imprecise-End]");
    }
    strcat(msgline, "\n");
    sclp_print(msgline);

    print_int("    Residual Count     =", sd->res_count);
    print_int("    Phys Drive ID      =", sd->phys_drive_id);
    print_int("    low cyl address    =", sd->low_cyl_addr);
    print_int("    head addr & hi cyl =", sd->head_high_cyl_addr);
    print_int("    format/message     =", sd->fmt_msg);
    print_int("    fmt-dependent[0-7] =", sd->fmt_dependent_info[0]);
    print_int("    fmt-dependent[8-15]=", sd->fmt_dependent_info[1]);
    print_int("    prog action code   =", sd->program_action_code);
    print_int("    Configuration info =", sd->config_info);
    print_int("    mcode / hi-cyl     =", sd->mcode_hicyl);
    print_int("    cyl & head addr [0]=", sd->cyl_head_addr[0]);
    print_int("    cyl & head addr [1]=", sd->cyl_head_addr[1]);
    print_int("    cyl & head addr [2]=", sd->cyl_head_addr[2]);
}

static void print_irb_err(Irb *irb)
{
    Ccw0 *this_ccw = u32toptr(irb->scsw.cpa);
    Ccw0 *prev_ccw = u32toptr(irb->scsw.cpa - 8);
    char msgline[256];

    sclp_print("vfio-ccw device I/O error - Interrupt Response Block Data:\n");

    strcat(msgline, "    Function Ctrl :");
    if (irb->scsw.ctrl & SCSW_FCTL_START_FUNC) {
        strcat(msgline, " [Start]");
    }
    if (irb->scsw.ctrl & SCSW_FCTL_HALT_FUNC) {
        strcat(msgline, " [Halt]");
    }
    if (irb->scsw.ctrl & SCSW_FCTL_CLEAR_FUNC) {
        strcat(msgline, " [Clear]");
    }
    strcat(msgline, "\n");
    sclp_print(msgline);

    msgline[0] = '\0';
    strcat(msgline, "    Activity Ctrl :");
    if (irb->scsw.ctrl & SCSW_ACTL_RESUME_PEND) {
        strcat(msgline, " [Resume-Pending]");
    }
    if (irb->scsw.ctrl & SCSW_ACTL_START_PEND) {
        strcat(msgline, " [Start-Pending]");
    }
    if (irb->scsw.ctrl & SCSW_ACTL_HALT_PEND) {
        strcat(msgline, " [Halt-Pending]");
    }
    if (irb->scsw.ctrl & SCSW_ACTL_CLEAR_PEND) {
        strcat(msgline, " [Clear-Pending]");
    }
    if (irb->scsw.ctrl & SCSW_ACTL_CH_ACTIVE) {
        strcat(msgline, " [Channel-Active]");
    }
    if (irb->scsw.ctrl & SCSW_ACTL_DEV_ACTIVE) {
        strcat(msgline, " [Device-Active]");
    }
    if (irb->scsw.ctrl & SCSW_ACTL_SUSPENDED) {
        strcat(msgline, " [Suspended]");
    }
    strcat(msgline, "\n");
    sclp_print(msgline);

    msgline[0] = '\0';
    strcat(msgline, "    Status Ctrl :");
    if (irb->scsw.ctrl & SCSW_SCTL_ALERT) {
        strcat(msgline, " [Alert]");
    }
    if (irb->scsw.ctrl & SCSW_SCTL_INTERMED) {
        strcat(msgline, " [Intermediate]");
    }
    if (irb->scsw.ctrl & SCSW_SCTL_PRIMARY) {
        strcat(msgline, " [Primary]");
    }
    if (irb->scsw.ctrl & SCSW_SCTL_SECONDARY) {
        strcat(msgline, " [Secondary]");
    }
    if (irb->scsw.ctrl & SCSW_SCTL_STATUS_PEND) {
        strcat(msgline, " [Status-Pending]");
    }

    strcat(msgline, "\n");
    sclp_print(msgline);

    msgline[0] = '\0';
    strcat(msgline, "    Device Status :");
    if (irb->scsw.dstat & SCSW_DSTAT_ATTN) {
        strcat(msgline, " [Attention]");
    }
    if (irb->scsw.dstat & SCSW_DSTAT_STATMOD) {
        strcat(msgline, " [Status-Modifier]");
    }
    if (irb->scsw.dstat & SCSW_DSTAT_CUEND) {
        strcat(msgline, " [Ctrl-Unit-End]");
    }
    if (irb->scsw.dstat & SCSW_DSTAT_BUSY) {
        strcat(msgline, " [Busy]");
    }
    if (irb->scsw.dstat & SCSW_DSTAT_CHEND) {
        strcat(msgline, " [Channel-End]");
    }
    if (irb->scsw.dstat & SCSW_DSTAT_DEVEND) {
        strcat(msgline, " [Device-End]");
    }
    if (irb->scsw.dstat & SCSW_DSTAT_UCHK) {
        strcat(msgline, " [Unit-Check]");
    }
    if (irb->scsw.dstat & SCSW_DSTAT_UEXCP) {
        strcat(msgline, " [Unit-Exception]");
    }
    strcat(msgline, "\n");
    sclp_print(msgline);

    msgline[0] = '\0';
    strcat(msgline, "    Channel Status :");
    if (irb->scsw.cstat & SCSW_CSTAT_PCINT) {
        strcat(msgline, " [Program-Ctrl-Interruption]");
    }
    if (irb->scsw.cstat & SCSW_CSTAT_BADLEN) {
        strcat(msgline, " [Incorrect-Length]");
    }
    if (irb->scsw.cstat & SCSW_CSTAT_PROGCHK) {
        strcat(msgline, " [Program-Check]");
    }
    if (irb->scsw.cstat & SCSW_CSTAT_PROTCHK) {
        strcat(msgline, " [Protection-Check]");
    }
    if (irb->scsw.cstat & SCSW_CSTAT_CHDCHK) {
        strcat(msgline, " [Channel-Data-Check]");
    }
    if (irb->scsw.cstat & SCSW_CSTAT_CHCCHK) {
        strcat(msgline, " [Channel-Ctrl-Check]");
    }
    if (irb->scsw.cstat & SCSW_CSTAT_ICCHK) {
        strcat(msgline, " [Interface-Ctrl-Check]");
    }
    if (irb->scsw.cstat & SCSW_CSTAT_CHAINCHK) {
        strcat(msgline, " [Chaining-Check]");
    }
    strcat(msgline, "\n");
    sclp_print(msgline);

    print_int("    cpa=", irb->scsw.cpa);
    print_int("    prev_ccw=", *((uint64_t *)prev_ccw));
    print_int("    this_ccw=", *((uint64_t *)this_ccw));
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
    CmdOrb orb = {};
    Irb irb = {};
    SenseData sd;
    int rc, retries = 0;

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

    while (true) {
        rc = ssch(schid, &orb);
        if (rc) {
            print_int("ssch failed with rc=", rc);
            break;
        }

        consume_io_int();

        /* Clear read */
        rc = tsch(schid, &irb);
        if (rc) {
            print_int("tsch failed with rc=", rc);
            break;
        }

        if (!irb_error(&irb)) {
            break;
        }

        /* Unexpected unit check. Use sense to clear unit check then retry. */
        if (unit_check(&irb) && retries <= 2) {
            basic_sense(schid, &sd);
            retries++;
            continue;
        }

        print_irb_err(&irb);
        basic_sense(schid, &sd);
        print_sense_data(&sd);
        break;
    }

    return rc;
}
