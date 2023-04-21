// SPDX-License-Identifier: GPL-2.0

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/iov.h"
#include "trace.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-gpu.h"
#include "hw/virtio/virtio-gpu-pixman.h"
#include "hw/virtio/virtio-iommu.h"

#include <rutabaga_gfx/rutabaga_gfx_ffi.h>

static int virtio_gpu_rutabaga_init(VirtIOGPU *g);

#define GET_VIRTIO_GPU_GL(x)                                                  \
        VirtIOGPUGL *virtio_gpu = VIRTIO_GPU_GL(x);                           \

#define GET_RUTABAGA(x)                                                       \
        struct rutabaga *rutabaga = (struct rutabaga *)(x->rutabaga);         \

#define CHECK(condition, cmd)                                                 \
    do {                                                                      \
        if (!condition) {                                                     \
            qemu_log_mask(LOG_GUEST_ERROR, "CHECK_RESULT failed in %s() %s:"  \
                          "%d\n", __func__, __FILE__, __LINE__);              \
            cmd->error = VIRTIO_GPU_RESP_ERR_UNSPEC;                          \
            return;                                                           \
       }                                                                      \
    } while (0)

#define CHECK_RESULT(result, cmd) CHECK(result == 0, cmd)

struct rutabaga_aio_data {
    struct VirtIOGPUGL *virtio_gpu;
    struct rutabaga_fence fence;
};

static void
virtio_gpu_rutabaga_update_cursor(VirtIOGPU *g, struct virtio_gpu_scanout *s,
                                  uint32_t resource_id)
{
    struct virtio_gpu_simple_resource *res;
    struct rutabaga_transfer transfer = { 0 };
    struct iovec transfer_iovec;

    GET_VIRTIO_GPU_GL(g);
    GET_RUTABAGA(virtio_gpu);

    res = virtio_gpu_find_resource(g, resource_id);
    if (!res) {
        return;
    }

    if (res->width != s->current_cursor->width ||
        res->height != s->current_cursor->height) {
        return;
    }

    transfer.x = 0;
    transfer.y = 0;
    transfer.z = 0;
    transfer.w = res->width;
    transfer.h = res->height;
    transfer.d = 1;

    transfer_iovec.iov_base = (void *)s->current_cursor->data;
    transfer_iovec.iov_len = res->width * res->height * 4;

    rutabaga_resource_transfer_read(rutabaga, 0,
                                    resource_id, &transfer,
                                    &transfer_iovec);
}

static void
virtio_gpu_rutabaga_gl_flushed(VirtIOGPUBase *b)
{
    VirtIOGPU *g = VIRTIO_GPU(b);
    virtio_gpu_process_cmdq(g);
}

static void
rutabaga_cmd_create_resource_2d(VirtIOGPU *g,
                                struct virtio_gpu_ctrl_command *cmd)
{
    int32_t result;
    struct rutabaga_create_3d rc_3d = { 0 };
    struct virtio_gpu_simple_resource *res;
    struct virtio_gpu_resource_create_2d c2d;

    GET_VIRTIO_GPU_GL(g);
    GET_RUTABAGA(virtio_gpu);

    VIRTIO_GPU_FILL_CMD(c2d);
    trace_virtio_gpu_cmd_res_create_2d(c2d.resource_id, c2d.format,
                                       c2d.width, c2d.height);

    rc_3d.target = 2;
    rc_3d.format = c2d.format;
    rc_3d.bind = (1 << 1);
    rc_3d.width = c2d.width;
    rc_3d.height = c2d.height;
    rc_3d.depth = 1;
    rc_3d.array_size = 1;
    rc_3d.last_level = 0;
    rc_3d.nr_samples = 0;
    rc_3d.flags = VIRTIO_GPU_RESOURCE_FLAG_Y_0_TOP;

    result = rutabaga_resource_create_3d(rutabaga, c2d.resource_id, &rc_3d);
    CHECK_RESULT(result, cmd);

    res = g_new0(struct virtio_gpu_simple_resource, 1);
    res->width = c2d.width;
    res->height = c2d.height;
    res->format = c2d.format;
    res->resource_id = c2d.resource_id;

    QTAILQ_INSERT_HEAD(&g->reslist, res, next);
}

static void
rutabaga_cmd_create_resource_3d(VirtIOGPU *g,
                                struct virtio_gpu_ctrl_command *cmd)
{
    int32_t result;
    struct rutabaga_create_3d rc_3d = { 0 };
    struct virtio_gpu_simple_resource *res;
    struct virtio_gpu_resource_create_3d c3d;

    GET_VIRTIO_GPU_GL(g);
    GET_RUTABAGA(virtio_gpu);

    VIRTIO_GPU_FILL_CMD(c3d);

    trace_virtio_gpu_cmd_res_create_3d(c3d.resource_id, c3d.format,
                                       c3d.width, c3d.height, c3d.depth);

    rc_3d.target = c3d.target;
    rc_3d.format = c3d.format;
    rc_3d.bind = c3d.bind;
    rc_3d.width = c3d.width;
    rc_3d.height = c3d.height;
    rc_3d.depth = c3d.depth;
    rc_3d.array_size = c3d.array_size;
    rc_3d.last_level = c3d.last_level;
    rc_3d.nr_samples = c3d.nr_samples;
    rc_3d.flags = c3d.flags;

    result = rutabaga_resource_create_3d(rutabaga, c3d.resource_id, &rc_3d);
    CHECK_RESULT(result, cmd);

    res = g_new0(struct virtio_gpu_simple_resource, 1);
    res->width = c3d.width;
    res->height = c3d.height;
    res->format = c3d.format;
    res->resource_id = c3d.resource_id;

    QTAILQ_INSERT_HEAD(&g->reslist, res, next);
}

static int32_t rutabaga_handle_unmap(VirtIOGPU *g,
                                     struct virtio_gpu_simple_resource *res)
{
    GET_VIRTIO_GPU_GL(g);
    GET_RUTABAGA(virtio_gpu);

    memory_region_transaction_begin();
    memory_region_set_enabled(&res->region, false);
    memory_region_del_subregion(&g->parent_obj.hostmem, &res->region);
    object_unparent(OBJECT(&res->region));
    memory_region_transaction_commit();

    res->mapped = NULL;
    return rutabaga_resource_unmap(rutabaga, res->resource_id);
}

static void
rutabaga_cmd_resource_unref(VirtIOGPU *g,
                            struct virtio_gpu_ctrl_command *cmd)
{
    int32_t result;
    struct virtio_gpu_simple_resource *res;
    struct virtio_gpu_resource_unref unref;

    GET_VIRTIO_GPU_GL(g);
    GET_RUTABAGA(virtio_gpu);

    VIRTIO_GPU_FILL_CMD(unref);

    trace_virtio_gpu_cmd_res_unref(unref.resource_id);

    res = virtio_gpu_find_resource(g, unref.resource_id);
    CHECK(res, cmd);

    result = rutabaga_resource_unref(rutabaga, unref.resource_id);
    CHECK_RESULT(result, cmd);

    if (res->image) {
        pixman_image_unref(res->image);
    }

    if (res->mapped) {
        result = rutabaga_handle_unmap(g, res);
        CHECK(result, cmd);
    }

    QTAILQ_REMOVE(&g->reslist, res, next);
    g_free(res);
}

static void
rutabaga_cmd_context_create(VirtIOGPU *g,
                            struct virtio_gpu_ctrl_command *cmd)
{
    int32_t result;
    struct virtio_gpu_ctx_create cc;

    GET_VIRTIO_GPU_GL(g);
    GET_RUTABAGA(virtio_gpu);

    VIRTIO_GPU_FILL_CMD(cc);
    trace_virtio_gpu_cmd_ctx_create(cc.hdr.ctx_id,
                                    cc.debug_name);

    result = rutabaga_context_create(rutabaga, cc.hdr.ctx_id, cc.context_init,
                                     cc.debug_name, cc.nlen);
    CHECK_RESULT(result, cmd);
}

static void
rutabaga_cmd_context_destroy(VirtIOGPU *g,
                             struct virtio_gpu_ctrl_command *cmd)
{
    int32_t result;
    struct virtio_gpu_ctx_destroy cd;

    GET_VIRTIO_GPU_GL(g);
    GET_RUTABAGA(virtio_gpu);

    VIRTIO_GPU_FILL_CMD(cd);
    trace_virtio_gpu_cmd_ctx_destroy(cd.hdr.ctx_id);

    result = rutabaga_context_destroy(rutabaga, cd.hdr.ctx_id);
    CHECK_RESULT(result, cmd);
}

static void
rutabaga_cmd_resource_flush(VirtIOGPU *g, struct virtio_gpu_ctrl_command *cmd)
{
    int32_t result, i;
    struct virtio_gpu_scanout *scanout = NULL;
    struct virtio_gpu_simple_resource *res;
    struct rutabaga_transfer transfer = { 0 };
    struct iovec transfer_iovec;
    struct virtio_gpu_resource_flush rf;
    bool found = false;

    GET_VIRTIO_GPU_GL(g);
    GET_RUTABAGA(virtio_gpu);

    VIRTIO_GPU_FILL_CMD(rf);
    trace_virtio_gpu_cmd_res_flush(rf.resource_id,
                                   rf.r.width, rf.r.height, rf.r.x, rf.r.y);

    res = virtio_gpu_find_resource(g, rf.resource_id);
    CHECK(res, cmd);

    for (i = 0; i < g->parent_obj.conf.max_outputs; i++) {
        scanout = &g->parent_obj.scanout[i];
        if (i == res->scanout_bitmask) {
            found = true;
            break;
        }
    }

    if (!found) {
        return;
    }

    transfer.x = 0;
    transfer.y = 0;
    transfer.z = 0;
    transfer.w = res->width;
    transfer.h = res->height;
    transfer.d = 1;

    transfer_iovec.iov_base = (void *)pixman_image_get_data(res->image);
    transfer_iovec.iov_len = res->width * res->height * 4;

    result = rutabaga_resource_transfer_read(rutabaga, 0,
                                             rf.resource_id, &transfer,
                                             &transfer_iovec);
    CHECK_RESULT(result, cmd);
    dpy_gfx_update_full(scanout->con);
}

static void
rutabaga_cmd_set_scanout(VirtIOGPU *g, struct virtio_gpu_ctrl_command *cmd)
{
    struct virtio_gpu_simple_resource *res;
    struct virtio_gpu_scanout *scanout = NULL;
    struct virtio_gpu_set_scanout ss;

    VIRTIO_GPU_FILL_CMD(ss);
    trace_virtio_gpu_cmd_set_scanout(ss.scanout_id, ss.resource_id,
                                     ss.r.width, ss.r.height, ss.r.x, ss.r.y);

    scanout = &g->parent_obj.scanout[ss.scanout_id];
    g->parent_obj.enable = 1;

    res = virtio_gpu_find_resource(g, ss.resource_id);
    CHECK(res, cmd);

    if (!res->image) {
        pixman_format_code_t pformat;
        pformat = virtio_gpu_get_pixman_format(res->format);
        CHECK(pformat, cmd);

        res->image = pixman_image_create_bits(pformat,
                                              res->width,
                                              res->height,
                                              NULL, 0);
        CHECK(res->image, cmd);
        pixman_image_ref(res->image);
    }

    /* realloc the surface ptr */
    scanout->ds = qemu_create_displaysurface_pixman(res->image);
    dpy_gfx_replace_surface(scanout->con, NULL);
    dpy_gfx_replace_surface(scanout->con, scanout->ds);
    res->scanout_bitmask = ss.scanout_id;
}

static void
rutabaga_cmd_submit_3d(VirtIOGPU *g,
                       struct virtio_gpu_ctrl_command *cmd)
{
    int32_t result;
    struct virtio_gpu_cmd_submit cs;
    void *buf;
    size_t s;

    GET_VIRTIO_GPU_GL(g);
    GET_RUTABAGA(virtio_gpu);

    VIRTIO_GPU_FILL_CMD(cs);
    trace_virtio_gpu_cmd_ctx_submit(cs.hdr.ctx_id, cs.size);

    buf = g_malloc(cs.size);
    s = iov_to_buf(cmd->elem.out_sg, cmd->elem.out_num,
                   sizeof(cs), buf, cs.size);
    if (s != cs.size) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: size mismatch (%zd/%d)",
                      __func__, s, cs.size);
        cmd->error = VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER;
        goto out;
    }

    result = rutabaga_submit_command(rutabaga, cs.hdr.ctx_id, buf, cs.size);
    CHECK_RESULT(result, cmd);

out:
    g_free(buf);
}

static void
rutabaga_cmd_transfer_to_host_2d(VirtIOGPU *g,
                                 struct virtio_gpu_ctrl_command *cmd)
{
    int32_t result;
    struct rutabaga_transfer transfer = { 0 };
    struct virtio_gpu_transfer_to_host_2d t2d;

    GET_VIRTIO_GPU_GL(g);
    GET_RUTABAGA(virtio_gpu);

    VIRTIO_GPU_FILL_CMD(t2d);
    trace_virtio_gpu_cmd_res_xfer_toh_2d(t2d.resource_id);

    transfer.x = t2d.r.x;
    transfer.y = t2d.r.y;
    transfer.z = 0;
    transfer.w = t2d.r.width;
    transfer.h = t2d.r.height;
    transfer.d = 1;

    result = rutabaga_resource_transfer_write(rutabaga, 0, t2d.resource_id,
                                              &transfer);
    CHECK_RESULT(result, cmd);
}

static void
rutabaga_cmd_transfer_to_host_3d(VirtIOGPU *g,
                                 struct virtio_gpu_ctrl_command *cmd)
{
    int32_t result;
    struct rutabaga_transfer transfer = { 0 };
    struct virtio_gpu_transfer_host_3d t3d;

    GET_VIRTIO_GPU_GL(g);
    GET_RUTABAGA(virtio_gpu);

    VIRTIO_GPU_FILL_CMD(t3d);
    trace_virtio_gpu_cmd_res_xfer_toh_3d(t3d.resource_id);

    transfer.x = t3d.box.x;
    transfer.y = t3d.box.y;
    transfer.z = t3d.box.z;
    transfer.w = t3d.box.w;
    transfer.h = t3d.box.h;
    transfer.d = t3d.box.d;
    transfer.level = t3d.level;
    transfer.stride = t3d.stride;
    transfer.layer_stride = t3d.layer_stride;
    transfer.offset = t3d.offset;

    result = rutabaga_resource_transfer_write(rutabaga, t3d.hdr.ctx_id,
                                              t3d.resource_id, &transfer);
    CHECK_RESULT(result, cmd);
}

static void
rutabaga_cmd_transfer_from_host_3d(VirtIOGPU *g,
                                   struct virtio_gpu_ctrl_command *cmd)
{
    int32_t result;
    struct rutabaga_transfer transfer = { 0 };
    struct virtio_gpu_transfer_host_3d t3d;

    GET_VIRTIO_GPU_GL(g);
    GET_RUTABAGA(virtio_gpu);

    VIRTIO_GPU_FILL_CMD(t3d);
    trace_virtio_gpu_cmd_res_xfer_fromh_3d(t3d.resource_id);

    transfer.x = t3d.box.x;
    transfer.y = t3d.box.y;
    transfer.z = t3d.box.z;
    transfer.w = t3d.box.w;
    transfer.h = t3d.box.h;
    transfer.d = t3d.box.d;
    transfer.level = t3d.level;
    transfer.stride = t3d.stride;
    transfer.layer_stride = t3d.layer_stride;
    transfer.offset = t3d.offset;

    result = rutabaga_resource_transfer_read(rutabaga, t3d.hdr.ctx_id,
                                             t3d.resource_id, &transfer, NULL);
    CHECK_RESULT(result, cmd);
}

static void
rutabaga_cmd_attach_backing(VirtIOGPU *g, struct virtio_gpu_ctrl_command *cmd)
{
    struct rutabaga_iovecs vecs = { 0 };
    struct virtio_gpu_simple_resource *res;
    struct virtio_gpu_resource_attach_backing att_rb;
    struct iovec *res_iovs;
    uint32_t res_niov;
    int ret;

    GET_VIRTIO_GPU_GL(g);
    GET_RUTABAGA(virtio_gpu);

    VIRTIO_GPU_FILL_CMD(att_rb);
    trace_virtio_gpu_cmd_res_back_attach(att_rb.resource_id);

    res = virtio_gpu_find_resource(g, att_rb.resource_id);
    if (!res) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: illegal resource specified %d\n",
                      __func__, att_rb.resource_id);
        cmd->error = VIRTIO_GPU_RESP_ERR_UNSPEC;
        return;
    }

    if (res->iov) {
        cmd->error = VIRTIO_GPU_RESP_ERR_UNSPEC;
        return;
    }

    ret = virtio_gpu_create_mapping_iov(g, att_rb.nr_entries, sizeof(att_rb),
                                        cmd, NULL, &res_iovs, &res_niov);
    if (ret != 0) {
        cmd->error = VIRTIO_GPU_RESP_ERR_UNSPEC;
        return;
    }

    vecs.iovecs = res_iovs;
    vecs.num_iovecs = res_niov;

    ret = rutabaga_resource_attach_backing(rutabaga, att_rb.resource_id, &vecs);
    if (ret != 0) {
        virtio_gpu_cleanup_mapping_iov(g, res_iovs, res_niov);
    }
}

static void
rutabaga_cmd_detach_backing(VirtIOGPU *g, struct virtio_gpu_ctrl_command *cmd)
{
    struct virtio_gpu_simple_resource *res;
    struct virtio_gpu_resource_detach_backing detach_rb;

    GET_VIRTIO_GPU_GL(g);
    GET_RUTABAGA(virtio_gpu);

    VIRTIO_GPU_FILL_CMD(detach_rb);
    trace_virtio_gpu_cmd_res_back_detach(detach_rb.resource_id);

    res = virtio_gpu_find_resource(g, detach_rb.resource_id);
    CHECK(res, cmd);

    rutabaga_resource_detach_backing(rutabaga,
                                     detach_rb.resource_id);

    virtio_gpu_cleanup_mapping(g, res);
}

static void
rutabaga_cmd_ctx_attach_resource(VirtIOGPU *g,
                                 struct virtio_gpu_ctrl_command *cmd)
{
    int32_t result;
    struct virtio_gpu_ctx_resource att_res;

    GET_VIRTIO_GPU_GL(g);
    GET_RUTABAGA(virtio_gpu);

    VIRTIO_GPU_FILL_CMD(att_res);
    trace_virtio_gpu_cmd_ctx_res_attach(att_res.hdr.ctx_id,
                                        att_res.resource_id);

    result = rutabaga_context_attach_resource(rutabaga, att_res.hdr.ctx_id,
                                              att_res.resource_id);
    CHECK_RESULT(result, cmd);
}

static void
rutabaga_cmd_ctx_detach_resource(VirtIOGPU *g,
                                 struct virtio_gpu_ctrl_command *cmd)
{
    int32_t result;
    struct virtio_gpu_ctx_resource det_res;

    GET_VIRTIO_GPU_GL(g);
    GET_RUTABAGA(virtio_gpu);

    VIRTIO_GPU_FILL_CMD(det_res);
    trace_virtio_gpu_cmd_ctx_res_detach(det_res.hdr.ctx_id,
                                        det_res.resource_id);

    result = rutabaga_context_detach_resource(rutabaga, det_res.hdr.ctx_id,
                                              det_res.resource_id);
    CHECK_RESULT(result, cmd);
}

static void
rutabaga_cmd_get_capset_info(VirtIOGPU *g, struct virtio_gpu_ctrl_command *cmd)
{
    int32_t result;
    struct virtio_gpu_get_capset_info info;
    struct virtio_gpu_resp_capset_info resp;

    GET_VIRTIO_GPU_GL(g);
    GET_RUTABAGA(virtio_gpu);

    VIRTIO_GPU_FILL_CMD(info);

    result = rutabaga_get_capset_info(rutabaga, info.capset_index,
                                      &resp.capset_id, &resp.capset_max_version,
                                      &resp.capset_max_size);
    CHECK_RESULT(result, cmd);

    resp.hdr.type = VIRTIO_GPU_RESP_OK_CAPSET_INFO;
    virtio_gpu_ctrl_response(g, cmd, &resp.hdr, sizeof(resp));
}

static void
rutabaga_cmd_get_capset(VirtIOGPU *g, struct virtio_gpu_ctrl_command *cmd)
{
    int32_t result;
    struct virtio_gpu_get_capset gc;
    struct virtio_gpu_resp_capset *resp;
    uint32_t capset_size;
    uint32_t current_id;
    bool found = false;

    GET_VIRTIO_GPU_GL(g);
    GET_RUTABAGA(virtio_gpu);

    VIRTIO_GPU_FILL_CMD(gc);
    for (uint32_t i = 0; i < virtio_gpu->num_capsets; i++) {
        result = rutabaga_get_capset_info(rutabaga, i,
                                          &current_id, &capset_size,
                                          &capset_size);
        CHECK_RESULT(result, cmd);

        if (current_id == gc.capset_id) {
            found = true;
            break;
        }
    }

    if (!found) {
        qemu_log_mask(LOG_GUEST_ERROR, "capset not found!");
        return;
    }

    resp = g_malloc0(sizeof(*resp) + capset_size);
    resp->hdr.type = VIRTIO_GPU_RESP_OK_CAPSET;
    rutabaga_get_capset(rutabaga, gc.capset_id, gc.capset_version,
                       (uint8_t *)resp->capset_data, capset_size);

    virtio_gpu_ctrl_response(g, cmd, &resp->hdr, sizeof(*resp) + capset_size);
    g_free(resp);
}

static void
rutabaga_cmd_resource_create_blob(VirtIOGPU *g,
                                  struct virtio_gpu_ctrl_command *cmd)
{
    int result;
    struct rutabaga_iovecs vecs = { 0 };
    struct virtio_gpu_simple_resource *res;
    struct virtio_gpu_resource_create_blob cblob;
    struct rutabaga_create_blob rc_blob = { 0 };

    GET_VIRTIO_GPU_GL(g);
    GET_RUTABAGA(virtio_gpu);

    VIRTIO_GPU_FILL_CMD(cblob);
    trace_virtio_gpu_cmd_res_create_blob(cblob.resource_id, cblob.size);

    CHECK(cblob.resource_id != 0, cmd);

    res = g_new0(struct virtio_gpu_simple_resource, 1);
    QTAILQ_INSERT_HEAD(&g->reslist, res, next);

    res->resource_id = cblob.resource_id;
    res->blob_size = cblob.size;

    if (cblob.blob_mem != VIRTIO_GPU_BLOB_MEM_HOST3D) {
        result = virtio_gpu_create_mapping_iov(g, cblob.nr_entries,
                                               sizeof(cblob), cmd, &res->addrs,
                                               &res->iov, &res->iov_cnt);
        CHECK_RESULT(result, cmd);
    }

    rc_blob.blob_id = cblob.blob_id;
    rc_blob.blob_mem = cblob.blob_mem;
    rc_blob.blob_flags = cblob.blob_flags;
    rc_blob.size = cblob.size;

    vecs.iovecs = res->iov;
    vecs.num_iovecs = res->iov_cnt;

    result = rutabaga_resource_create_blob(rutabaga, cblob.hdr.ctx_id,
                                           cblob.resource_id, &rc_blob, &vecs,
                                           NULL);
    CHECK_RESULT(result, cmd);
}

static void
rutabaga_cmd_resource_map_blob(VirtIOGPU *g,
                               struct virtio_gpu_ctrl_command *cmd)
{
    int32_t result;
    struct virtio_gpu_simple_resource *res;
    struct rutabaga_mapping mapping = { 0 };
    struct virtio_gpu_resource_map_blob mblob;
    struct virtio_gpu_resp_map_info resp;

    GET_VIRTIO_GPU_GL(g);
    GET_RUTABAGA(virtio_gpu);

    VIRTIO_GPU_FILL_CMD(mblob);

    CHECK(mblob.resource_id != 0, cmd);

    res = virtio_gpu_find_resource(g, mblob.resource_id);
    CHECK(res, cmd);

    result = rutabaga_resource_map(rutabaga, mblob.resource_id, &mapping);
    CHECK_RESULT(result, cmd);

    memory_region_transaction_begin();
    memory_region_init_ram_device_ptr(&res->region, OBJECT(g), NULL,
                                      mapping.size, (void *)mapping.ptr);
    memory_region_add_subregion(&g->parent_obj.hostmem, mblob.offset,
                                &res->region);
    memory_region_set_enabled(&res->region, true);
    memory_region_transaction_commit();

    memset(&resp, 0, sizeof(resp));
    resp.hdr.type = VIRTIO_GPU_RESP_OK_MAP_INFO;
    result = rutabaga_resource_map_info(rutabaga, mblob.resource_id,
                                        &resp.map_info);

    CHECK_RESULT(result, cmd);
    virtio_gpu_ctrl_response(g, cmd, &resp.hdr, sizeof(resp));
}

static void
rutabaga_cmd_resource_unmap_blob(VirtIOGPU *g,
                                 struct virtio_gpu_ctrl_command *cmd)
{
    int32_t result;
    struct virtio_gpu_simple_resource *res;
    struct virtio_gpu_resource_unmap_blob ublob;

    VIRTIO_GPU_FILL_CMD(ublob);

    CHECK(ublob.resource_id != 0, cmd);

    res = virtio_gpu_find_resource(g, ublob.resource_id);
    CHECK(res, cmd);

    result = rutabaga_handle_unmap(g, res);
    CHECK_RESULT(result, cmd);
}

static void
virtio_gpu_rutabaga_process_cmd(VirtIOGPU *g,
                                struct virtio_gpu_ctrl_command *cmd)
{
    struct rutabaga_fence fence = { 0 };
    int32_t result;

    GET_VIRTIO_GPU_GL(g);
    GET_RUTABAGA(virtio_gpu);

    VIRTIO_GPU_FILL_CMD(cmd->cmd_hdr);

    switch (cmd->cmd_hdr.type) {
    case VIRTIO_GPU_CMD_CTX_CREATE:
        rutabaga_cmd_context_create(g, cmd);
        break;
    case VIRTIO_GPU_CMD_CTX_DESTROY:
        rutabaga_cmd_context_destroy(g, cmd);
        break;
    case VIRTIO_GPU_CMD_RESOURCE_CREATE_2D:
        rutabaga_cmd_create_resource_2d(g, cmd);
        break;
    case VIRTIO_GPU_CMD_RESOURCE_CREATE_3D:
        rutabaga_cmd_create_resource_3d(g, cmd);
        break;
    case VIRTIO_GPU_CMD_SUBMIT_3D:
        rutabaga_cmd_submit_3d(g, cmd);
        break;
    case VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D:
        rutabaga_cmd_transfer_to_host_2d(g, cmd);
        break;
    case VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D:
        rutabaga_cmd_transfer_to_host_3d(g, cmd);
        break;
    case VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D:
        rutabaga_cmd_transfer_from_host_3d(g, cmd);
        break;
    case VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING:
        rutabaga_cmd_attach_backing(g, cmd);
        break;
    case VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING:
        rutabaga_cmd_detach_backing(g, cmd);
        break;
    case VIRTIO_GPU_CMD_SET_SCANOUT:
        rutabaga_cmd_set_scanout(g, cmd);
        break;
    case VIRTIO_GPU_CMD_RESOURCE_FLUSH:
        rutabaga_cmd_resource_flush(g, cmd);
        break;
    case VIRTIO_GPU_CMD_RESOURCE_UNREF:
        rutabaga_cmd_resource_unref(g, cmd);
        break;
    case VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE:
        rutabaga_cmd_ctx_attach_resource(g, cmd);
        break;
    case VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE:
        rutabaga_cmd_ctx_detach_resource(g, cmd);
        break;
    case VIRTIO_GPU_CMD_GET_CAPSET_INFO:
        rutabaga_cmd_get_capset_info(g, cmd);
        break;
    case VIRTIO_GPU_CMD_GET_CAPSET:
        rutabaga_cmd_get_capset(g, cmd);
        break;
    case VIRTIO_GPU_CMD_GET_DISPLAY_INFO:
        virtio_gpu_get_display_info(g, cmd);
        break;
    case VIRTIO_GPU_CMD_GET_EDID:
        virtio_gpu_get_edid(g, cmd);
        break;
    case VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB:
        rutabaga_cmd_resource_create_blob(g, cmd);
        break;
    case VIRTIO_GPU_CMD_RESOURCE_MAP_BLOB:
        rutabaga_cmd_resource_map_blob(g, cmd);
        break;
    case VIRTIO_GPU_CMD_RESOURCE_UNMAP_BLOB:
        rutabaga_cmd_resource_unmap_blob(g, cmd);
        break;
    default:
        cmd->error = VIRTIO_GPU_RESP_ERR_UNSPEC;
        break;
    }

    if (cmd->finished) {
        return;
    }
    if (cmd->error) {
        fprintf(stderr, "%s: ctrl 0x%x, error 0x%x\n", __func__,
                cmd->cmd_hdr.type, cmd->error);
        virtio_gpu_ctrl_response_nodata(g, cmd, cmd->error);
        return;
    }
    if (!(cmd->cmd_hdr.flags & VIRTIO_GPU_FLAG_FENCE)) {
        virtio_gpu_ctrl_response_nodata(g, cmd, VIRTIO_GPU_RESP_OK_NODATA);
        return;
    }

    fence.flags = cmd->cmd_hdr.flags;
    fence.ctx_id = cmd->cmd_hdr.ctx_id;
    fence.fence_id = cmd->cmd_hdr.fence_id;
    fence.ring_idx = cmd->cmd_hdr.ring_idx;

    trace_virtio_gpu_fence_ctrl(cmd->cmd_hdr.fence_id, cmd->cmd_hdr.type);

    result = rutabaga_create_fence(rutabaga, &fence);
    CHECK_RESULT(result, cmd);
}

static void
virtio_gpu_rutabaga_aio_cb(void *opaque)
{
    struct rutabaga_aio_data *data =  (struct rutabaga_aio_data *)opaque;
    VirtIOGPU *g = (VirtIOGPU *)data->virtio_gpu;
    struct rutabaga_fence fence_data = data->fence;
    struct virtio_gpu_ctrl_command *cmd, *tmp;

    bool signaled_ctx_specific = fence_data.flags & RUTABAGA_FLAG_INFO_RING_IDX;

    QTAILQ_FOREACH_SAFE(cmd, &g->fenceq, next, tmp) {
        /*
         * Due to context specific timelines.
         */
        bool target_ctx_specific = cmd->cmd_hdr.flags &
                                   RUTABAGA_FLAG_INFO_RING_IDX;

        if (signaled_ctx_specific != target_ctx_specific) {
            continue;
        }

        if (signaled_ctx_specific &&
           (cmd->cmd_hdr.ring_idx != fence_data.ring_idx)) {
            continue;
        }

        if (cmd->cmd_hdr.fence_id > fence_data.fence_id) {
            continue;
        }

        trace_virtio_gpu_fence_resp(cmd->cmd_hdr.fence_id);
        virtio_gpu_ctrl_response_nodata(g, cmd, VIRTIO_GPU_RESP_OK_NODATA);
        QTAILQ_REMOVE(&g->fenceq, cmd, next);
        g_free(cmd);
    }

    g_free(data);
}

static void
virtio_gpu_rutabaga_fence_cb(uint64_t user_data,
                             struct rutabaga_fence fence_data) {
    struct rutabaga_aio_data *data;
    VirtIOGPU *g = (VirtIOGPU *)(void*)(uintptr_t)user_data;
    GET_VIRTIO_GPU_GL(g);

    data = g_new0(struct rutabaga_aio_data, 1);
    data->virtio_gpu = virtio_gpu;
    data->fence = fence_data;
    aio_bh_schedule_oneshot_full(virtio_gpu->ctx, virtio_gpu_rutabaga_aio_cb,
                                 (void *)data, "aio");
}

static int virtio_gpu_rutabaga_init(VirtIOGPU *g)
{
    int result;
    uint64_t capset_mask;
    struct rutabaga_channels channels = { 0 };
    struct rutabaga_builder builder = { 0 };

    GET_VIRTIO_GPU_GL(g);
    virtio_gpu->rutabaga = NULL;

    if (!virtio_gpu->capset_names) {
        return -EINVAL;
    }

    result = rutabaga_calculate_capset_mask(virtio_gpu->capset_names,
                                            &capset_mask);
    if (result) {
        return result;
    }

    /*
     * rutabaga-0.1.1 is only compiled/tested with gfxstream and cross-domain
     * support. Future versions may change this to have more context types if
     * there is any interest.
     */
    if ((capset_mask & (1 << RUTABAGA_CAPSET_VIRGL)) ||
        (capset_mask & (1 << RUTABAGA_CAPSET_VIRGL2)) ||
        (capset_mask & (1 << RUTABAGA_CAPSET_VENUS)) ||
        (capset_mask & (1 << RUTABAGA_CAPSET_DRM))) {
        return -EINVAL;
     }

    builder.user_data = (uint64_t)(uintptr_t *)(void *)g;
    builder.fence_cb = virtio_gpu_rutabaga_fence_cb;
    builder.capset_mask = capset_mask;

    if (virtio_gpu->wayland_socket_path) {
        if ((builder.capset_mask & (1 << RUTABAGA_CAPSET_CROSS_DOMAIN)) == 0) {
            return -EINVAL;
        }

        channels.channels =
          (struct rutabaga_channel *)calloc(1, sizeof(struct rutabaga_channel));
        channels.num_channels = 1;
        channels.channels[0].channel_name = virtio_gpu->wayland_socket_path;
        channels.channels[0].channel_type = RUTABAGA_CHANNEL_TYPE_WAYLAND;
        builder.channels = &channels;
    }

    result = rutabaga_init(&builder, (struct rutabaga **)&virtio_gpu->rutabaga);
    if (builder.capset_mask & (1 << RUTABAGA_CAPSET_CROSS_DOMAIN)) {
        free(channels.channels);
    }

    virtio_gpu->ctx = qemu_get_aio_context();
    return result;
}

static int virtio_gpu_rutabaga_get_num_capsets(VirtIOGPU *g)
{
    int result = 0;
    uint32_t num_capsets;
    GET_VIRTIO_GPU_GL(g);

    if (!virtio_gpu->renderer_inited) {
        result = virtio_gpu_rutabaga_init(g);
        if (result) {
            qemu_log_mask(LOG_GUEST_ERROR, "Failed to init rutabaga");
            return 0;
        }

        virtio_gpu->renderer_inited = true;
    }

    GET_RUTABAGA(virtio_gpu);

    result = rutabaga_get_num_capsets(rutabaga, &num_capsets);
    if (result) {
        qemu_log_mask(LOG_GUEST_ERROR, "Failed to get capsets");
        return 0;
    }
    virtio_gpu->num_capsets = num_capsets;
    return (int)(num_capsets);
}

static void virtio_gpu_rutabaga_handle_ctrl(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOGPU *g = VIRTIO_GPU(vdev);
    GET_VIRTIO_GPU_GL(g);
    struct virtio_gpu_ctrl_command *cmd;

    if (!virtio_queue_ready(vq)) {
        return;
    }

    if (!virtio_gpu->renderer_inited) {
        int result = virtio_gpu_rutabaga_init(g);
        if (!result) {
            virtio_gpu->renderer_inited = true;
        }
    }

    if (!virtio_gpu->renderer_inited) {
        return;
    }

    cmd = virtqueue_pop(vq, sizeof(struct virtio_gpu_ctrl_command));
    while (cmd) {
        cmd->vq = vq;
        cmd->error = 0;
        cmd->finished = false;
        QTAILQ_INSERT_TAIL(&g->cmdq, cmd, next);
        cmd = virtqueue_pop(vq, sizeof(struct virtio_gpu_ctrl_command));
    }

    virtio_gpu_process_cmdq(g);
}

void virtio_gpu_rutabaga_device_realize(DeviceState *qdev, Error **errp)
{
    int num_capsets;
    VirtIOGPUBase *bdev = VIRTIO_GPU_BASE(qdev);
    VirtIOGPUBaseClass *vbc = VIRTIO_GPU_BASE_GET_CLASS(bdev);

    VirtIOGPU *gpudev = VIRTIO_GPU(qdev);
    VirtIOGPUClass *vgc = VIRTIO_GPU_GET_CLASS(gpudev);

    num_capsets = virtio_gpu_rutabaga_get_num_capsets(gpudev);
    if (!num_capsets) {
        return;
    }

    vbc->gl_flushed = virtio_gpu_rutabaga_gl_flushed;
    vgc->handle_ctrl = virtio_gpu_rutabaga_handle_ctrl;
    vgc->process_cmd = virtio_gpu_rutabaga_process_cmd;
    vgc->update_cursor_data = virtio_gpu_rutabaga_update_cursor;

#if HOST_BIG_ENDIAN
    error_setg(errp, "rutabaga is not supported on bigendian platforms");
    return;
#endif

    gpudev->parent_obj.conf.flags |= (1 << VIRTIO_GPU_FLAG_RUTABAGA_ENABLED);
    gpudev->parent_obj.conf.flags |= (1 << VIRTIO_GPU_FLAG_BLOB_ENABLED);
    gpudev->parent_obj.conf.flags
      |= (1 << VIRTIO_GPU_FLAG_CONTEXT_INIT_ENABLED);

    VIRTIO_GPU_BASE(gpudev)->virtio_config.num_capsets = num_capsets;
    virtio_gpu_device_realize(qdev, errp);
}
