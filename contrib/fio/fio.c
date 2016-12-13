/*
 * QEMU engine for fio
 *
 */
#include "fio-qemu.h"
#include "fio-optgroup-qemu.h"
#include "qemu/osdep.h"
#include "qemu/iov.h"
#include "qapi/error.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"
#include "block/aio.h"
#include "block/qapi.h"
#include "crypto/init.h"
#include "sysemu/block-backend.h"

#ifndef TD_ENG_FLAG_SHIFT
#define io_ops_data io_ops->data
#define io_ops_dlhandle io_ops->dlhandle
#endif

struct qemu_data {
	AioContext *ctx;
	unsigned int completed;
	unsigned int to_submit;
	struct io_u *aio_events[];
};

struct qemu_options {
	void *pad;
	const char *aio;
	const char *format;
	const char *driver;
	unsigned int poll_max_ns;
};

static QemuMutex iothread_lock;

static int str_aio_cb(void *data, const char *str)
{
	struct qemu_options *o = data;

	if (!strcmp(str, "native") || !strcmp(str, "threads"))
		o->aio = strdup(str);
	else
		return 1;

	return 0;
}

static struct fio_option options[] = {
	{
		.name   = "qemu_driver",
		.lname  = "QEMU block driver",
		.type   = FIO_OPT_STR_STORE,
		.off1   = offsetof(struct qemu_options, driver),
		.category = FIO_OPT_C_ENGINE,
		.group  = FIO_OPT_G_INVALID,
	},
	{
		.name   = "qemu_format",
		.lname  = "Image format",
		.type   = FIO_OPT_STR_STORE,
		.off1   = offsetof(struct qemu_options, format),
		.category = FIO_OPT_C_ENGINE,
		.group  = FIO_OPT_G_INVALID,
	},
	{
		.name   = "qemu_aio",
		.lname  = "Use native AIO",
		.type   = FIO_OPT_STR_STORE,
		.off1   = offsetof(struct qemu_options, aio),
		.cb     = str_aio_cb,
		.category = FIO_OPT_C_ENGINE,
		.group  = FIO_OPT_G_INVALID,
	},
	{
		.name   = "qemu_poll_max_ns",
		.lname  = "QEMU polling period",
		.type   = FIO_OPT_STR_SET,
		.off1   = offsetof(struct qemu_options, poll_max_ns),
		.category = FIO_OPT_C_ENGINE,
		.group  = FIO_OPT_G_INVALID,
	},
	{
		.name   = NULL,
	},
};

static int fio_qemu_getevents(struct thread_data *td, unsigned int min,
			      unsigned int max, const struct timespec *t)
{
	struct qemu_data *qd = td->io_ops_data;

	/* TODO: set timer */
	do
		aio_poll(qd->ctx, true);
	while (qd->completed < min);

	return qd->completed;
}

static struct io_u *fio_qemu_event(struct thread_data *td, int event)
{
	struct qemu_data *qd = td->io_ops_data;

	qd->completed--;
	return qd->aio_events[event];
}

static inline BlockBackend *fio_qemu_get_blk(struct fio_file *file)
{
	return (BlockBackend *)(uintptr_t)(file->engine_data & ~1);
}

static inline bool fio_qemu_mark_plugged(struct fio_file *file)
{
	bool plugged = (file->engine_data & 1);
	file->engine_data |= 1;
	return plugged;
}

static inline bool fio_qemu_test_and_clear_plugged(struct fio_file *file)
{
	bool plugged = (file->engine_data & 1);
	file->engine_data &= ~1;
	return plugged;
}

static void fio_qemu_entry(void *opaque)
{
	struct io_u *io_u = opaque;
	struct fio_file *file = io_u->file;
	BlockBackend *blk = fio_qemu_get_blk(file);
	struct iovec iov = { io_u->xfer_buf, io_u->xfer_buflen };

	struct thread_data *td;
	struct qemu_data *qd;
	unsigned r;
	int ret;

	if (!fio_qemu_mark_plugged(io_u->file))
		blk_io_plug(blk);

	if (io_u->ddir == DDIR_READ) {
		QEMUIOVector qiov;
		qemu_iovec_init_external(&qiov, &iov, 1);
		ret = blk_co_preadv(blk, io_u->offset, iov.iov_len, &qiov, 0);
	} else if (io_u->ddir == DDIR_WRITE) {
		QEMUIOVector qiov;
		qemu_iovec_init_external(&qiov, &iov, 1);
		ret = blk_co_pwritev(blk, io_u->offset, iov.iov_len, &qiov, 0);
	} else if (io_u->ddir == DDIR_TRIM) {
		ret = blk_co_pdiscard(blk, io_u->offset, iov.iov_len);
	} else {
		ret = blk_flush(blk);
	}

	if (!ret) {
		io_u->resid = 0;
		io_u->error = 0;
	} else if (ret == -ECANCELED) {
		io_u->resid = io_u->xfer_buflen;
		io_u->error = 0;
	} else {
		io_u->error = -ret;
	}

	td = io_u->engine_data;
	qd = td->io_ops_data;
	r = qd->completed++;
	qd->aio_events[r] = io_u;
}

static int fio_qemu_queue(struct thread_data *td,
			      struct io_u *io_u)
{
	Coroutine *co;
	struct qemu_data *qd;

	fio_ro_check(td, io_u);

	co = qemu_coroutine_create(fio_qemu_entry, io_u);
	io_u->error = EINPROGRESS;
	io_u->engine_data = td;
	qemu_coroutine_enter(co);

	qd = td->io_ops_data;
	if (io_u->error == EINPROGRESS) {
		/* Since we have a commit hook, we need to call io_u_queued()
		 * ourselves, but we don't really know if the backend actually
		 * does anything on blk_io_plug/unplug.  Calling it here is not
		 * exactly right if it does do something, but it saves the
		 * expense of walking the io_u's again in fio_qemu_commit.
		 */
		io_u_queued(td, io_u);
		io_u->error = 0;
		qd->to_submit++;
		return FIO_Q_QUEUED;
	}

	/* This I/O operation has completed.  If all of them are, fio will not
	 * call fio_qemu_commit, so unplug immediately.
	 */
	qd->completed--;
	if (qd->to_submit == 0) {
		BlockBackend *blk = fio_qemu_get_blk(io_u->file);
		fio_qemu_test_and_clear_plugged(io_u->file);
		blk_io_unplug(blk);
	}

	return FIO_Q_COMPLETED;
}

static int fio_qemu_commit(struct thread_data *td)
{
	struct qemu_data *qd = td->io_ops_data;
	struct fio_file *file;
	int i;

	for_each_file(td, file, i) {
		if (fio_qemu_test_and_clear_plugged(file)) {
			BlockBackend *blk = fio_qemu_get_blk(file);
			blk_io_unplug(blk);
		}
	}
	qd->to_submit = 0;

	return 0;
}

static int fio_qemu_invalidate(struct thread_data *td, struct fio_file *file)
{
	return 0;
}
static void fio_qemu_cleanup(struct thread_data *td)
{
	struct qemu_data *qd = td->io_ops_data;

	if (qd) {
		aio_context_unref(qd->ctx);
		free(qd);
	}
}

static QDict *fio_qemu_opts(struct thread_data *td, struct fio_file *file)
{
	QDict *bs_opts;
	struct qemu_options *o = td->eo;

	bs_opts = qdict_new();
	if (td_read(td) && read_only)
		qdict_put(bs_opts, BDRV_OPT_READ_ONLY,
			  qstring_from_str("on"));
	qdict_put(bs_opts, BDRV_OPT_CACHE_DIRECT,
		  qstring_from_str(td->o.odirect ? "on" : "off"));
	if (o->format)
		qdict_put(bs_opts, "format", qstring_from_str(o->format));

	/* If no format is provided, but a driver is, skip the raw format.  */
	if (o->driver)
		qdict_put(bs_opts, !o->format ? "driver" : "file.driver",
			  qstring_from_str(o->driver));


	/* This is mostly a convenience, because the aio option of the file
	 * driver is commonly specified.
	 */
	if (o->aio)
		qdict_put(bs_opts, !o->format && o->driver ? "aio" : "file.aio",
			  qstring_from_str(o->aio));

	return bs_opts;
}

static int fio_qemu_get_file_size(struct thread_data *td, struct fio_file *file)
{
	Error *local_error = NULL;
	BlockBackend *blk;
	QDict *bs_opts;
	ImageInfo *info;

	bs_opts = fio_qemu_opts(td, file);
	qemu_mutex_lock(&iothread_lock);
	blk = blk_new_open(file->file_name, NULL, bs_opts, 0, &local_error);
	if (local_error) {
		struct qemu_options *o = td->eo;
		if (!td->o.create_on_open || !td->o.allow_create)
			goto err;

		error_free(local_error);
		local_error = NULL;

		bdrv_img_create(file->file_name, o->format ? : "raw", NULL, NULL,
				NULL, get_rand_file_size(td), 0, &local_error, false);
		if (local_error)
			goto err;

		bs_opts = fio_qemu_opts(td, file);
		blk = blk_new_open(file->file_name, NULL, bs_opts, 0, &local_error);
		if (local_error)
			goto err;
	}

	/* QDECREF(bs_opts); ??? */
	bdrv_query_image_info(blk_bs(blk), &info, &local_error);
	blk_unref(blk);
	qemu_mutex_unlock(&iothread_lock);

	file->real_file_size = info->virtual_size;
	fio_file_set_size_known(file);
	qapi_free_ImageInfo(info);

	return 0;
err:
	qemu_mutex_unlock(&iothread_lock);
	error_report_err(local_error);
	return -EINVAL;
}

static void fio_qemu_setup_globals(void)
{
	qemu_init_main_loop(&error_abort);
	qcrypto_init(&error_fatal);
	module_call_init(MODULE_INIT_QOM);
	bdrv_init();
	qemu_mutex_init(&iothread_lock);
}

static int fio_qemu_setup(struct thread_data *td)
{
	static pthread_once_t fio_qemu_globals = PTHREAD_ONCE_INIT;
	struct fio_file *file;
	int i;

	td->o.use_thread = 1;
	pthread_once(&fio_qemu_globals, fio_qemu_setup_globals);

        if (!td->o.file_size_low)
		td->o.file_size_low = td->o.file_size_high =
			td->o.size / td->o.nr_files;

	for_each_file(td, file, i) {
		int ret;
		dprint(FD_FILE, "get file size for %p/%d/%p\n", file, i,
								file->file_name);

		ret = fio_qemu_get_file_size(td, file);
		if (ret < 0) {
			log_err("%s\n", strerror(-ret));
			return 1;
		}
	}

	return 0;
}

static int fio_qemu_init(struct thread_data *td)
{
	size_t sz = sizeof(struct qemu_data) + td->o.iodepth * sizeof(struct io_u *);
	struct qemu_data *qd = malloc(sz);
	struct qemu_options *o = td->eo;
	Error *local_error = NULL;

	memset(qd, 0, sz);
	qd->ctx = aio_context_new(&error_abort);
	aio_context_set_poll_params(qd->ctx, o->poll_max_ns, 0, 0, &local_error);
	if (local_error) {
		error_report_err(local_error);
		return 1;
	}

	/* dlclosing QEMU leaves a pthread_key behind.  We'd need RTLD_NODELETE,
	 * but fio does not use it.  Instead, just prevent fio from dlclosing.
	 */
	td->io_ops_dlhandle = NULL;

	td->io_ops_data = qd;
	td->o.use_thread = 1;
	return 0;
}

static int fio_qemu_open_file(struct thread_data *td, struct fio_file *file)
{
	struct qemu_data *qd = td->io_ops_data;
	Error *local_error = NULL;
	QDict *bs_opts;
	BlockBackend *blk;

	qemu_mutex_lock(&iothread_lock);
	bs_opts = fio_qemu_opts(td, file);
	blk = blk_new_open(file->file_name, NULL, bs_opts, 0, &local_error);
	/* QDECREF(bs_opts); ??? */

	if (local_error) {
		error_report_err(local_error);
		return -EINVAL;
	}

	blk_set_aio_context(blk, qd->ctx);
	blk_set_enable_write_cache(blk, !td->o.sync_io);
	qemu_mutex_unlock(&iothread_lock);

	file->engine_data = (uintptr_t)blk;
	td->o.open_files ++;
	return 0;
}

static int fio_qemu_close_file(struct thread_data *td, struct fio_file *file)
{
	BlockBackend *blk = fio_qemu_get_blk(file);

	if (blk) {
		blk_unref(blk);
		file->engine_data = 0;
	}

	return 0;
}

struct ioengine_ops ioengine = {
	.name		= "qemu",
	.version	= FIO_IOOPS_VERSION,
	.init		= fio_qemu_init,
	.queue		= fio_qemu_queue,
	.commit		= fio_qemu_commit,
	.getevents	= fio_qemu_getevents,
	.event		= fio_qemu_event,
	.invalidate	= fio_qemu_invalidate,
	.cleanup	= fio_qemu_cleanup,
	.setup		= fio_qemu_setup,
	.open_file	= fio_qemu_open_file,
	.close_file	= fio_qemu_close_file,
	.options	= options,
	.option_struct_size = sizeof(struct qemu_options),
};
