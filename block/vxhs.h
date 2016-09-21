/*
 * QEMU Block driver for Veritas HyperScale (VxHS)
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef VXHSD_H
#define VXHSD_H

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "block/block_int.h"
#include "qemu/uri.h"
#include "qemu/queue.h"

#define QNIO_CONNECT_RETRY_SECS     5
#define QNIO_CONNECT_TIMOUT_SECS    120

/*
 * IO specific flags
 */
#define IIO_FLAG_ASYNC              0x00000001
#define IIO_FLAG_DONE               0x00000010
#define IIO_FLAG_SYNC               0

#define VDISK_FD_READ               0
#define VDISK_FD_WRITE              1
#define VXHS_MAX_HOSTS              4

/* Lock specific macros */
#define VXHS_SPIN_LOCK_ALLOC                  \
    (g_malloc(sizeof(QemuSpin)))
#define VXHS_SPIN_LOCK(lock)                  \
    (qemu_spin_lock(lock))
#define VXHS_SPIN_UNLOCK(lock)                \
    (qemu_spin_unlock(lock))
#define VXHS_SPIN_LOCK_DESTROY(lock)          \
    (g_free(lock))

typedef enum {
    VXHS_IO_INPROGRESS,
    VXHS_IO_COMPLETED,
    VXHS_IO_ERROR
} VXHSIOState;

typedef enum {
    VDISK_AIO_READ,
    VDISK_AIO_WRITE,
    VDISK_STAT,
    VDISK_TRUNC,
    VDISK_AIO_FLUSH,
    VDISK_AIO_RECLAIM,
    VDISK_GET_GEOMETRY,
    VDISK_CHECK_IO_FAILOVER_READY,
    VDISK_AIO_LAST_CMD
} VDISKAIOCmd;

typedef void (*qnio_callback_t)(ssize_t retval, void *arg);

/*
 * BDRVVXHSState specific flags
 */
#define OF_VDISK_FLAGS_STATE_ACTIVE             0x0000000000000001
#define OF_VDISK_FLAGS_STATE_FAILED             0x0000000000000002
#define OF_VDISK_FLAGS_IOFAILOVER_IN_PROGRESS   0x0000000000000004

#define OF_VDISK_ACTIVE(s)                                              \
        ((s)->vdisk_flags & OF_VDISK_FLAGS_STATE_ACTIVE)
#define OF_VDISK_SET_ACTIVE(s)                                          \
        ((s)->vdisk_flags |= OF_VDISK_FLAGS_STATE_ACTIVE)
#define OF_VDISK_RESET_ACTIVE(s)                                        \
        ((s)->vdisk_flags &= ~OF_VDISK_FLAGS_STATE_ACTIVE)

#define OF_VDISK_FAILED(s)                                              \
        ((s)->vdisk_flags & OF_VDISK_FLAGS_STATE_FAILED)
#define OF_VDISK_SET_FAILED(s)                                          \
        ((s)->vdisk_flags |= OF_VDISK_FLAGS_STATE_FAILED)
#define OF_VDISK_RESET_FAILED(s)                                        \
        ((s)->vdisk_flags &= ~OF_VDISK_FLAGS_STATE_FAILED)

#define OF_VDISK_IOFAILOVER_IN_PROGRESS(s)                              \
        ((s)->vdisk_flags & OF_VDISK_FLAGS_IOFAILOVER_IN_PROGRESS)
#define OF_VDISK_SET_IOFAILOVER_IN_PROGRESS(s)                          \
        ((s)->vdisk_flags |= OF_VDISK_FLAGS_IOFAILOVER_IN_PROGRESS)
#define OF_VDISK_RESET_IOFAILOVER_IN_PROGRESS(s)                        \
        ((s)->vdisk_flags &= ~OF_VDISK_FLAGS_IOFAILOVER_IN_PROGRESS)

/*
 * VXHSAIOCB specific flags
 */
#define OF_ACB_QUEUED               0x00000001

#define OF_AIOCB_FLAGS_QUEUED(a)            \
        ((a)->flags & OF_ACB_QUEUED)
#define OF_AIOCB_FLAGS_SET_QUEUED(a)        \
        ((a)->flags |= OF_ACB_QUEUED)
#define OF_AIOCB_FLAGS_RESET_QUEUED(a)      \
        ((a)->flags &= ~OF_ACB_QUEUED)

typedef struct qemu2qnio_ctx {
    uint32_t            qnio_flag;
    uint64_t            qnio_size;
    char                *qnio_channel;
    char                *target;
    qnio_callback_t     qnio_cb;
} qemu2qnio_ctx_t;

typedef qemu2qnio_ctx_t qnio2qemu_ctx_t;

typedef struct LibQNIOSymbol {
        const char *name;
        gpointer *addr;
} LibQNIOSymbol;

/*
 * HyperScale AIO callbacks structure
 */
typedef struct VXHSAIOCB {
    BlockAIOCB          common;
    size_t              ret;
    size_t              size;
    QEMUBH              *bh;
    int                 aio_done;
    int                 segments;
    int                 flags;
    size_t              io_offset;
    QEMUIOVector        *qiov;
    void                *buffer;
    int                 direction;  /* IO direction (r/w) */
    QSIMPLEQ_ENTRY(VXHSAIOCB) retry_entry;
} VXHSAIOCB;

typedef struct VXHSvDiskHostsInfo {
    int     qnio_cfd;        /* Channel FD */
    int     vdisk_rfd;      /* vDisk remote FD */
    char    *hostip;        /* Host's IP addresses */
    int     port;           /* Host's port number */
} VXHSvDiskHostsInfo;

/*
 * Structure per vDisk maintained for state
 */
typedef struct BDRVVXHSState {
    int                     fds[2];
    int64_t                 vdisk_size;
    int64_t                 vdisk_blocks;
    int64_t                 vdisk_flags;
    int                     vdisk_aio_count;
    int                     event_reader_pos;
    VXHSAIOCB               *qnio_event_acb;
    void                    *qnio_ctx;
    void                    *vdisk_lock; /* Lock to protect BDRVVXHSState */
    void                    *vdisk_acb_lock;  /* Protects ACB */
    VXHSvDiskHostsInfo      vdisk_hostinfo[VXHS_MAX_HOSTS]; /* Per host info */
    int                     vdisk_nhosts;   /* Total number of hosts */
    int                     vdisk_cur_host_idx; /* IOs are being shipped to */
    int                     vdisk_ask_failover_idx; /*asking permsn to ship io*/
    QSIMPLEQ_HEAD(aio_retryq, VXHSAIOCB) vdisk_aio_retryq;
    int                     vdisk_aio_retry_qd; /* Currently for debugging */
    char                    *vdisk_guid;
} BDRVVXHSState;

void bdrv_vxhs_init(void);
void *vxhs_setup_qnio(void);
void vxhs_iio_callback(int32_t rfd, uint32_t reason, void *ctx,
                                            uint32_t error, uint32_t opcode);
void vxhs_aio_event_reader(void *opaque);
void vxhs_complete_aio(VXHSAIOCB *acb, BDRVVXHSState *s);
unsigned long vxhs_get_vdisk_stat(BDRVVXHSState *s);
int vxhs_open(BlockDriverState *bs, QDict *options,
                                            int bdrv_flags, Error **errp);
void vxhs_close(BlockDriverState *bs);
BlockAIOCB *vxhs_aio_readv(BlockDriverState *bs, int64_t sector_num,
                                            QEMUIOVector *qiov, int nb_sectors,
                                            BlockCompletionFunc *cb,
                                            void *opaque);
BlockAIOCB *vxhs_aio_writev(BlockDriverState *bs, int64_t sector_num,
                                            QEMUIOVector *qiov, int nb_sectors,
                                            BlockCompletionFunc *cb,
                                            void *opaque);
int64_t vxhs_get_allocated_blocks(BlockDriverState *bs);
BlockAIOCB *vxhs_aio_rw(BlockDriverState *bs, int64_t sector_num,
                                            QEMUIOVector *qiov, int nb_sectors,
                                            BlockCompletionFunc *cb,
                                            void *opaque, int write);
int32_t vxhs_qnio_iio_writev(void *qnio_ctx, uint32_t rfd, struct iovec *iov,
                                            int iovcnt, uint64_t offset,
                                            void *ctx, uint32_t flags);
int32_t vxhs_qnio_iio_readv(void *qnio_ctx, uint32_t rfd, struct iovec *iov,
                                            int iovcnt, uint64_t offset,
                                            void *ctx, uint32_t flags);
int32_t vxhs_qnio_iio_ioctl(void *apictx, uint32_t rfd, uint32_t opcode,
                                            int64_t *in, void *ctx,
                                            uint32_t flags);
size_t vxhs_calculate_iovec_size(struct iovec *iov, int niov);
void vxhs_copy_iov_to_buffer(struct iovec *iov, int niov, void *buf);
void *vxhs_convert_iovector_to_buffer(struct iovec *iov, int niov,
                                            size_t sector);
int vxhs_is_iovector_read_aligned(struct iovec *iov, int niov, size_t sector);

int vxhs_aio_flush_cb(void *opaque);
int vxhs_co_flush(BlockDriverState *bs);
int64_t vxhs_getlength(BlockDriverState *bs);
void vxhs_inc_vdisk_iocount(void *ptr, uint32_t delta);
void vxhs_dec_vdisk_iocount(void *ptr, uint32_t delta);
uint32_t vxhs_get_vdisk_iocount(void *ptr);
void vxhs_inc_acb_segment_count(void *ptr, int count);
void vxhs_dec_acb_segment_count(void *ptr, int count);
void vxhs_set_acb_buffer(void *ptr, void *buffer);
int vxhs_failover_io(BDRVVXHSState *s);
int vxhs_reopen_vdisk(BDRVVXHSState *s, int hostinfo_index);
int vxhs_switch_storage_agent(BDRVVXHSState *s);
int vxhs_handle_queued_ios(BDRVVXHSState *s);
int vxhs_restart_aio(VXHSAIOCB *acb);
void vxhs_fail_aio(VXHSAIOCB *acb, int err);
void vxhs_check_failover_status(int res, void *ctx);

#endif
