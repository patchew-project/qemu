 /*
  * SPDX-License-Identifier: GPL-2.0-or-later
  *
  * SCLP event type 11 - Control-Program Identification (CPI):
  *    CPI is used to send program identifiers from the guest to the
  *    Service-Call Logical Processor (SCLP). It is not sent by the SCLP.
  *    Please refer S390ControlProgramId QOM-type description for details
  *    on the contents of the CPI.
  *
  * Copyright IBM, Corp. 2024
  *
  * Authors:
  *  Shalini Chellathurai Saroja <shalini@linux.ibm.com>
  *
  */

#include "qemu/osdep.h"
#include "hw/s390x/event-facility.h"

typedef struct Data {
    uint8_t id_format;
    uint8_t reserved0;
    uint8_t system_type[8];
    uint64_t reserved1;
    uint8_t system_name[8];
    uint64_t reserved2;
    uint64_t system_level;
    uint64_t reserved3;
    uint8_t sysplex_name[8];
    uint8_t reserved4[16];
} QEMU_PACKED Data;

typedef struct ControlProgramIdMsg {
    EventBufferHeader ebh;
    Data data;
} QEMU_PACKED ControlProgramIdMsg;

static bool can_handle_event(uint8_t type)
{
    return type == SCLP_EVENT_CTRL_PGM_ID;
}

static sccb_mask_t send_mask(void)
{
    return 0;
}

/* Enable SCLP to accept buffers of event type CPI from the control-program. */
static sccb_mask_t receive_mask(void)
{
    return SCLP_EVENT_MASK_CTRL_PGM_ID;
}

static int write_event_data(SCLPEvent *event, EventBufferHeader *evt_buf_hdr)
{
    ControlProgramIdMsg *cpim = container_of(evt_buf_hdr, ControlProgramIdMsg,
                                             ebh);

    cpim->ebh.flags = SCLP_EVENT_BUFFER_ACCEPTED;
    return SCLP_RC_NORMAL_COMPLETION;
}

static void cpi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SCLPEventClass *k = SCLP_EVENT_CLASS(klass);

    dc->user_creatable = false;

    k->can_handle_event = can_handle_event;
    k->get_send_mask = send_mask;
    k->get_receive_mask = receive_mask;
    k->write_event_data = write_event_data;
}

static const TypeInfo sclp_cpi_info = {
    .name          = TYPE_SCLP_EVENT_CPI,
    .parent        = TYPE_SCLP_EVENT,
    .instance_size = sizeof(SCLPEventCPI),
    .class_init    = cpi_class_init,
};

static void sclp_cpi_register_types(void)
{
    type_register_static(&sclp_cpi_info);
}

type_init(sclp_cpi_register_types)

