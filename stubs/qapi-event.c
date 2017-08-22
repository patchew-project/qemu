#include "qemu/osdep.h"
#include "qapi-event.h"

void qapi_event_send_device_tray_moved(const char *device, const char *id,
                                       bool tray_open, Error **errp)
{
}

void qapi_event_send_quorum_report_bad(QuorumOpType type, bool has_error,
                                       const char *error, const char *node_name,
                                       int64_t sector_num,
                                       int64_t sectors_count, Error **errp)
{
}

void qapi_event_send_quorum_failure(const char *reference, int64_t sector_num,
                                    int64_t sectors_count, Error **errp)
{
}

void qapi_event_send_block_job_cancelled(BlockJobType type, const char *device,
                                         int64_t len, int64_t offset,
                                         int64_t speed, Error **errp)
{
}

void qapi_event_send_block_job_completed(BlockJobType type, const char *device,
                                         int64_t len, int64_t offset,
                                         int64_t speed, bool has_error,
                                         const char *error, Error **errp)
{
}

void qapi_event_send_block_job_error(const char *device,
                                     IoOperationType operation,
                                     BlockErrorAction action, Error **errp)
{
}

void qapi_event_send_block_job_ready(BlockJobType type, const char *device,
                                     int64_t len, int64_t offset, int64_t speed,
                                     Error **errp)
{
}

void qapi_event_send_block_io_error(const char *device, const char *node_name,
                                    IoOperationType operation,
                                    BlockErrorAction action, bool has_nospace,
                                    bool nospace, const char *reason,
                                    Error **errp)
{
}

void qapi_event_send_block_image_corrupted(const char *device,
                                           bool has_node_name,
                                           const char *node_name,
                                           const char *msg, bool has_offset,
                                           int64_t offset, bool has_size,
                                           int64_t size, bool fatal,
                                           Error **errp)
{
}

void qapi_event_send_block_write_threshold(const char *node_name,
                                           uint64_t amount_exceeded,
                                           uint64_t write_threshold,
                                           Error **errp)
{
}

void qapi_event_send_device_deleted(bool has_device, const char *device,
                                    const char *path, Error **errp)
{
}
