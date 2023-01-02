/*
 * QEMU NVM Express Virtual Dynamic Namespace Management
 * Common configuration handling for qemu-img tool and qemu-system-xx
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

#include "hw/nvme/ctrl-cfg.h"

#define NS_CFG_MAXSIZE 1024
#define NS_FILE_FMT "%s/nvme_%s_ns_%03d"
#define NS_IMG_EXT ".img"
#define NS_CFG_EXT ".cfg"
#define NS_CFG_TYPE "ns-cfg"

#define NVME_FILE_FMT "%s/nvme_%s_ctrl"
#define NVME_CFG_EXT ".cfg"
#define NVME_CFG_TYPE "ctrl-cfg"

#define NVME_CFG_MAXSIZE 512
static inline int storage_path_check(char *ns_directory, char *serial, Error **errp)
{
    if (access(ns_directory, F_OK)) {
        error_setg(errp,
                         "Path '%s' to nvme controller's storage area with serial no: '%s' must exist",
                          ns_directory, serial);
        return -1;
    }

    return 0;
}


static inline char *c_create_cfg_name(char *ns_directory, char *serial, Error **errp)
{
    char *file_name = NULL;

    if (!storage_path_check(ns_directory, serial, errp)) {
        file_name = g_strdup_printf(NVME_FILE_FMT NVME_CFG_EXT,
                                   ns_directory, serial);
    }

    return file_name;
}

static inline char *create_fmt_name(const char *fmt, char *ns_directory, char *serial, uint32_t nsid, Error **errp)
{
    char *file_name = NULL;

    if (!storage_path_check(ns_directory, serial, errp)) {
        file_name = g_strdup_printf(fmt, ns_directory, serial, nsid);
    }

    return file_name;
}

static inline char *create_cfg_name(char *ns_directory, char *serial, uint32_t nsid, Error **errp)
{
    return create_fmt_name(NS_FILE_FMT NS_CFG_EXT, ns_directory, serial, nsid, errp);
}


static inline char *create_image_name(char *ns_directory, char *serial, uint32_t nsid, Error **errp)
{
    return create_fmt_name(NS_FILE_FMT NS_IMG_EXT, ns_directory, serial, nsid, errp);
}

static inline int cfg_save(char *ns_directory, char *serial, QDict *cfg,
                             const char *cfg_type, char *filename, size_t maxsize)
{
    GString *json = NULL;
    FILE *fp;
    int ret = 0;
    Error *local_err = NULL;

    json = qobject_to_json_pretty(QOBJECT(cfg), false);

    if (strlen(json->str) + 2 /* '\n'+'\0' */ > maxsize) {
        error_setg(&local_err, "%s allowed max size %ld exceeded", cfg_type, maxsize);
        goto fail;
    }

    if (filename) {
        fp = fopen(filename, "w");
        if (fp == NULL) {
            error_setg(&local_err, "open %s: %s", filename,
                         strerror(errno));
        } else {
            chmod(filename, 0644);
            if (!fprintf(fp, "%s\n", json->str)) {
                error_setg(&local_err, "could not write %s %s: %s", cfg_type, filename,
                             strerror(errno));
            }
            fclose(fp);
        }
    }

fail:
    if (local_err) {
        error_report_err(local_err);
        ret = -1;
    }

    g_string_free(json, true);
    g_free(filename);
    qobject_unref(cfg);

    return ret;
}

static inline int nsid_cfg_save(char *ns_directory, char *serial, QDict *ns_cfg, uint32_t nsid)
{
    Error *local_err = NULL;
    char *filename = create_cfg_name(ns_directory, serial, nsid, &local_err);

    if (local_err) {
        error_report_err(local_err);
        return -1;
    }

    return cfg_save(ns_directory, serial, ns_cfg, NS_CFG_TYPE, filename, NS_CFG_MAXSIZE);
}

static inline int ns_cfg_default_save(char *ns_directory, char *serial, uint32_t nsid)
{
    QDict *ns_cfg = qdict_new();
    QList *ctrl_qlist = qlist_new();

#define NS_CFG_DEF(type, key, value, default) \
    qdict_put_##type(ns_cfg, key, default);
#include "hw/nvme/ns-cfg.h"
#undef NS_CFG_DEF

    return nsid_cfg_save(ns_directory, serial, ns_cfg, nsid);
}

static inline int c_cfg_save(char *ns_directory, char *serial, QDict *nvme_cfg)
{
    Error *local_err = NULL;
    char *filename = c_create_cfg_name(ns_directory, serial, &local_err);

    if (local_err) {
        error_report_err(local_err);
        return -1;
    }

    return cfg_save(ns_directory, serial, nvme_cfg, NVME_CFG_TYPE, filename, NVME_CFG_MAXSIZE);
}

static inline int c_cfg_default_save(char *ns_directory, char *serial, uint64_t tnvmcap64, uint64_t unvmcap64)
{
    QDict *nvme_cfg = qdict_new();

#define CTRL_CFG_DEF(type, key, value, default) \
    qdict_put_##type(nvme_cfg, key, default);
#include "hw/nvme/ctrl-cfg.h"
#undef CTRL_CFG_DEF

    return c_cfg_save(ns_directory, serial, nvme_cfg);
}
