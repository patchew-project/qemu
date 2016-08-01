/*
 * QEMU Block driver for Veritas HyperScale (VxHS)
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Please follow QEMU coding guidelines while modifying this file.
 * The QEMU coding guidelines need to be followed because this driver has
 * to be submitted to QEMU community in near futute and we want to prevent any
 * reduce the amount of work at that time.
 * QEMU coding guidelines can be found at :
 * http://git.qemu-project.org/?p=qemu.git;a=blob_plain;f=CODING_STYLE;hb=HEAD
 */

#ifndef VXHSD_H
#define VXHSD_H

#include <gmodule.h>
#include <inttypes.h>
#include <pthread.h>

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "block/block_int.h"
#include "qemu/uri.h"
#include "qemu/queue.h"

/*
 * #define VXHS_DEBUG
 */

#define vxhsErr(fmt, ...) { \
    time_t t = time(0); \
    char buf[9] = {0}; \
    strftime(buf, 9, "%H:%M:%S", localtime(&t)); \
    fprintf(stderr, "[%s: %lu] %d: %s():\t", buf, pthread_self(), \
                __LINE__, __func__); \
    fprintf(stderr, fmt, ## __VA_ARGS__); \
}

#ifdef VXHS_DEBUG
#define vxhsDbg vxhsErr
#else
#define vxhsDbg(...)        {/**/}
#endif

#define OF_GUID_STR_LEN             40
#define OF_GUID_STR_SZ              (OF_GUID_STR_LEN + 1)
#define QNIO_CONNECT_RETRY_SECS     5
#define QNIO_CONNECT_TIMOUT_SECS    120

/* constants from io_qnio.h */
#define IIO_REASON_DONE     0x00000004
#define IIO_REASON_EVENT    0x00000008
#define IIO_REASON_HUP      0x00000010

/*
 * IO specific flags
 */
#define IIO_FLAG_ASYNC      0x00000001
#define IIO_FLAG_DONE       0x00000010
#define IIO_FLAG_SYNC       0

/* constants from error.h */
#define VXERROR_RETRY_ON_SOURCE     44
#define VXERROR_HUP                 901
#define VXERROR_CHANNEL_HUP         903

/* constants from iomgr.h and opcode.h */
#define IRP_READ_REQUEST                    0x1FFF
#define IRP_WRITE_REQUEST                   0x2FFF
#define IRP_VDISK_CHECK_IO_FAILOVER_READY   2020

/* Lock specific macros */
#define VXHS_SPIN_LOCK_ALLOC                  \
    ((*qnioops.qemu_initialize_lock)())
#define VXHS_SPIN_LOCK(lock)                  \
    ((*qnioops.qemu_spin_lock)(lock))
#define VXHS_SPIN_UNLOCK(lock)                \
    ((*qnioops.qemu_spin_unlock)(lock))
#define VXHS_SPIN_LOCK_DESTROY(lock)          \
    ((*qnioops.qemu_destroy_lock)(lock))

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

typedef enum {
    VXHS_IO_INPROGRESS,
    VXHS_IO_COMPLETED,
    VXHS_IO_ERROR
} VXHSIOState;


typedef void *qemu_aio_ctx_t;
typedef void (*qnio_callback_t)(ssize_t retval, void *arg);

#define VDISK_FD_READ 0
#define VDISK_FD_WRITE 1

#define QNIO_VDISK_NONE        0x00
#define QNIO_VDISK_CREATE      0x01

/* max IO size supported by QEMU NIO lib */
#define QNIO_MAX_IO_SIZE       4194304

#define IP_ADDR_LEN             20
#define OF_MAX_FILE_LEN         1024
#define OF_MAX_SERVER_ADDR      1024
#define MAX_HOSTS               4

/*
 * Opcodes for making IOCTL on QEMU NIO library
 */
#define BASE_OPCODE_SHARED     1000
#define BASE_OPCODE_DAL        2000
#define IRP_VDISK_STAT                  (BASE_OPCODE_SHARED + 5)
#define IRP_VDISK_GET_GEOMETRY          (BASE_OPCODE_DAL + 17)
#define IRP_VDISK_READ_PARTITION        (BASE_OPCODE_DAL + 18)
#define IRP_VDISK_FLUSH                 (BASE_OPCODE_DAL + 19)

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
#define OF_ACB_QUEUED       0x00000001

#define OF_AIOCB_FLAGS_QUEUED(a)            \
        ((a)->flags & OF_ACB_QUEUED)
#define OF_AIOCB_FLAGS_SET_QUEUED(a)        \
        ((a)->flags |= OF_ACB_QUEUED)
#define OF_AIOCB_FLAGS_RESET_QUEUED(a)      \
        ((a)->flags &= ~OF_ACB_QUEUED)

char vdisk_prefix[] = "/dev/of/vdisk";

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

typedef void (*iio_cb_t) (uint32_t rfd, uint32_t reason, void *ctx,
                          void *reply);

typedef struct QNIOOps {
    void * (*qemu_iio_init)(iio_cb_t cb);
    int32_t (* qemu_open_iio_conn)(void *qnio_ctx, const char *uri,
                                       uint32_t flags);
    int32_t (*qemu_iio_devopen)(void *qnio_ctx, uint32_t cfd,
                                     const char *devpath, uint32_t flags);
    int32_t (*qemu_iio_devclose)(void *qnio_ctx, uint32_t rfd);
    int32_t (*qemu_iio_writev)(void *qnio_ctx, uint32_t rfd,
                                    struct iovec *iov, int iovcnt,
                                    uint64_t offset, void *ctx,
                                    uint32_t flags);
    int32_t (*qemu_iio_readv)(void *qnio_ctx, uint32_t rfd,
                                   struct iovec *iov, int iovcnt,
                                   uint64_t offset, void *ctx, uint32_t flags);
    int32_t (*qemu_iio_read)(void *qnio_ctx, uint32_t rfd,
                                  unsigned char *buf, uint64_t size,
                                  uint64_t offset, void *ctx, uint32_t flags);
    int32_t (*qemu_iio_ioctl)(void *apictx, uint32_t rfd, uint32_t opcode,
                                   void *in, void *ctx, uint32_t flags);
    int32_t (*qemu_iio_close)(void *qnio_ctx, uint32_t cfd);
    uint32_t (*qemu_iio_extract_msg_error)(void *ptr);
    size_t (*qemu_iio_extract_msg_size)(void *ptr);
    uint32_t (*qemu_iio_extract_msg_opcode)(void *ptr);
    void * (*qemu_initialize_lock)(void);
    void (*qemu_spin_lock)(void *ptr);
    void (*qemu_spin_unlock)(void *ptr);
    void (*qemu_destroy_lock)(void *ptr);
} QNIOOps;

int32_t qemu_open_qnio_conn(const char *uri, uint32_t lanes,
                               uint32_t flags);
int32_t qemu_iio_devopen(void *qnio_ctx, uint32_t cfd,
                         const char *devpath, uint32_t flags);
int32_t qemu_iio_writev(void *qnio_ctx, uint32_t rfd, void *ctx,
                        uint64_t offset, uint32_t count,
                        struct iovec *iov, uint32_t flags);
int32_t qemu_iio_readv(void *qnio_ctx, uint32_t rfd, void *ctx,
                       uint64_t offset, uint32_t count,
                       struct iovec *iov, uint32_t flags);

/*
 * HyperScale AIO callbacks structure
 */
typedef struct VXHSAIOCB {
    BlockAIOCB    common;
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
    VXHSAIOCB             *qnio_event_acb;
    void                    *qnio_ctx;
    void                    *vdisk_lock; /* Lock to protect BDRVVXHSState */
    void                    *vdisk_acb_lock;  /* Protects ACB */
    VXHSvDiskHostsInfo    vdisk_hostinfo[MAX_HOSTS]; /* Per host info */
    int                     vdisk_nhosts;   /* Total number of hosts */
    int                     vdisk_cur_host_idx; /* IOs are being shipped to */
    int                     vdisk_ask_failover_idx; /*asking permsn to ship io*/
    QSIMPLEQ_HEAD(aio_retryq, VXHSAIOCB) vdisk_aio_retryq;
    int                     vdisk_aio_retry_qd;
    char                    *vdisk_guid;
    AioContext              *aio_context;
} BDRVVXHSState;

int vxhs_load_iio_ops(void);
void bdrv_vxhs_init(void);
void *vxhs_initialize(void);
void *vxhs_setup_qnio(void);
int qemu_qnio_fini(BDRVVXHSState *s);
void vxhs_iio_callback(uint32_t rfd, uint32_t reason, void *ctx, void *m);
int qemu_qnio_init(BDRVVXHSState *s, const char *vxhs_uri);
void vxhs_aio_event_reader(void *opaque);
void vxhs_complete_aio(VXHSAIOCB *acb, BDRVVXHSState *s);
int vxhs_aio_flush_cb(void *opaque);
void vxhs_finish_aiocb(ssize_t ret, void *arg);
unsigned long vxhs_get_vdisk_stat(BDRVVXHSState *s);
int vxhs_open(BlockDriverState *bs, QDict *options,
              int bdrv_flags, Error **errp);
int vxhs_create(const char *filename, QemuOpts *options,
                Error **errp);
int vxhs_open_device(const char *vxhs_uri, int *cfd, int *rfd,
                       BDRVVXHSState *s);
void vxhs_close(BlockDriverState *bs);
void vxhs_aio_cancel(BlockAIOCB *blockacb);
int vxhs_truncate(BlockDriverState *bs, int64_t offset);
int qemu_submit_io(BDRVVXHSState *s, struct iovec *iov, int64_t niov,
                   int64_t offset, int cmd, qemu_aio_ctx_t acb);
BlockAIOCB *vxhs_aio_flush(BlockDriverState *bs,
                                   BlockCompletionFunc *cb, void *opaque);
BlockAIOCB *vxhs_aio_pdiscard(BlockDriverState *bs, int64_t sector_num,
                                     int nb_sectors,
                                     BlockCompletionFunc *cb,
                                     void *opaque);
BlockAIOCB *vxhs_aio_readv(BlockDriverState *bs, int64_t sector_num,
                                   QEMUIOVector *qiov, int nb_sectors,
                                   BlockCompletionFunc *cb, void *opaque);
BlockAIOCB *vxhs_aio_writev(BlockDriverState *bs, int64_t sector_num,
                                    QEMUIOVector *qiov, int nb_sectors,
                                    BlockCompletionFunc *cb,
                                    void *opaque);
coroutine_fn int vxhs_co_read(BlockDriverState *bs, int64_t sector_num,
                                uint8_t *buf, int nb_sectors);
coroutine_fn int vxhs_co_write(BlockDriverState *bs, int64_t sector_num,
                                 const uint8_t *buf, int nb_sectors);
int64_t vxhs_get_allocated_blocks(BlockDriverState *bs);
BlockAIOCB *vxhs_aio_rw(BlockDriverState *bs, int64_t sector_num,
                                QEMUIOVector *qiov, int nb_sectors,
                                BlockCompletionFunc *cb,
                                void *opaque, int write);
int vxhs_co_flush(BlockDriverState *bs);
coroutine_fn int vxhs_co_pdiscard(BlockDriverState *bs,
                                   int64_t sector_num, int nb_sectors);
int vxhs_has_zero_init(BlockDriverState *bs);
int64_t vxhs_getlength(BlockDriverState *bs);
inline void vxhs_inc_vdisk_iocount(void *ptr, uint32_t delta);
inline void vxhs_dec_vdisk_iocount(void *ptr, uint32_t delta);
inline uint32_t vxhs_get_vdisk_iocount(void *ptr);
void vxhs_inc_acb_segment_count(void *ptr, int count);
void vxhs_dec_acb_segment_count(void *ptr, int count);
inline int vxhs_dec_and_get_acb_segment_count(void *ptr, int count);
void vxhs_set_acb_buffer(void *ptr, void *buffer);
int vxhs_sync_rw(BlockDriverState *bs, int64_t sector_num,
                   const uint8_t *buf, int nb_sectors, int write);
int vxhs_failover_io(BDRVVXHSState *s);
int vxhs_reopen_vdisk(BDRVVXHSState *s,
                        int hostinfo_index);
int vxhs_switch_storage_agent(BDRVVXHSState *s);
int vxhs_check_io_failover_ready(BDRVVXHSState *s,
                                   int hostinfo_index);
int vxhs_build_io_target_list(BDRVVXHSState *s, int resiliency_count,
                              char **filenames);
int vxhs_handle_queued_ios(BDRVVXHSState *s);
int vxhs_restart_aio(VXHSAIOCB *acb);
void vxhs_fail_aio(VXHSAIOCB *acb, int err);
void failover_ioctl_cb(int res, void *ctx);
char *vxhs_string_iterate(char *p, const char *d, const size_t len);
int vxhs_tokenize(char **result, char *working, const char *src,
                  const char *delim);

#endif
