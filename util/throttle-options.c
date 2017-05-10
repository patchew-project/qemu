#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/throttle-options.h"
#include "qemu/iov.h"

void parse_io_throttle_options(ThrottleConfig *throttle_cfg, QemuOpts *opts)
{
    throttle_config_init(throttle_cfg);
    throttle_cfg->buckets[THROTTLE_BPS_TOTAL].avg =
        qemu_opt_get_number(opts, "throttling.bps-total", 0);
    throttle_cfg->buckets[THROTTLE_BPS_READ].avg  =
        qemu_opt_get_number(opts, "throttling.bps-read", 0);
    throttle_cfg->buckets[THROTTLE_BPS_WRITE].avg =
        qemu_opt_get_number(opts, "throttling.bps-write", 0);
    throttle_cfg->buckets[THROTTLE_OPS_TOTAL].avg =
        qemu_opt_get_number(opts, "throttling.iops-total", 0);
    throttle_cfg->buckets[THROTTLE_OPS_READ].avg =
        qemu_opt_get_number(opts, "throttling.iops-read", 0);
    throttle_cfg->buckets[THROTTLE_OPS_WRITE].avg =
        qemu_opt_get_number(opts, "throttling.iops-write", 0);

    throttle_cfg->buckets[THROTTLE_BPS_TOTAL].max =
        qemu_opt_get_number(opts, "throttling.bps-total-max", 0);
    throttle_cfg->buckets[THROTTLE_BPS_READ].max  =
        qemu_opt_get_number(opts, "throttling.bps-read-max", 0);
    throttle_cfg->buckets[THROTTLE_BPS_WRITE].max =
        qemu_opt_get_number(opts, "throttling.bps-write-max", 0);
    throttle_cfg->buckets[THROTTLE_OPS_TOTAL].max =
        qemu_opt_get_number(opts, "throttling.iops-total-max", 0);
    throttle_cfg->buckets[THROTTLE_OPS_READ].max =
        qemu_opt_get_number(opts, "throttling.iops-read-max", 0);
    throttle_cfg->buckets[THROTTLE_OPS_WRITE].max =
        qemu_opt_get_number(opts, "throttling.iops-write-max", 0);

    throttle_cfg->buckets[THROTTLE_BPS_TOTAL].burst_length =
        qemu_opt_get_number(opts, "throttling.bps-total-max-length", 1);
    throttle_cfg->buckets[THROTTLE_BPS_READ].burst_length  =
        qemu_opt_get_number(opts, "throttling.bps-read-max-length", 1);
    throttle_cfg->buckets[THROTTLE_BPS_WRITE].burst_length =
        qemu_opt_get_number(opts, "throttling.bps-write-max-length", 1);
    throttle_cfg->buckets[THROTTLE_OPS_TOTAL].burst_length =
        qemu_opt_get_number(opts, "throttling.iops-total-max-length", 1);
    throttle_cfg->buckets[THROTTLE_OPS_READ].burst_length =
        qemu_opt_get_number(opts, "throttling.iops-read-max-length", 1);
    throttle_cfg->buckets[THROTTLE_OPS_WRITE].burst_length =
        qemu_opt_get_number(opts, "throttling.iops-write-max-length", 1);

    throttle_cfg->op_size =
        qemu_opt_get_number(opts, "throttling.iops-size", 0);

}
