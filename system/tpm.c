/*
 * TPM configuration
 *
 * Copyright (C) 2011-2013 IBM Corporation
 *
 * Authors:
 *  Stefan Berger    <stefanb@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * Based on net.c
 */

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qapi/qapi-commands-tpm.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qapi-visit-tpm.h"
#include "sysemu/tpm_backend.h"
#include "sysemu/tpm.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "qemu/help_option.h"

static QLIST_HEAD(, TPMBackend) tpm_backends =
    QLIST_HEAD_INITIALIZER(tpm_backends);

typedef struct TpmCreateOptionsQueueEntry {
        TpmCreateOptions *tco;
        QSIMPLEQ_ENTRY(TpmCreateOptionsQueueEntry) entry;
} TpmCreateOptionsQueueEntry;

typedef QSIMPLEQ_HEAD(, TpmCreateOptionsQueueEntry) TpmCreateOptionsQueue;

static TpmCreateOptionsQueue tco_queue = QSIMPLEQ_HEAD_INITIALIZER(tco_queue);

static const TPMBackendClass *
tpm_be_find_by_type(enum TpmType type)
{
    ObjectClass *oc;
    char *typename = g_strdup_printf("tpm-%s", TpmType_str(type));

    oc = object_class_by_name(typename);
    g_free(typename);

    if (!object_class_dynamic_cast(oc, TYPE_TPM_BACKEND)) {
        return NULL;
    }

    return TPM_BACKEND_CLASS(oc);
}

/*
 * Walk the list of available TPM backend drivers and display them on the
 * screen.
 */
static void tpm_display_backend_drivers(void)
{
    bool got_one = false;
    int i;

    for (i = 0; i < TPM_TYPE__MAX; i++) {
        const TPMBackendClass *bc = tpm_be_find_by_type(i);
        if (!bc) {
            continue;
        }
        if (!got_one) {
            error_printf("Supported TPM types (choose only one):\n");
            got_one = true;
        }
        error_printf("%12s   %s\n", TpmType_str(i), bc->desc);
    }
    if (!got_one) {
        error_printf("No TPM backend types are available\n");
    }
}

/*
 * Find the TPM with the given Id
 */
TPMBackend *qemu_find_tpm_be(const char *id)
{
    TPMBackend *drv;

    if (id) {
        QLIST_FOREACH(drv, &tpm_backends, list) {
            if (!strcmp(drv->id, id)) {
                return drv;
            }
        }
    }

    return NULL;
}

static void tpm_init_tpmdev(TpmCreateOptions *tco)
{
    const TPMBackendClass *be;
    TPMBackend *drv;

    if (!QLIST_EMPTY(&tpm_backends)) {
        error_report("Only one TPM is allowed.");
        exit(1);
    }

    be = tco->type >= 0 ? tpm_be_find_by_type(tco->type) : NULL;
    if (be == NULL) {
        error_report(QERR_INVALID_PARAMETER_VALUE,
                     "type", "a TPM backend type");
        tpm_display_backend_drivers();
        exit(1);
    }

    drv = be->create(tco);
    if (!drv) {
        exit(1);
    }

    drv->id = g_strdup(tco->id);
    QLIST_INSERT_HEAD(&tpm_backends, drv, list);
}

/*
 * Walk the list of TPM backend drivers that are in use and call their
 * destroy function to have them cleaned up.
 */
void tpm_cleanup(void)
{
    TPMBackend *drv, *next;

    QLIST_FOREACH_SAFE(drv, &tpm_backends, list, next) {
        QLIST_REMOVE(drv, list);
        object_unref(OBJECT(drv));
    }
}

/*
 * Initialize the TPM. Process the tpmdev command line options describing the
 * TPM backend.
 */
void tpm_init(void)
{
    while (!QSIMPLEQ_EMPTY(&tco_queue)) {
        TpmCreateOptionsQueueEntry *tcoqe = QSIMPLEQ_FIRST(&tco_queue);

        QSIMPLEQ_REMOVE_HEAD(&tco_queue, entry);
        tpm_init_tpmdev(tcoqe->tco);
        qapi_free_TpmCreateOptions(tcoqe->tco);
        g_free(tcoqe);
    }
}

/*
 * Parse the TPM configuration options.
 * To display all available TPM backends the user may use '-tpmdev help'
 */
void tpm_config_parse(const char *optstr)
{
    Visitor *v;
    TpmCreateOptionsQueueEntry *tcqe;

    if (is_help_option(optstr)) {
        tpm_display_backend_drivers();
        exit(0);
    }
    v = qobject_input_visitor_new_str(optstr, "type", &error_fatal);
    tcqe = g_new(TpmCreateOptionsQueueEntry, 1);
    visit_type_TpmCreateOptions(v, NULL, &tcqe->tco, &error_fatal);
    visit_free(v);
    QSIMPLEQ_INSERT_TAIL(&tco_queue, tcqe, entry);
}

/*
 * Walk the list of active TPM backends and collect information about them.
 */
TPMInfoList *qmp_query_tpm(Error **errp)
{
    TPMBackend *drv;
    TPMInfoList *head = NULL, **tail = &head;

    QLIST_FOREACH(drv, &tpm_backends, list) {
        if (!drv->tpmif) {
            continue;
        }

        QAPI_LIST_APPEND(tail, tpm_backend_query_tpm(drv));
    }

    return head;
}

TpmTypeList *qmp_query_tpm_types(Error **errp)
{
    unsigned int i = 0;
    TpmTypeList *head = NULL, **tail = &head;

    for (i = 0; i < TPM_TYPE__MAX; i++) {
        if (!tpm_be_find_by_type(i)) {
            continue;
        }
        QAPI_LIST_APPEND(tail, i);
    }

    return head;
}
TpmModelList *qmp_query_tpm_models(Error **errp)
{
    TpmModelList *head = NULL, **tail = &head;
    GSList *e, *l = object_class_get_list(TYPE_TPM_IF, false);

    for (e = l; e; e = e->next) {
        TPMIfClass *c = TPM_IF_CLASS(e->data);

        QAPI_LIST_APPEND(tail, c->model);
    }
    g_slist_free(l);

    return head;
}
