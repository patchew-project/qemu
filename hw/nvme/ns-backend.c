/*
 * QEMU NVM Express Virtual Dynamic Namespace Management
 *
 *
 * Copyright (c) 2022 Solidigm
 *
 * Authors:
 *  Michael Kropaczek      <michael.kropaczek@solidigm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See the
 * COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/qmp/qnum.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qmp/qlist.h"
#include "sysemu/sysemu.h"
#include "sysemu/block-backend.h"
#include "block/qdict.h"
#include "hw/nvme/nvme-cfg.h"

#include "nvme.h"
#include "trace.h"

/* caller will take ownership */
static QDict *ns_get_bs_default_opts(bool read_only)
{
    QDict *bs_opts = qdict_new();

    qdict_set_default_str(bs_opts, BDRV_OPT_CACHE_DIRECT, "off");
    qdict_set_default_str(bs_opts, BDRV_OPT_CACHE_NO_FLUSH, "off");
    qdict_set_default_str(bs_opts, BDRV_OPT_READ_ONLY,
                          read_only ? "on" : "off");
    qdict_set_default_str(bs_opts, BDRV_OPT_AUTO_READ_ONLY, "on");
    qdict_set_default_str(bs_opts, "driver", "raw");

    return bs_opts;
}

BlockBackend *ns_blockdev_init(const char *file, Error **errp)
{
    BlockBackend *blk = NULL;
    bool read_only = false;
    QDict *bs_opts;

    if (access(file, F_OK)) {
        error_setg(errp, "%s not found, please create one", file);
    } else {
        bs_opts = ns_get_bs_default_opts(read_only);
        blk = blk_new_open(file, NULL, bs_opts, BDRV_O_RDWR | BDRV_O_RESIZE, errp);
    }

    return blk;
}

void ns_blockdev_activate(BlockBackend *blk,  uint64_t image_size, Error **errp)
{
    int ret;

    ret = blk_set_perm(blk, BLK_PERM_WRITE | BLK_PERM_RESIZE, BLK_PERM_WRITE_UNCHANGED,  errp);
    if (ret < 0) {
        return;
    }
    ret = blk_truncate(blk, image_size, false, PREALLOC_MODE_OFF, 0,
                       errp);
}

void ns_blockdev_deactivate(BlockBackend *blk, Error **errp)
{
    ns_blockdev_activate(blk, 0, errp);
}

int ns_storage_path_check(NvmeCtrl *n, Error **errp)
{
    return storage_path_check(n->params.ns_directory,  n->params.serial, errp);
}

/* caller will take ownership */
char *ns_create_image_name(NvmeCtrl *n, uint32_t nsid, Error **errp)
{
    return create_image_name(n->params.ns_directory, n->params.serial, nsid, errp);
}

static char *ns_create_cfg_name(NvmeCtrl *n, uint32_t nsid, Error **errp)
{
    return create_cfg_name(n->params.ns_directory, n->params.serial, nsid, errp);
}

int ns_auto_check(NvmeCtrl *n, NvmeNamespace *ns, uint32_t nsid)
{
    int ret = 0;
    BlockBackend *blk = ns->blkconf.blk;
    char *file_name_img = NULL;

    if (!blk) {
        return 0;
    }

    file_name_img = ns_create_image_name(n, nsid, NULL);

    if (!file_name_img || strcmp(blk_bs(blk)->filename, file_name_img)) {
        ret = -1;
    }

    g_free(file_name_img);

    return ret;
}

void ns_cfg_clear(NvmeNamespace *ns)
{
    ns->params.pi = 0;
    ns->lbasz = 0;
    ns->id_ns.nsze = 0;
    ns->id_ns.ncap = 0;
    ns->id_ns.nuse = 0;
    ns->id_ns.nsfeat = 0;
    ns->id_ns.flbas = 0;
    ns->id_ns.nmic= 0;
    ns->size = 0;
}

int ns_cfg_save(NvmeCtrl *n, NvmeNamespace *ns, uint32_t nsid)
{
    NvmeSubsystem *subsys = n->subsys;
    QDict *ns_cfg = NULL;
    QList *ctrl_qlist = NULL;
    Error *local_err = NULL;
    int i;

    if (ns_auto_check(n, ns, nsid)) {
        error_setg(&local_err, "ns-cfg not saved: ns[%"PRIu32"] configured via '-device nvme-ns'", nsid);
        error_report_err(local_err);
        return 1;       /* not an error */
    }

    ctrl_qlist = qlist_new();
    ns_cfg = qdict_new();

    if (subsys) {
        for (i = 0; i < ARRAY_SIZE(subsys->ctrls); i++) {
            NvmeCtrl *ctrl = subsys->ctrls[i];

            if (ctrl && nvme_ns(ctrl, nsid)) {
                qlist_append_int(ctrl_qlist, i);
            }
        }
    }

#define NS_CFG_DEF(type, key, value, default) \
    qdict_put_##type(ns_cfg, key, value);
#include "hw/nvme/ns-cfg.h"
#undef NS_CFG_DEF

    return nsid_cfg_save(n->params.ns_directory, n->params.serial, ns_cfg, nsid);
}

static bool glist_exists_int(QList *qlist, int64_t value)
{
    QListEntry *entry;

    QLIST_FOREACH_ENTRY(qlist, entry) {
        if (qnum_get_int(qobject_to(QNum, entry->value)) == value) {
            return true;
        }
    }
    return false;
}


int ns_cfg_load(NvmeCtrl *n, NvmeNamespace *ns, uint32_t nsid)
{
    QObject *ns_cfg_obj = NULL;
    QDict *ns_cfg = NULL;
    QList *ctrl_qlist = NULL;
    int ret = 0;
    char *filename = NULL;
    FILE *fp = NULL;
    char buf[NS_CFG_MAXSIZE] = {};
    Error *local_err = NULL;

    if (ns_auto_check(n, ns, nsid)) {
        error_setg(&local_err, "ns-cfg not loaded: ns[%"PRIu32"] configured via '-device nvme-ns'", nsid);
        ret = 1;    /* not an error */
        goto fail2;
    }

    filename = ns_create_cfg_name(n, nsid, &local_err);
    if (local_err) {
        goto fail2;
    }

    if (access(filename, F_OK)) {
        error_setg(&local_err, "Missing ns-cfg file");
        goto fail2;
    }

    fp = fopen(filename, "r");
    if (fp == NULL) {
        error_setg(&local_err, "open %s: %s", filename,
                     strerror(errno));
        goto fail2;
    }

    if (fread(buf,  sizeof(buf), 1, fp)) {
        error_setg(&local_err, "Could not read ns-cfg");
        goto fail1;
    }

    ns_cfg_obj = qobject_from_json(buf, NULL);
    if (!ns_cfg_obj) {
        error_setg(&local_err, "Could not parse the JSON for ns-cfg");
        goto fail1;
    }

    ns_cfg = qobject_to(QDict, ns_cfg_obj);

    ns->params.nsid = (uint32_t)qdict_get_int_chkd(ns_cfg, "params.nsid", &local_err);  /* (uint32_t) */
    if (local_err) {
        goto fail1;
    }
    ctrl_qlist = qdict_get_qlist_chkd(ns_cfg, "attached_ctrls", &local_err);            /* (QList) */
    if (local_err) {
        goto fail1;
    }
    ns->params.detached = !glist_exists_int(ctrl_qlist, n->cntlid);
    ns->params.pi = (uint8_t)qdict_get_int_chkd(ns_cfg, "params.pi", &local_err);       /* (uint8_t) */
    if (local_err) {
        goto fail1;
    }
    ns->lbasz = (size_t)qdict_get_int_chkd(ns_cfg, "lbasz", &local_err);                /* (size_t) */
    if (local_err) {
        goto fail1;
    }
    ns->id_ns.nsze = cpu_to_le64(qdict_get_int_chkd(ns_cfg, "id_ns.nsze", &local_err)); /* (uint64_t) */
    if (local_err) {
        goto fail1;
    }
    ns->id_ns.ncap = cpu_to_le64(qdict_get_int_chkd(ns_cfg, "id_ns.ncap", &local_err)); /* (uint64_t) */
    if (local_err) {
        goto fail1;
    }
    ns->id_ns.nuse = cpu_to_le64(qdict_get_int_chkd(ns_cfg, "id_ns.nuse", &local_err)); /* (uint64_t) */
    if (local_err) {
        goto fail1;
    }
    ns->id_ns.nsfeat = (uint8_t)qdict_get_int_chkd(ns_cfg, "id_ns.nsfeat", &local_err); /* (uint8_t) */
    if (local_err) {
        goto fail1;
    }
    ns->id_ns.flbas = (uint8_t)qdict_get_int_chkd(ns_cfg, "id_ns.flbas", &local_err);   /* (uint8_t) */
    if (local_err) {
        goto fail1;
    }
    ns->id_ns.nmic = (uint8_t)qdict_get_int_chkd(ns_cfg, "id_ns.nmic", &local_err);     /* (uint8_t) */
    if (local_err) {
        goto fail1;
    }

    /* ns->size below will be overwritten after nvme_ns_backend_sanity_chk() */
    ns->size = qdict_get_int_chkd(ns_cfg, "ns_size", &local_err);                       /* (uint64_t) */
    if (local_err) {
        goto fail1;
    }

    /* it is expected that ns-cfg file is consistent with paired ns-img file
     * here is a simple check preventing against a crash */
    nvme_validate_flbas(ns->id_ns.flbas, &local_err);

fail1:
    fclose(fp);

fail2:
    if (local_err) {
        error_report_err(local_err);
        ret = !ret ? -1: ret;
    }

    qobject_unref(ns_cfg_obj);
    g_free(filename);
    return ret;
}
