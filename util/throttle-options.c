#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/throttle-options.h"
#include "qemu/iov.h"

void qmp_set_io_throttle(ThrottleConfig *cfg, IOThrottle *arg)
{
    throttle_config_init(cfg);
    cfg->buckets[THROTTLE_BPS_TOTAL].avg = arg->bps;
    cfg->buckets[THROTTLE_BPS_READ].avg  = arg->bps_rd;
    cfg->buckets[THROTTLE_BPS_WRITE].avg = arg->bps_wr;

    cfg->buckets[THROTTLE_OPS_TOTAL].avg = arg->iops;
    cfg->buckets[THROTTLE_OPS_READ].avg  = arg->iops_rd;
    cfg->buckets[THROTTLE_OPS_WRITE].avg = arg->iops_wr;

    if (arg->has_bps_max) {
        cfg->buckets[THROTTLE_BPS_TOTAL].max = arg->bps_max;
    }
    if (arg->has_bps_rd_max) {
        cfg->buckets[THROTTLE_BPS_READ].max = arg->bps_rd_max;
    }
    if (arg->has_bps_wr_max) {
        cfg->buckets[THROTTLE_BPS_WRITE].max = arg->bps_wr_max;
    }
    if (arg->has_iops_max) {
        cfg->buckets[THROTTLE_OPS_TOTAL].max = arg->iops_max;
    }
    if (arg->has_iops_rd_max) {
        cfg->buckets[THROTTLE_OPS_READ].max = arg->iops_rd_max;
    }
    if (arg->has_iops_wr_max) {
        cfg->buckets[THROTTLE_OPS_WRITE].max = arg->iops_wr_max;
    }

    if (arg->has_bps_max_length) {
        cfg->buckets[THROTTLE_BPS_TOTAL].burst_length = arg->bps_max_length;
    }
    if (arg->has_bps_rd_max_length) {
        cfg->buckets[THROTTLE_BPS_READ].burst_length = arg->bps_rd_max_length;
    }
    if (arg->has_bps_wr_max_length) {
        cfg->buckets[THROTTLE_BPS_WRITE].burst_length = arg->bps_wr_max_length;
    }
    if (arg->has_iops_max_length) {
        cfg->buckets[THROTTLE_OPS_TOTAL].burst_length = arg->iops_max_length;
    }
    if (arg->has_iops_rd_max_length) {
        cfg->buckets[THROTTLE_OPS_READ].burst_length = arg->iops_rd_max_length;
    }
    if (arg->has_iops_wr_max_length) {
        cfg->buckets[THROTTLE_OPS_WRITE].burst_length = arg->iops_wr_max_length;
    }

    if (arg->has_iops_size) {
        cfg->op_size = arg->iops_size;
    }
}
