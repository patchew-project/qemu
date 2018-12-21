/*
 *  A TCMU userspace handler for QEMU block drivers.
 *
 *  Copyright (C) 2016 Red Hat, Inc.
 *
 *  Authors:
 *      Fam Zheng <famz@redhat.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; under version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "libtcmu.h"
#include "helper.h"
#include "qapi/qmp/qerror.h"
#include "qemu/error-report.h"
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"
#include "block/aio.h"
#include "block/qdict.h"
#include "scsi/constants.h"
#include "tcmu/tcmu.h"
#include "qemu/main-loop.h"
#include "qemu/option.h"
#include "qapi/qapi-commands.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qmp/qdict.h"
#include "qapi/error.h"

#include "qemu/compiler.h"
#include "trace.h"

typedef struct TCMUExport TCMUExport;

struct TCMUExport {
    BlockBackend *blk;
    struct tcmu_device *tcmu_dev;
    bool writable;
    QLIST_ENTRY(TCMUExport) next;
};

typedef struct {
    struct tcmulib_context *tcmulib_ctx;
} TCMUHandlerState;

static QLIST_HEAD(, TCMUExport) tcmu_exports =
    QLIST_HEAD_INITIALIZER(tcmu_exports);

static TCMUHandlerState *handler_state;

/* This's temporary, will use scsi/utils.c code */
#define ASCQ_INVALID_FIELD_IN_CDB 0x2400

typedef struct {
    struct tcmulib_cmd *cmd;
    TCMUExport *exp;
    QEMUIOVector *qiov;
} TCMURequest;

static void qemu_tcmu_aio_cb(void *opaque, int ret)
{
    TCMURequest *req = opaque;

    trace_qemu_tcmu_aio_cb();
    tcmulib_command_complete(req->exp->tcmu_dev, req->cmd,
                             ret ? CHECK_CONDITION : GOOD);
    tcmulib_processing_complete(req->exp->tcmu_dev);
    g_free(req->qiov);
    g_free(req);
}

static inline TCMURequest *qemu_tcmu_req_new(TCMUExport *exp,
                                             struct tcmulib_cmd *cmd,
                                             QEMUIOVector *qiov)
{
    TCMURequest *req = g_new(TCMURequest, 1);
    *req = (TCMURequest) {
        .exp = exp,
        .cmd = cmd,
        .qiov = qiov,
    };
    return req;
}

static int qemu_tcmu_handle_cmd(TCMUExport *exp, struct tcmulib_cmd *cmd)
{

    uint8_t *cdb = cmd->cdb;
    /* TODO: block size? */
    uint64_t offset = tcmu_cdb_get_lba(cdb) << BDRV_SECTOR_BITS;
    QEMUIOVector *qiov;

    trace_qemu_tcmu_handle_cmd(cdb[0]);
    switch (cdb[0]) {
    case INQUIRY:
        return tcmu_emulate_inquiry(exp->tcmu_dev, cdb,
                                    cmd->iovec, cmd->iov_cnt);
    case TEST_UNIT_READY:
        return tcmu_emulate_test_unit_ready(cdb, cmd->iovec, cmd->iov_cnt);
    case SERVICE_ACTION_IN_16:
        if (cdb[1] == SAI_READ_CAPACITY_16) {
            return tcmu_emulate_read_capacity_16(blk_getlength(exp->blk) / 512,
                                                 512,
                                                 cmd->cdb, cmd->iovec,
                                                 cmd->iov_cnt);
        } else {
            return TCMU_STS_NOT_HANDLED;
        }
    case MODE_SENSE:
    case MODE_SENSE_10:
        return tcmu_emulate_mode_sense(exp->tcmu_dev, cdb, cmd->iovec,
                                       cmd->iov_cnt);
    case MODE_SELECT:
    case MODE_SELECT_10:
        return tcmu_emulate_mode_select(exp->tcmu_dev, cdb, cmd->iovec,
                                        cmd->iov_cnt);
    case SYNCHRONIZE_CACHE:
    case SYNCHRONIZE_CACHE_16:
        if (cdb[1] & 0x2) {
            return tcmu_sense_set_data(cmd->sense_buf, ILLEGAL_REQUEST,
                                       ASCQ_INVALID_FIELD_IN_CDB);
        } else {
            blk_aio_flush(exp->blk, qemu_tcmu_aio_cb,
                          qemu_tcmu_req_new(exp, cmd, NULL));
            return TCMU_STS_ASYNC_HANDLED;
        }
        break;
    case READ_6:
    case READ_10:
    case READ_12:
    case READ_16:
        qiov = g_new(QEMUIOVector, 1);
        qemu_iovec_init_external(qiov, cmd->iovec, cmd->iov_cnt);
        trace_qemu_tcmu_handle_cmd_read(offset);
        blk_aio_preadv(exp->blk, offset, qiov, 0, qemu_tcmu_aio_cb,
                       qemu_tcmu_req_new(exp, cmd, qiov));
        return TCMU_STS_ASYNC_HANDLED;

    case WRITE_6:
    case WRITE_10:
    case WRITE_12:
    case WRITE_16:
        qiov = g_new(QEMUIOVector, 1);
        qemu_iovec_init_external(qiov, cmd->iovec, cmd->iov_cnt);
        trace_qemu_tcmu_handle_cmd_write(offset);
        blk_aio_pwritev(exp->blk, offset, qiov, 0, qemu_tcmu_aio_cb,
                        qemu_tcmu_req_new(exp, cmd, qiov));
        return TCMU_STS_ASYNC_HANDLED;

    default:
        trace_qemu_tcmu_handle_cmd_unknown_cmd(cdb[0]);
        return TCMU_STS_NOT_HANDLED;
    }
}

static void qemu_tcmu_dev_event_handler(void *opaque)
{
    TCMUExport *exp = opaque;
    struct tcmulib_cmd *cmd;
    struct tcmu_device *dev = exp->tcmu_dev;

    tcmulib_processing_start(dev);

    while ((cmd = tcmulib_get_next_command(dev)) != NULL) {
        int ret = qemu_tcmu_handle_cmd(exp, cmd);
        if (ret != TCMU_STS_ASYNC_HANDLED) {
            tcmulib_command_complete(dev, cmd, ret);
        }
    }

    tcmulib_processing_complete(dev);
}

static TCMUExport *tcmu_export_lookup(const BlockBackend *blk)
{
    TCMUExport *exp;

    QLIST_FOREACH(exp, &tcmu_exports, next) {
        if (exp->blk == blk) {
            return exp;
        }
    }
    return NULL;
}
static TCMUExport *parse_cfgstr(const char *cfgstr,
                                          Error **errp);
static bool check_cfgstr(const char *cfgstr,
                                          Error **errp);

QemuOptsList qemu_tcmu_common_export_opts = {
    .name = "export",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_tcmu_common_export_opts.head),
    .desc = {
        {
            .name = "snapshot",
            .type = QEMU_OPT_BOOL,
            .help = "enable/disable snapshot mode",
        },{
            .name = "aio",
            .type = QEMU_OPT_STRING,
            .help = "host AIO implementation (threads, native)",
        },{
            .name = "format",
            .type = QEMU_OPT_STRING,
            .help = "disk format (raw, qcow2, ...)",
        },{
            .name = "file",
            .type = QEMU_OPT_STRING,
            .help = "file name",
        },
        { /* end of list */ }
    },
};

QemuOptsList qemu_tcmu_export_opts = {
    .name = "export",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_tcmu_export_opts.head),
    .desc = {
        /* no elements => accept any params */
        { /* end of list */ }
    },
};

int export_init_func(void *opaque, QemuOpts *all_opts, Error **errp)
{
    int flags = BDRV_O_RDWR;
    const char *buf;
    int ret = 0;
    bool writethrough;
    BlockBackend *blk;
    int snapshot = 0;
    Error *local_err = NULL;
    QemuOpts *common_opts;
    const char *id;
    const char *aio;
    const char *value;
    QDict *bs_opts;
    bool read_only = false;
    const char *file;
    TCMUExport *exp;

    value = qemu_opt_get(all_opts, "cache");
    if (value) {
        if (bdrv_parse_cache_mode(value, &flags, &writethrough) != 0) {
            error_report("invalid cache option");
            ret = -1;
            goto err_too_early;
        }
        /* Specific options take precedence */
        if (!qemu_opt_get(all_opts, BDRV_OPT_CACHE_DIRECT)) {
            qemu_opt_set_bool(all_opts, BDRV_OPT_CACHE_DIRECT,
                              !!(flags & BDRV_O_NOCACHE), &error_abort);
        }
        if (!qemu_opt_get(all_opts, BDRV_OPT_CACHE_NO_FLUSH)) {
            qemu_opt_set_bool(all_opts, BDRV_OPT_CACHE_NO_FLUSH,
                              !!(flags & BDRV_O_NO_FLUSH), &error_abort);
        }
        qemu_opt_unset(all_opts, "cache");
    }

    bs_opts = qdict_new();
    /* all_opts->id also copied into one option in bs_opts */
    qemu_opts_to_qdict(all_opts, bs_opts);

    id = qdict_get_try_str(bs_opts, "id");
    common_opts = qemu_opts_create(&qemu_tcmu_common_export_opts, id, 1,
                                   &local_err);
    if (local_err) {
        error_report_err(local_err);
        ret = -1;
        goto err_no_opts;
    }

    trace_export_init_func();

    qemu_opts_absorb_qdict(common_opts, bs_opts, &local_err);
    if (local_err) {
        error_report_err(local_err);
        ret = -1;
        goto early_err;
    }

    if (id) {
        qdict_del(bs_opts, "id");
    }

    if ((aio = qemu_opt_get(common_opts, "aio")) != NULL) {
            if (!strcmp(aio, "native")) {
                flags |= BDRV_O_NATIVE_AIO;
            } else if (!strcmp(aio, "threads")) {
                /* this is the default */
            } else {
               error_report("invalid aio option");
               ret = -1;
               goto early_err;
            }
    }

    if ((buf = qemu_opt_get(common_opts, "format")) != NULL) {
        if (qdict_haskey(bs_opts, "driver")) {
            error_report("Cannot specify both 'driver' and 'format'");
            ret = -1;
            goto early_err;
        }
        qdict_put_str(bs_opts, "driver", buf);
    }

    snapshot = qemu_opt_get_bool(common_opts, "snapshot", 0);
    if (snapshot) {
        flags |= BDRV_O_SNAPSHOT;
    }

    read_only = qemu_opt_get_bool(common_opts, BDRV_OPT_READ_ONLY, false);
    if (read_only)
        flags &= ~BDRV_O_RDWR;

    /* bdrv_open() defaults to the values in bdrv_flags (for compatibility
     * with other callers) rather than what we want as the real defaults
     * Apply the defaults here instead. */
    qdict_set_default_str(bs_opts, BDRV_OPT_CACHE_DIRECT, "off");
    qdict_set_default_str(bs_opts, BDRV_OPT_CACHE_NO_FLUSH, "off");
    qdict_set_default_str(bs_opts, BDRV_OPT_READ_ONLY,
                              read_only ? "on" : "off");

    /* if (qemu_opts_id(all_opts) == NULL) */

    file = qemu_opt_get(common_opts, "file");
    blk = blk_new_open(file, NULL, bs_opts, flags, &local_err);
    bs_opts = NULL;
    if (!blk) {
        error_report_err(local_err);
        ret = -1;
        goto err_no_bs_opts;
    }

    blk_set_enable_write_cache(blk, !writethrough);

    id = qemu_opts_id(common_opts);
    if (!monitor_add_blk(blk, id, &local_err)) {
        error_report_err(local_err);
        blk_unref(blk);
        ret = -1;
        goto err_no_bs_opts;
    }

    exp = tcmu_export_new(blk, flags & BDRV_O_RDWR, &local_err);
    if (!exp) {
        error_reportf_err(local_err, "Failed to create export: ");
        ret = -1;
        monitor_remove_blk(blk);
    }

err_no_bs_opts:
early_err:
    qemu_opts_del(common_opts);
err_no_opts:
    qobject_unref(bs_opts);
err_too_early:
    return ret;
}

static bool qemu_tcmu_check_config(const char *cfgstr, char **reason)
{
    Error *local_err = NULL;

    if (!check_cfgstr(cfgstr, &local_err) && local_err) {
        *reason = strdup(error_get_pretty(local_err));
        error_free(local_err);
        return false;
    }
    return true;
}

static int qemu_tcmu_added(struct tcmu_device *dev)
{
    TCMUExport *exp;
    const char *cfgstr = tcmu_dev_get_cfgstring(dev);
    Error *local_err = NULL;

    exp = parse_cfgstr(cfgstr, &local_err);
    if (!exp) {
        return -1;
    }
    exp->tcmu_dev = dev;
    tcmu_dev_set_private(dev, exp);
    aio_set_fd_handler(blk_get_aio_context(exp->blk),
                       tcmu_dev_get_fd(dev),
                       true, qemu_tcmu_dev_event_handler,
                       NULL, NULL, exp);
    return 0;
}

static void tcmu_export_close(TCMUExport *exp)
{
    aio_set_fd_handler(blk_get_aio_context(exp->blk),
                       tcmu_dev_get_fd(exp->tcmu_dev),
                       false, NULL,
                       NULL, NULL, NULL);
    monitor_remove_blk(exp->blk);
    blk_unref(exp->blk);
    QLIST_REMOVE(exp, next);
    g_free(exp);
}

static void qemu_tcmu_removed(struct tcmu_device *dev)
{
    TCMUExport *exp = tcmu_dev_get_private(dev);

    if(exp)
        tcmu_export_close(exp);
}

static void qemu_tcmu_master_read(void *opaque)
{
    TCMUHandlerState *s = opaque;

    trace_qemu_tcmu_master_read();
    tcmulib_master_fd_ready(s->tcmulib_ctx);
}

static struct tcmulib_handler qemu_tcmu_handler = {
    .name = "Handler for QEMU block devices",
    .subtype = NULL, /* Dynamically generated when starting. */
    .cfg_desc = "Format: device=<name>",
    .added = qemu_tcmu_added,
    .removed = qemu_tcmu_removed,
    .check_config = qemu_tcmu_check_config,
};

static bool check_cfgstr(const char *cfgstr,
                                          Error **errp)
{
    BlockBackend *blk;
    const char *dev_str, *id, *device;
    const char *pr;
    const char *subtype = qemu_tcmu_handler.subtype;
    size_t subtype_len;
    TCMUExport *exp;

    if (!subtype) {
        error_setg(errp, "TCMU Handler not started");
    }
    subtype_len = strlen(subtype);
    if (strncmp(cfgstr, subtype, subtype_len) ||
        cfgstr[subtype_len] != '/') {
        error_report("TCMU: Invalid subtype in device cfgstring: %s", cfgstr);
        return false;
    }
    dev_str = &cfgstr[subtype_len + 1];
    if (dev_str[0] != '@') {
        error_report("TCMU: Invalid cfgstring format. Must be @<device_name>");
        return false;
    }
    device = &dev_str[1];

    pr = strchr(device, '@');
    if (!pr) {
	id = device;
    	blk = blk_by_name(id);
    	if (!blk) {
        	error_setg(errp, "TCMU: Device not found: %s", id);
        	return false;
    	}
    	exp = tcmu_export_lookup(blk);
    	if (!exp) {
        	error_setg(errp, "TCMU: Device not found: %s", id);
        	return false;
   	}
    }// TODO: else to check id?

    return true;
}

static void tcmu_convert_delim(char *to, const char *opts)
{
    while (*opts != '\0') {
	if (*opts == '@') {
	    *to = ',';
	} else
	    *to = *opts;

	opts++;
	to++;
    }

    if(to)
        *to = '\0';
}
static TCMUExport *parse_cfgstr(const char *cfgstr,
                                          Error **errp)
{
    const char *device, *id, *pr;
    const char *subtype = qemu_tcmu_handler.subtype;
    size_t subtype_len;
    TCMUExport *exp = NULL;
    char *new_device;

    subtype_len = strlen(subtype);
    device = &cfgstr[subtype_len + 2];

    pr = strchr(device, '@');
    if (!pr) {
    	id = device;
    	exp = tcmu_export_lookup(blk_by_name(id));
    }
    else {
	QemuOpts * export_opts;

	new_device = g_malloc0(strlen(device) + 1);
	tcmu_convert_delim(new_device, device);

        /* parse new_device into an QemuOpts and link into
           qemu_tcmu_export_opts with QemuOpts->id set while
           without an option id in QemuOpts.
         */
	export_opts = qemu_opts_parse_noisily(&qemu_tcmu_export_opts,
					    new_device, false);
        trace_qemu_tcmu_parse_cfgstr();
        g_free(new_device);

        if(!export_opts)
            goto fail;

	if (export_init_func(NULL, export_opts, NULL))
	    goto fail;

	id = qemu_opts_id(export_opts);
	exp = tcmu_export_lookup(blk_by_name(id));

        qemu_opts_del(export_opts);
    }

fail:
    return exp;
}

void qemu_tcmu_stop(void)
{
    tcmulib_close(handler_state->tcmulib_ctx);
    g_free(handler_state);
    handler_state = NULL;
}

void qemu_tcmu_start(const char *subtype, Error **errp)
{
    int fd;

    trace_qemu_tcmu_start();
    if (handler_state) {
        error_setg(errp, "TCMU handler already started");
        return;
    }
    assert(!qemu_tcmu_handler.subtype);
    qemu_tcmu_handler.subtype = g_strdup(subtype);
    handler_state = g_new0(TCMUHandlerState, 1);
    handler_state->tcmulib_ctx = tcmulib_initialize(&qemu_tcmu_handler, 1);

    if (!handler_state->tcmulib_ctx) {
        error_setg(errp, "Failed to initialize tcmulib");
        goto fail;
    }
    fd = tcmulib_get_master_fd(handler_state->tcmulib_ctx);
    qemu_set_fd_handler(fd, qemu_tcmu_master_read, NULL, handler_state);
    trace_qemu_tcmu_start_register();
    tcmulib_register(handler_state->tcmulib_ctx);
    return;

fail:
    g_free(handler_state);
    handler_state = NULL;
}

TCMUExport *tcmu_export_new(BlockBackend *blk, bool writable, Error **errp)
{
    TCMUExport *exp;

    exp = tcmu_export_lookup(blk);
    if (exp) {
        error_setg(errp, "Block device already added");
        return NULL;
    }
    exp = g_new0(TCMUExport, 1);
    exp->blk = blk;
    blk_ref(blk);
    exp->writable = writable;
    QLIST_INSERT_HEAD(&tcmu_exports, exp, next);
    return exp;
}
