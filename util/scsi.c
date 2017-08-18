/*
 *  SCSI helpers
 *
 *  Copyright 2017 Red Hat, Inc.
 *
 *  Authors:
 *   Fam Zheng <famz@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include "qemu/osdep.h"
#include "scsi/scsi.h"

int scsi_sense_to_errno(int key, int asc, int ascq)
{
    switch (key) {
    case 0x0b: /* SCSI_SENSE_COMMAND_ABORTED */
        return ECANCELED;
    case 0x02: /* SCSI_SENSE_NOT_READY */
    case 0x05: /* SCSI_SENSE_ILLEGAL_REQUEST */
    case 0x07: /* SCSI_SENSE_DATA_PROTECTION */
        /* Parse ASCQ */
        break;
    default:
        return EIO;
    }
    switch ((asc << 8) | ascq) {
    case 0x1a00: /* SCSI_SENSE_ASCQ_PARAMETER_LIST_LENGTH_ERROR */
    case 0x2000: /* SCSI_SENSE_ASCQ_INVALID_OPERATION_CODE */
    case 0x2400: /* SCSI_SENSE_ASCQ_INVALID_FIELD_IN_CDB */
    case 0x2600: /* SCSI_SENSE_ASCQ_INVALID_FIELD_IN_PARAMETER_LIST */
        return EINVAL;
    case 0x2100: /* SCSI_SENSE_ASCQ_LBA_OUT_OF_RANGE */
    case 0x2707: /* SCSI_SENSE_ASCQ_SPACE_ALLOC_FAILED */
        return ENOSPC;
    case 0x2500: /* SCSI_SENSE_ASCQ_LOGICAL_UNIT_NOT_SUPPORTED */
        return ENOTSUP;
    case 0x3a00: /* SCSI_SENSE_ASCQ_MEDIUM_NOT_PRESENT */
    case 0x3a01: /* SCSI_SENSE_ASCQ_MEDIUM_NOT_PRESENT_TRAY_CLOSED */
    case 0x3a02: /* SCSI_SENSE_ASCQ_MEDIUM_NOT_PRESENT_TRAY_OPEN */
        return ENOMEDIUM;
    case 0x2700: /* SCSI_SENSE_ASCQ_WRITE_PROTECTED */
        return EACCES;
    default:
        return EIO;
    }
}
