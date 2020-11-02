#include "qemu/osdep.h"
#include "qemu/error-report.h"

#include "hw/virtio/virtio-net.h" /* VIRTIO_NET_RSS_MAX_TABLE_LEN */

#include "ebpf/ebpf_rss.h"
#include "ebpf/ebpf.h"
#include "ebpf/tun_rss_steering.h"
#include "trace.h"

void ebpf_rss_init(struct EBPFRSSContext *ctx)
{
    if (ctx != NULL) {
        ctx->program_fd = -1;
    }
}

bool ebpf_rss_is_loaded(struct EBPFRSSContext *ctx)
{
    return ctx != NULL && ctx->program_fd >= 0;
}

bool ebpf_rss_load(struct EBPFRSSContext *ctx)
{
    if (ctx == NULL) {
        return false;
    }

    ctx->map_configuration =
            bpf_create_map(BPF_MAP_TYPE_ARRAY, sizeof(uint32_t),
                           sizeof(struct EBPFRSSConfig), 1);
    if (ctx->map_configuration < 0) {
        trace_ebpf_error("eBPF RSS", "can not create MAP for configurations");
        goto l_conf_create;
    }
    ctx->map_toeplitz_key =
            bpf_create_map(BPF_MAP_TYPE_ARRAY, sizeof(uint32_t),
                           VIRTIO_NET_RSS_MAX_KEY_SIZE, 1);
    if (ctx->map_toeplitz_key < 0) {
        trace_ebpf_error("eBPF RSS", "can not create MAP for toeplitz key");
        goto l_toe_create;
    }

    ctx->map_indirections_table =
            bpf_create_map(BPF_MAP_TYPE_ARRAY, sizeof(uint32_t),
                           sizeof(uint16_t), VIRTIO_NET_RSS_MAX_TABLE_LEN);
    if (ctx->map_indirections_table < 0) {
        trace_ebpf_error("eBPF RSS", "can not create MAP for indirections table");
        goto l_table_create;
    }

    bpf_fixup_mapfd(reltun_rss_steering,
            sizeof(reltun_rss_steering) / sizeof(struct fixup_mapfd_t),
            instun_rss_steering,
            sizeof(instun_rss_steering) / sizeof(struct bpf_insn),
            "tap_rss_map_configurations", ctx->map_configuration);

    bpf_fixup_mapfd(reltun_rss_steering,
            sizeof(reltun_rss_steering) / sizeof(struct fixup_mapfd_t),
            instun_rss_steering,
            sizeof(instun_rss_steering) / sizeof(struct bpf_insn),
            "tap_rss_map_toeplitz_key", ctx->map_toeplitz_key);

    bpf_fixup_mapfd(reltun_rss_steering,
            sizeof(reltun_rss_steering) / sizeof(struct fixup_mapfd_t),
            instun_rss_steering,
            sizeof(instun_rss_steering) / sizeof(struct bpf_insn),
            "tap_rss_map_indirection_table", ctx->map_indirections_table);

    ctx->program_fd =
            bpf_prog_load(BPF_PROG_TYPE_SOCKET_FILTER, instun_rss_steering,
                         sizeof(instun_rss_steering) / sizeof(struct bpf_insn),
                         "GPL");
    if (ctx->program_fd < 0) {
        trace_ebpf_error("eBPF RSS", "can not load eBPF program");
        goto l_prog_load;
    }

    return true;
l_prog_load:
    close(ctx->map_indirections_table);
l_table_create:
    close(ctx->map_toeplitz_key);
l_toe_create:
    close(ctx->map_configuration);
l_conf_create:
    return false;
}

static bool ebpf_rss_set_config(struct EBPFRSSContext *ctx,
                                struct EBPFRSSConfig *config)
{
    if (!ebpf_rss_is_loaded(ctx)) {
        return false;
    }
    uint32_t map_key = 0;
    if (bpf_update_elem(ctx->map_configuration,
                            &map_key, config, BPF_ANY) < 0) {
        return false;
    }
    return true;
}

static bool ebpf_rss_set_indirections_table(struct EBPFRSSContext *ctx,
                                            uint16_t *indirections_table,
                                            size_t len)
{
    if (!ebpf_rss_is_loaded(ctx) || indirections_table == NULL ||
       len > VIRTIO_NET_RSS_MAX_TABLE_LEN) {
        return false;
    }
    uint32_t i = 0;

    for (; i < len; ++i) {
        if (bpf_update_elem(ctx->map_indirections_table, &i,
                                indirections_table + i, BPF_ANY) < 0) {
            return false;
        }
    }
    return true;
}

static bool ebpf_rss_set_toepliz_key(struct EBPFRSSContext *ctx,
                                     uint8_t *toeplitz_key)
{
    if (!ebpf_rss_is_loaded(ctx) || toeplitz_key == NULL) {
        return false;
    }
    uint32_t map_key = 0;

    /* prepare toeplitz key */
    uint8_t toe[VIRTIO_NET_RSS_MAX_KEY_SIZE] = {};
    memcpy(toe, toeplitz_key, VIRTIO_NET_RSS_MAX_KEY_SIZE);
    *(uint32_t *)toe = ntohl(*(uint32_t *)toe);

    if (bpf_update_elem(ctx->map_toeplitz_key, &map_key, toe,
                            BPF_ANY) < 0) {
        return false;
    }
    return true;
}

bool ebpf_rss_set_all(struct EBPFRSSContext *ctx, struct EBPFRSSConfig *config,
                      uint16_t *indirections_table, uint8_t *toeplitz_key)
{
    if (!ebpf_rss_is_loaded(ctx) || config == NULL ||
        indirections_table == NULL || toeplitz_key == NULL) {
        return false;
    }

    if (!ebpf_rss_set_config(ctx, config)) {
        return false;
    }

    if (!ebpf_rss_set_indirections_table(ctx, indirections_table,
                                      config->indirections_len)) {
        return false;
    }

    if (!ebpf_rss_set_toepliz_key(ctx, toeplitz_key)) {
        return false;
    }

    return true;
}

void ebpf_rss_unload(struct EBPFRSSContext *ctx)
{
    if (!ebpf_rss_is_loaded(ctx)) {
        return;
    }

    close(ctx->program_fd);
    close(ctx->map_configuration);
    close(ctx->map_toeplitz_key);
    close(ctx->map_indirections_table);
    ctx->program_fd = -1;
}
