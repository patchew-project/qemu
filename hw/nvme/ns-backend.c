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
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qstring.h"
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
    Error *local_err = NULL;
    QDict *bs_opts;

    if (access(file, F_OK)) {
        error_setg(&local_err, "%s not found, please create one", file);
    }

    if (!local_err) {
        bs_opts = ns_get_bs_default_opts(read_only);
        blk = blk_new_open(file, NULL, bs_opts, BDRV_O_RDWR | BDRV_O_RESIZE, &local_err);
    }

    if (local_err) {
        error_propagate(errp, local_err);
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

    file_name_img = ns_create_image_name(n, nsid, NULL);

    if (!blk) {
    } else if (!file_name_img || strcmp(blk_bs(blk)->filename, file_name_img)) {
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
    QDict *ns_cfg = NULL;
    Error *local_err = NULL;

    if (ns_auto_check(n, ns, nsid)) {
        error_setg(&local_err, "ns-cfg not saved: ns[%"PRIu32"] configured via '-device nvme-ns'", nsid);
        error_report_err(local_err);
        return 1;       /* not an error */
    }

    ns_cfg = qdict_new();

#define NS_CFG_DEF(type, key, value, default) \
    qdict_put_##type(ns_cfg, key, value);
#include "hw/nvme/ns-cfg.h"
#undef NS_CFG_DEF

    return nsid_cfg_save(n->params.ns_directory, n->params.serial, ns_cfg, nsid);
}

int ns_cfg_load(NvmeCtrl *n, NvmeNamespace *ns, uint32_t nsid)
{
    QObject *ns_cfg_obj = NULL;
    QDict *ns_cfg = NULL;
    int ret = 0;
    char *filename;
    FILE *fp;
    char buf[NS_CFG_MAXSIZE] = {};
    Error *local_err = NULL;

    if (ns_auto_check(n, ns, nsid)) {
        error_setg(&local_err, "ns-cfg not loaded: ns[%"PRIu32"] configured via '-device nvme-ns'", nsid);
        error_report_err(local_err);
        return 1;       /* not an error */
    }

    filename = ns_create_cfg_name(n, nsid, &local_err);
    if (!local_err && !access(filename, F_OK)) {
        fp = fopen(filename, "r");
        if (fp == NULL) {
            error_setg(&local_err, "open %s: %s", filename,
                         strerror(errno));
        } else {
            if (!fread(buf,  sizeof(buf), 1, fp)) {
                ns_cfg_obj = qobject_from_json(buf, NULL);
                if (!ns_cfg_obj) {
                    error_setg(&local_err, "Could not parse the JSON for ns-cfg");
                } else {
                    ns_cfg = qobject_to(QDict, ns_cfg_obj);
                    qdict_flatten(ns_cfg);

                    ns->params.nsid = (uint32_t)qdict_get_int_chkd(ns_cfg, "params.nsid", &local_err);      /* (uint32_t) */
                    if (!local_err) {
                        ns->params.detached = qdict_get_bool_chkd(ns_cfg,"params.detached", &local_err);    /* (bool) */
                    }
                    if (!local_err) {
                        ns->params.pi = (uint8_t)qdict_get_int_chkd(ns_cfg, "params.pi", &local_err);       /* (uint8_t) */
                    }
                    if (!local_err) {
                        ns->lbasz = (size_t)qdict_get_int_chkd(ns_cfg, "lbasz", &local_err);                /* (size_t) */
                    }
                    if (!local_err) {
                        ns->id_ns.nsze = cpu_to_le64(qdict_get_int_chkd(ns_cfg, "id_ns.nsze", &local_err)); /* (uint64_t) */
                    }
                    if (!local_err) {
                        ns->id_ns.ncap = cpu_to_le64(qdict_get_int_chkd(ns_cfg, "id_ns.ncap", &local_err)); /* (uint64_t) */
                    }
                    if (!local_err) {
                        ns->id_ns.nuse = cpu_to_le64(qdict_get_int_chkd(ns_cfg, "id_ns.nuse", &local_err)); /* (uint64_t) */
                    }
                    if (!local_err) {
                        ns->id_ns.nsfeat = (uint8_t)qdict_get_int_chkd(ns_cfg, "id_ns.nsfeat", &local_err); /* (uint8_t) */
                    }
                    if (!local_err) {
                        ns->id_ns.flbas = (uint8_t)qdict_get_int_chkd(ns_cfg, "id_ns.flbas", &local_err);   /* (uint8_t) */
                    }
                    if (!local_err) {
                        ns->id_ns.nmic = (uint8_t)qdict_get_int_chkd(ns_cfg, "id_ns.nmic", &local_err);     /* (uint8_t) */
                    }
                    if (!local_err) {
                        /* ns->size below will be overwritten after nvme_ns_backend_sanity_chk() */
                        ns->size = qdict_get_int_chkd(ns_cfg, "ns_size", &local_err);                       /* (uint64_t) */
                    }

                    qobject_unref(ns_cfg_obj);

                    /* it is expected that ns-cfg file is consistent with paired ns-img file
                     * here a simple check preventing against a crash */
                    nvme_validate_flbas(ns->id_ns.flbas, &local_err);
                }
            } else {
                error_setg(&local_err, "Could not read ns-cfg");
            }
            fclose(fp);
        }
    }
    else if (!local_err){
        error_setg(&local_err, "Missing ns-cfg file");
    }

    if (local_err) {
        error_report_err(local_err);
        ret = -1;
    }

    g_free(filename);
    return ret;
}
