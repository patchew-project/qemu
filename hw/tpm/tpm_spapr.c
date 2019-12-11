/*
 * QEMU PowerPC pSeries Logical Partition (aka sPAPR) hardware System Emulator
 *
 * PAPR Virtual TPM
 *
 * Copyright (c) 2015, 2017 IBM Corporation.
 *
 * Authors:
 *    Stefan Berger <stefanb@linux.vnet.ibm.com>
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#include "sysemu/tpm_backend.h"
#include "tpm_int.h"
#include "tpm_util.h"

#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_vio.h"
#include "trace.h"

#define DEBUG_SPAPR 0

#define VIO_SPAPR_VTPM(obj) \
     OBJECT_CHECK(SPAPRvTPMState, (obj), TYPE_TPM_SPAPR)

typedef struct VioCRQ {
    uint8_t valid;  /* 0x80: cmd; 0xc0: init crq */
                    /* 0x81-0x83: CRQ message response */
    uint8_t msg;    /* see below */
    uint16_t len;   /* len of TPM request; len of TPM response */
    uint32_t data;  /* rtce_dma_handle when sending TPM request */
    uint64_t reserved;
} VioCRQ;

typedef union TPMSpaprCRQ {
    VioCRQ s;
    uint8_t raw[sizeof(VioCRQ)];
} TPMSpaprCRQ;

#define SPAPR_VTPM_VALID_INIT_CRQ_COMMAND  0xC0
#define SPAPR_VTPM_VALID_COMMAND           0x80
#define SPAPR_VTPM_MSG_RESULT              0x80

/* msg types for valid = SPAPR_VTPM_VALID_INIT_CRQ */
#define SPAPR_VTPM_INIT_CRQ_RESULT           0x1
#define SPAPR_VTPM_INIT_CRQ_COMPLETE_RESULT  0x2

/* msg types for valid = SPAPR_VTPM_VALID_CMD */
#define SPAPR_VTPM_GET_VERSION               0x1
#define SPAPR_VTPM_TPM_COMMAND               0x2
#define SPAPR_VTPM_GET_RTCE_BUFFER_SIZE      0x3
#define SPAPR_VTPM_PREPARE_TO_SUSPEND        0x4

/* response error messages */
#define SPAPR_VTPM_VTPM_ERROR                0xff

/* error codes */
#define SPAPR_VTPM_ERR_COPY_IN_FAILED        0x3
#define SPAPR_VTPM_ERR_COPY_OUT_FAILED       0x4

#define MAX_BUFFER_SIZE TARGET_PAGE_SIZE

typedef struct {
    SpaprVioDevice vdev;

    TPMSpaprCRQ crq; /* track single TPM command */

    uint8_t state;
#define SPAPR_VTPM_STATE_NONE         0
#define SPAPR_VTPM_STATE_EXECUTION    1
#define SPAPR_VTPM_STATE_COMPLETION   2

    unsigned char buffer[MAX_BUFFER_SIZE];

    TPMBackendCmd cmd;

    TPMBackend *be_driver;
    TPMVersion be_tpm_version;

    size_t be_buffer_size;
} SPAPRvTPMState;

static void tpm_spapr_show_buffer(const unsigned char *buffer,
                                  size_t buffer_size, const char *string)
{
    size_t len, i;
    char *line_buffer, *p;

    len = MIN(tpm_cmd_get_size(buffer), buffer_size);

    /*
     * allocate enough room for 3 chars per buffer entry plus a
     * newline after every 16 chars and a final null terminator.
     */
    line_buffer = g_malloc(len * 3 + (len / 16) + 1);

    for (i = 0, p = line_buffer; i < len; i++) {
        if (i && !(i % 16)) {
            p += sprintf(p, "\n");
        }
        p += sprintf(p, "%.2X ", buffer[i]);
    }
    trace_tpm_spapr_show_buffer(string, len, line_buffer);

    g_free(line_buffer);
}

/*
 * Send a request to the TPM.
 */
static void tpm_spapr_tpm_send(SPAPRvTPMState *s)
{
    if (trace_event_get_state_backends(TRACE_TPM_SPAPR_SHOW_BUFFER)) {
        tpm_spapr_show_buffer(s->buffer, sizeof(s->buffer), "To TPM");
    }

    s->state = SPAPR_VTPM_STATE_EXECUTION;
    s->cmd = (TPMBackendCmd) {
        .locty = 0,
        .in = s->buffer,
        .in_len = MIN(tpm_cmd_get_size(s->buffer), sizeof(s->buffer)),
        .out = s->buffer,
        .out_len = sizeof(s->buffer),
    };

    tpm_backend_deliver_request(s->be_driver, &s->cmd);
}

static int tpm_spapr_process_cmd(SPAPRvTPMState *s, uint64_t dataptr)
{
    long rc;

    /* a max. of be_buffer_size bytes can be transported */
    rc = spapr_vio_dma_read(&s->vdev, dataptr,
                            s->buffer, s->be_buffer_size);
    if (rc) {
        error_report("tpm_spapr_got_payload: DMA read failure");
    }
    /* let vTPM handle any malformed request */
    tpm_spapr_tpm_send(s);

    return rc;
}

static int tpm_spapr_do_crq(struct SpaprVioDevice *dev, uint8_t *crq_data)
{
    SPAPRvTPMState *s = VIO_SPAPR_VTPM(dev);
    TPMSpaprCRQ local_crq;
    TPMSpaprCRQ *crq = &s->crq; /* requests only */
    int rc;

    memcpy(&local_crq.raw, crq_data, sizeof(local_crq.raw));

    trace_tpm_spapr_do_crq(local_crq.raw[0], local_crq.raw[1]);

    switch (local_crq.s.valid) {
    case SPAPR_VTPM_VALID_INIT_CRQ_COMMAND: /* Init command/response */

        /* Respond to initialization request */
        switch (local_crq.s.msg) {
        case SPAPR_VTPM_INIT_CRQ_RESULT:
            trace_tpm_spapr_do_crq_crq_result();
            memset(local_crq.raw, 0, sizeof(local_crq.raw));
            local_crq.s.valid = SPAPR_VTPM_VALID_INIT_CRQ_COMMAND;
            local_crq.s.msg = SPAPR_VTPM_INIT_CRQ_RESULT;
            spapr_vio_send_crq(dev, local_crq.raw);
            break;

        case SPAPR_VTPM_INIT_CRQ_COMPLETE_RESULT:
            trace_tpm_spapr_do_crq_crq_complete_result();
            memset(local_crq.raw, 0, sizeof(local_crq.raw));
            local_crq.s.valid = SPAPR_VTPM_VALID_INIT_CRQ_COMMAND;
            local_crq.s.msg = SPAPR_VTPM_INIT_CRQ_COMPLETE_RESULT;
            spapr_vio_send_crq(dev, local_crq.raw);
            break;
        }

        break;
    case SPAPR_VTPM_VALID_COMMAND: /* Payloads */
        switch (local_crq.s.msg) {
        case SPAPR_VTPM_TPM_COMMAND:
            trace_tpm_spapr_do_crq_tpm_command();
            if (s->state == SPAPR_VTPM_STATE_EXECUTION) {
                return H_BUSY;
            }
            /* this crq is tracked */
            memcpy(crq->raw, crq_data, sizeof(crq->raw));

            rc = tpm_spapr_process_cmd(s, be32_to_cpu(crq->s.data));

            if (rc == H_SUCCESS) {
                crq->s.valid = be16_to_cpu(0);
            } else {
                local_crq.s.valid = SPAPR_VTPM_MSG_RESULT;
                local_crq.s.msg = SPAPR_VTPM_VTPM_ERROR;
                local_crq.s.data = cpu_to_be32(SPAPR_VTPM_ERR_COPY_IN_FAILED);
                spapr_vio_send_crq(dev, local_crq.raw);
            }
            break;

        case SPAPR_VTPM_GET_RTCE_BUFFER_SIZE:
            trace_tpm_spapr_do_crq_tpm_get_rtce_buffer_size(s->be_buffer_size);
            local_crq.s.msg |= SPAPR_VTPM_MSG_RESULT;
            local_crq.s.len = cpu_to_be16(s->be_buffer_size);
            spapr_vio_send_crq(dev, local_crq.raw);
            break;

        case SPAPR_VTPM_GET_VERSION:
            local_crq.s.msg |= SPAPR_VTPM_MSG_RESULT;
            local_crq.s.len = cpu_to_be16(0);
            switch (s->be_tpm_version) {
            case TPM_VERSION_UNSPEC:
                local_crq.s.data = cpu_to_be32(0);
                break;
            case TPM_VERSION_1_2:
                local_crq.s.data = cpu_to_be32(1);
                break;
            case TPM_VERSION_2_0:
                local_crq.s.data = cpu_to_be32(2);
                break;
            }
            trace_tpm_spapr_do_crq_get_version(be32_to_cpu(local_crq.s.data));
            spapr_vio_send_crq(dev, local_crq.raw);
            break;

        case SPAPR_VTPM_PREPARE_TO_SUSPEND:
            trace_tpm_spapr_do_crq_prepare_to_suspend();
            local_crq.s.msg |= SPAPR_VTPM_MSG_RESULT;
            spapr_vio_send_crq(dev, local_crq.raw);
            break;

        default:
            trace_tpm_spapr_do_crq_unknown_msg_type(crq->s.msg);
        }
        break;
    default:
        trace_tpm_spapr_do_crq_unknown_crq(local_crq.raw[0], local_crq.raw[1]);
    };

    return H_SUCCESS;
}

static void tpm_spapr_request_completed(TPMIf *ti, int ret)
{
    SPAPRvTPMState *s = VIO_SPAPR_VTPM(ti);
    TPMSpaprCRQ *crq = &s->crq;
    uint32_t len;
    int rc;

    s->state = SPAPR_VTPM_STATE_COMPLETION;

    /* a max. of be_buffer_size bytes can be transported */
    len = MIN(tpm_cmd_get_size(s->buffer), s->be_buffer_size);
    rc = spapr_vio_dma_write(&s->vdev, be32_to_cpu(crq->s.data),
                             s->buffer, len);

    if (trace_event_get_state_backends(TRACE_TPM_SPAPR_SHOW_BUFFER)) {
        tpm_spapr_show_buffer(s->buffer, len, "From TPM");
    }

    crq->s.valid = SPAPR_VTPM_MSG_RESULT;
    if (rc == H_SUCCESS) {
        crq->s.msg = SPAPR_VTPM_TPM_COMMAND | SPAPR_VTPM_MSG_RESULT;
        crq->s.len = cpu_to_be16(len);
    } else {
        error_report("%s: DMA write failure", __func__);
        crq->s.msg = SPAPR_VTPM_VTPM_ERROR;
        crq->s.len = cpu_to_be16(0);
        crq->s.data = cpu_to_be32(SPAPR_VTPM_ERR_COPY_OUT_FAILED);
    }

    rc = spapr_vio_send_crq(&s->vdev, crq->raw);
    if (rc) {
        error_report("%s: Error sending response", __func__);
    }
}

static int tpm_spapr_do_startup_tpm(SPAPRvTPMState *s, size_t buffersize)
{
    return tpm_backend_startup_tpm(s->be_driver, buffersize);
}

static void tpm_spapr_update_deviceclass(SpaprVioDevice *dev)
{
    SPAPRvTPMState *s = VIO_SPAPR_VTPM(dev);
    SpaprVioDeviceClass *k = VIO_SPAPR_DEVICE_GET_CLASS(dev);

    switch (s->be_tpm_version) {
    case TPM_VERSION_UNSPEC:
        assert(false);
        break;
    case TPM_VERSION_1_2:
        k->dt_name = "vtpm";
        k->dt_type = "IBM,vtpm";
        k->dt_compatible = "IBM,vtpm";
        break;
    case TPM_VERSION_2_0:
        k->dt_name = "vtpm";
        k->dt_type = "IBM,vtpm";
        k->dt_compatible = "IBM,vtpm20";
        break;
    }
}

static void tpm_spapr_reset(SpaprVioDevice *dev)
{
    SPAPRvTPMState *s = VIO_SPAPR_VTPM(dev);

    s->state = SPAPR_VTPM_STATE_NONE;

    s->be_tpm_version = tpm_backend_get_tpm_version(s->be_driver);
    tpm_spapr_update_deviceclass(dev);

    s->be_buffer_size = MAX(ROUND_UP(tpm_backend_get_buffer_size(s->be_driver),
                                     TARGET_PAGE_SIZE),
                            sizeof(s->buffer));

    tpm_backend_reset(s->be_driver);
    tpm_spapr_do_startup_tpm(s, s->be_buffer_size);
}

static enum TPMVersion tpm_spapr_get_version(TPMIf *ti)
{
    SPAPRvTPMState *s = VIO_SPAPR_VTPM(ti);

    if (tpm_backend_had_startup_error(s->be_driver)) {
        return TPM_VERSION_UNSPEC;
    }

    return tpm_backend_get_tpm_version(s->be_driver);
}

static const VMStateDescription vmstate_spapr_vtpm = {
    .name = "tpm-spapr",
    .unmigratable = 1,
};

static Property tpm_spapr_properties[] = {
    DEFINE_SPAPR_PROPERTIES(SPAPRvTPMState, vdev),
    DEFINE_PROP_TPMBE("tpmdev", SPAPRvTPMState, be_driver),
    DEFINE_PROP_END_OF_LIST(),
};

static void tpm_spapr_realizefn(SpaprVioDevice *dev, Error **errp)
{
    SPAPRvTPMState *s = VIO_SPAPR_VTPM(dev);

    if (!tpm_find()) {
        error_setg(errp, "at most one TPM device is permitted");
        return;
    }

    dev->crq.SendFunc = tpm_spapr_do_crq;

    if (!s->be_driver) {
        error_setg(errp, "'tpmdev' property is required");
        return;
    }
}

static void tpm_spapr_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SpaprVioDeviceClass *k = VIO_SPAPR_DEVICE_CLASS(klass);
    TPMIfClass *tc = TPM_IF_CLASS(klass);

    k->realize = tpm_spapr_realizefn;
    k->reset = tpm_spapr_reset;
    k->signal_mask = 0x00000001;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->props = tpm_spapr_properties;
    k->rtce_window_size = 0x10000000;
    dc->vmsd = &vmstate_spapr_vtpm;

    tc->model = TPM_MODEL_TPM_SPAPR;
    tc->get_version = tpm_spapr_get_version;
    tc->request_completed = tpm_spapr_request_completed;
}

static const TypeInfo tpm_spapr_info = {
    .name          = TYPE_TPM_SPAPR,
    .parent        = TYPE_VIO_SPAPR_DEVICE,
    .instance_size = sizeof(SPAPRvTPMState),
    .class_init    = tpm_spapr_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_TPM_IF },
        { }
    }
};

static void tpm_spapr_register_types(void)
{
    type_register_static(&tpm_spapr_info);
}

type_init(tpm_spapr_register_types)
