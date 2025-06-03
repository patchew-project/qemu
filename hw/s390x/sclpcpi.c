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
#include "qemu/timer.h"
#include "hw/s390x/event-facility.h"
#include "hw/s390x/ebcdic.h"
#include "migration/vmstate.h"

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
    SCLPEventCPI *e = SCLP_EVENT_CPI(event);

    ascii_put(e->system_type, (char *)cpim->data.system_type,
              sizeof(cpim->data.system_type));
    ascii_put(e->system_name, (char *)cpim->data.system_name,
              sizeof(cpim->data.system_name));
    ascii_put(e->sysplex_name, (char *)cpim->data.sysplex_name,
              sizeof(cpim->data.sysplex_name));
    e->system_level = ldq_be_p(&cpim->data.system_level);
    e->timestamp = qemu_clock_get_ns(QEMU_CLOCK_HOST);

    cpim->ebh.flags = SCLP_EVENT_BUFFER_ACCEPTED;
    return SCLP_RC_NORMAL_COMPLETION;
}

static char *get_system_type(Object *obj, Error **errp)
{
    SCLPEventCPI *e = SCLP_EVENT_CPI(obj);

    return g_strndup((char *) e->system_type, sizeof(e->system_type));
}

static char *get_system_name(Object *obj, Error **errp)
{
    SCLPEventCPI *e = SCLP_EVENT_CPI(obj);

    return g_strndup((char *) e->system_name, sizeof(e->system_name));
}

static char *get_sysplex_name(Object *obj, Error **errp)
{
    SCLPEventCPI *e = SCLP_EVENT_CPI(obj);

    return g_strndup((char *) e->sysplex_name, sizeof(e->sysplex_name));
}

static const VMStateDescription vmstate_sclpcpi = {
    .name = "s390_control_program_id",
    .version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(system_type, SCLPEventCPI, 8),
        VMSTATE_UINT8_ARRAY(system_name, SCLPEventCPI, 8),
        VMSTATE_UINT64(system_level, SCLPEventCPI),
        VMSTATE_UINT8_ARRAY(sysplex_name, SCLPEventCPI, 8),
        VMSTATE_UINT64(timestamp, SCLPEventCPI),
        VMSTATE_END_OF_LIST()
    }
};

static void cpi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SCLPEventClass *k = SCLP_EVENT_CLASS(klass);

    dc->user_creatable = false;
    dc->vmsd =  &vmstate_sclpcpi;

    k->can_handle_event = can_handle_event;
    k->get_send_mask = send_mask;
    k->get_receive_mask = receive_mask;
    k->write_event_data = write_event_data;
}

static void cpi_init(Object *obj)
{
    SCLPEventCPI *e = SCLP_EVENT_CPI(obj);

    object_property_add_str(obj, "system_type", get_system_type, NULL);
    object_property_add_str(obj, "system_name", get_system_name, NULL);
    object_property_add_str(obj, "sysplex_name", get_sysplex_name, NULL);
    object_property_add_uint64_ptr(obj, "system_level", &(e->system_level),
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "timestamp", &(e->timestamp),
                                   OBJ_PROP_FLAG_READ);
}

static const TypeInfo sclp_cpi_info = {
    .name          = TYPE_SCLP_EVENT_CPI,
    .parent        = TYPE_SCLP_EVENT,
    .instance_size = sizeof(SCLPEventCPI),
    .instance_init = cpi_init,
    .class_init    = cpi_class_init,
};

static void sclp_cpi_register_types(void)
{
    type_register_static(&sclp_cpi_info);
}

type_init(sclp_cpi_register_types)

