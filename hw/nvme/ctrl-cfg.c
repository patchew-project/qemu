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
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qmp/qlist.h"
#include "sysemu/block-backend.h"
#include "block/qdict.h"
#include "qemu/int128.h"
#include "hw/nvme/nvme-cfg.h"

#include "nvme.h"
#include "trace.h"

static char *nvme_create_cfg_name(NvmeCtrl *n, Error **errp)
{
    return c_create_cfg_name(n->params.ns_directory, n->params.serial, errp);
}

static NvmeIdCtrl *get_nvme_id_ctrl(NvmeCtrl *n)
{
    NvmeCtrl *ctrl;
    NvmeSubsystem *subsys = n->subsys;

    ctrl = nvme_subsys_ctrl(subsys, 0);
    return ctrl ? &ctrl->id_ctrl : &n->id_ctrl;
}

int nvme_cfg_save(NvmeCtrl *n)
{
    NvmeIdCtrl *id = get_nvme_id_ctrl(n);
    QDict *nvme_cfg = NULL;
    Int128  tnvmcap128;
    Int128  unvmcap128;

    nvme_cfg = qdict_new();

    memcpy(&tnvmcap128, id->tnvmcap, sizeof(tnvmcap128));
    memcpy(&unvmcap128, id->unvmcap, sizeof(unvmcap128));

#define CTRL_CFG_DEF(type, key, value, default) \
    qdict_put_##type(nvme_cfg, key, value);
#include "hw/nvme/ctrl-cfg.h"
#undef CTRL_CFG_DEF

    return c_cfg_save(n->params.ns_directory, n->params.serial, nvme_cfg);
}

int nvme_cfg_update(NvmeCtrl *n, uint64_t amount, NvmeNsAllocAction action)
{
    int ret = 0;
    NvmeCtrl *ctrl;
    NvmeIdCtrl *id = get_nvme_id_ctrl(n);
    NvmeSubsystem *subsys = n->subsys;
    Int128  tnvmcap128;
    Int128  unvmcap128;
    Int128  amount128 = int128_make64(amount);
    int i;

    memcpy(&tnvmcap128, id->tnvmcap, sizeof(tnvmcap128));
    memcpy(&unvmcap128, id->unvmcap, sizeof(unvmcap128));

    switch (action) {
    case NVME_NS_ALLOC_CHK:
        if (int128_ge(unvmcap128, amount128)) {
            return 0;   /* no update */
        } else {
            ret = -1;
        }
        break;
    case NVME_NS_ALLOC:
        if (int128_ge(unvmcap128, amount128)) {
            unvmcap128 = int128_sub(unvmcap128, amount128);
        } else {
            ret = -1;
        }
        break;
    case NVME_NS_DEALLOC:
        unvmcap128 = int128_add(unvmcap128, amount128);
        if (int128_ge(unvmcap128, tnvmcap128)) {
            unvmcap128 = tnvmcap128;
        }
        break;
    default:;
    }

    if (ret == 0) {
        if (subsys) {
            for (i = 0; i < ARRAY_SIZE(subsys->ctrls); i++) {
                ctrl = nvme_subsys_ctrl(subsys, i);
                if (ctrl) {
                    id = &ctrl->id_ctrl;
                    memcpy(id->unvmcap, &unvmcap128, sizeof(id->unvmcap));
                }
            }
        } else {
            memcpy(id->unvmcap, &unvmcap128, sizeof(id->unvmcap));
        }
    }

    return ret;
}

/* Note: id->tnvmcap and id->unvmcap are pointing to 16 bytes arrays,
 *       but those are interpreted as 128bits int objects.
 *       It is OK here to use Int128 because backend's namespace images cannot
 *       exceed size of 64bit max value */
static void nvme_cfg_validate(NvmeCtrl *n, uint64_t tnvmcap, uint64_t unvmcap,
                             Error **errp)
{
    NvmeIdCtrl *id = &n->id_ctrl;
    Int128  tnvmcap128;
    Int128  unvmcap128;

    if (unvmcap > tnvmcap) {
        error_setg(errp, "nvme-cfg file is corrupted, free to allocate[%"PRIu64
                   "] > total capacity[%"PRIu64"]",
                   unvmcap, tnvmcap);
    } else if (tnvmcap == (uint64_t) 0) {
        error_setg(errp, "nvme-cfg file error: total capacity cannot be zero");
    } else {
        tnvmcap128 = int128_make64(tnvmcap);
        unvmcap128 = int128_make64(unvmcap);
        memcpy(id->tnvmcap, &tnvmcap128, sizeof(id->tnvmcap));
        memcpy(id->unvmcap, &unvmcap128, sizeof(id->unvmcap));
    }
}

int nvme_cfg_load(NvmeCtrl *n)
{
    QObject *nvme_cfg_obj = NULL;
    QDict *nvme_cfg = NULL;
    NvmeIdCtrl *id_p = get_nvme_id_ctrl(n);
    NvmeIdCtrl *id = &n->id_ctrl;
    int ret = 0;
    char *filename = NULL;
    uint64_t tnvmcap;
    uint64_t unvmcap;
    FILE *fp = NULL;
    char buf[NVME_CFG_MAXSIZE] = {};
    Error *local_err = NULL;

    if (n->cntlid) {    /* secondary controller */
        memcpy(id->tnvmcap, id_p->tnvmcap, sizeof(id->tnvmcap));
        memcpy(id->unvmcap, id_p->unvmcap, sizeof(id->unvmcap));
        goto fail2;
    }

    filename = nvme_create_cfg_name(n, &local_err);
    if (local_err) {
        goto fail2;
    }

    if (access(filename, F_OK)) {
        error_setg(&local_err, "Missing nvme-cfg file");
        goto fail2;
    }

    fp = fopen(filename, "r");
    if (fp == NULL) {
        error_setg(&local_err, "open %s: %s", filename,
                     strerror(errno));
        goto fail2;
    }

    if (fread(buf,  sizeof(buf), 1, fp)) {
        error_setg(&local_err, "Could not read nvme-cfg");
        goto fail1;
    }

    nvme_cfg_obj = qobject_from_json(buf, NULL);
    if (!nvme_cfg_obj) {
        error_setg(&local_err, "Could not parse the JSON for nvme-cfg");
        goto fail1;
    }

    nvme_cfg = qobject_to(QDict, nvme_cfg_obj);
    qdict_flatten(nvme_cfg);

    tnvmcap = qdict_get_int_chkd(nvme_cfg, "tnvmcap", &local_err);
    if (local_err) {
        goto fail1;
    }

    unvmcap = qdict_get_int_chkd(nvme_cfg, "unvmcap", &local_err);
    if (local_err) {
        goto fail1;
    }

    nvme_cfg_validate(n, tnvmcap, unvmcap, &local_err);

fail1:
    fclose(fp);

fail2:
    if (local_err) {
        error_report_err(local_err);
        ret = -1;
    }

    qobject_unref(nvme_cfg_obj);
    g_free(filename);

    return ret;
}
