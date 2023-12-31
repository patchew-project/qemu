/*
 * Multifd QATzip compression implementation
 *
 * Copyright (c) Bytedance
 *
 * Authors:
 *  Bryan Zhang <bryan.zhang@bytedance.com>
 *  Hao Xiang   <hao.xiang@bytedance.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "exec/ramblock.h"
#include "exec/target_page.h"
#include "qapi/error.h"
#include "migration.h"
#include "options.h"
#include "multifd.h"
#include <qatzip.h>

struct qatzip_data {
    /*
     * Unique session for use with QATzip API
     */
    QzSession_T sess;

    /*
     * For compression: Buffer for pages to compress
     * For decompression: Buffer for data to decompress
     */
    uint8_t *in_buf;
    uint32_t in_len;

    /*
     * For compression: Output buffer of compressed data
     * For decompression: Output buffer of decompressed data
     */
    uint8_t *out_buf;
    uint32_t out_len;
};

/**
 * qatzip_send_setup: Set up QATzip session and private buffers.
 *
 * @param p    Multifd channel params
 * @param errp Pointer to error, which will be set in case of error
 * @return     0 on success, -1 on error (and *errp will be set)
 */
static int qatzip_send_setup(MultiFDSendParams *p, Error **errp)
{
    struct qatzip_data *q;
    QzSessionParamsDeflate_T params;
    const char *err_msg;
    int ret;

    q = g_new0(struct qatzip_data, 1);
    p->data = q;

    ret = qzInit(&q->sess, 0);
    if (ret != QZ_OK && ret != QZ_DUPLICATE) {
        err_msg = "qzInit failed";
        goto err_free_q;
    }

    ret = qzGetDefaultsDeflate(&params);
    if (ret != QZ_OK) {
        err_msg = "qzGetDefaultsDeflate failed";
        goto err_close;
    }

    /* Use maximum hardware buffer size to improve batching. */
    params.common_params.hw_buff_sz = QZ_HW_BUFF_MAX_SZ;

    /* Make sure to use configured QATzip compression level. */
    params.common_params.comp_lvl = migrate_multifd_qatzip_level();

    ret = qzSetupSessionDeflate(&q->sess, &params);
    if (ret != QZ_OK && ret != QZ_DUPLICATE) {
        err_msg = "qzSetupSessionDeflate failed";
        goto err_close;
    }

    /* TODO Add support for larger packets. */
    if (MULTIFD_PACKET_SIZE > UINT32_MAX) {
        err_msg = "packet size too large for QAT";
        goto err_close;
    }

    q->in_len = MULTIFD_PACKET_SIZE;
    q->in_buf = g_try_malloc(q->in_len);
    if (!q->in_buf) {
        err_msg = "malloc failed";
        goto err_close;
    }

    q->out_len = qzMaxCompressedLength(MULTIFD_PACKET_SIZE, &q->sess);
    q->out_buf = g_try_malloc(q->out_len);
    if (!q->out_buf) {
        err_msg = "malloc failed";
        goto err_free_inbuf;
    }

    return 0;

err_free_inbuf:
    g_free(q->in_buf);
err_close:
    qzClose(&q->sess);
err_free_q:
    g_free(q);
    error_setg(errp, "multifd %u: %s", p->id, err_msg);
    return -1;
}

/**
 * qatzip_send_cleanup: Tear down QATzip session and release private buffers.
 *
 * @param p    Multifd channel params
 * @param errp Pointer to error, which will be set in case of error
 * @return     None
 */
static void qatzip_send_cleanup(MultiFDSendParams *p, Error **errp)
{
    struct qatzip_data *q = p->data;
    const char *err_msg;
    int ret;

    ret = qzTeardownSession(&q->sess);
    if (ret != QZ_OK) {
        err_msg = "qzTeardownSession failed";
        goto err;
    }

    ret = qzClose(&q->sess);
    if (ret != QZ_OK) {
        err_msg = "qzClose failed";
        goto err;
    }

    g_free(q->in_buf);
    q->in_buf = NULL;
    g_free(q->out_buf);
    q->out_buf = NULL;
    g_free(p->data);
    p->data = NULL;
    return;

err:
    error_setg(errp, "multifd %u: %s", p->id, err_msg);
}

/**
 * qatzip_send_prepare: Compress pages and update IO channel info.
 *
 * @param p    Multifd channel params
 * @param errp Pointer to error, which will be set in case of error
 * @return     0 on success, -1 on error (and *errp will be set)
 */
static int qatzip_send_prepare(MultiFDSendParams *p, Error **errp)
{
    struct qatzip_data *q = p->data;
    int ret;
    unsigned int in_len, out_len;

    /* memcpy all the pages into one buffer. */
    for (int i = 0; i < p->normal_num; i++) {
        memcpy(q->in_buf + (i * p->page_size),
               p->pages->block->host + p->normal[i],
               p->page_size);
    }

    in_len = p->normal_num * p->page_size;
    if (in_len > q->in_len) {
        error_setg(errp, "multifd %u: unexpectedly large input", p->id);
        return -1;
    }
    out_len = q->out_len;

    /*
     * Unlike other multifd compression implementations, we use a non-streaming
     * API and place all the data into one buffer, rather than sending each page
     * to the compression API at a time. Based on initial benchmarks, the
     * non-streaming API outperforms the streaming API. Plus, the logic in QEMU
     * is friendly to using the non-streaming API anyway. If either of these
     * statements becomes no longer true, we can revisit adding a streaming
     * implementation.
     */
    ret = qzCompress(&q->sess, q->in_buf, &in_len, q->out_buf, &out_len, 1);
    if (ret != QZ_OK) {
        error_setg(errp, "multifd %u: QATzip returned %d instead of QZ_OK",
                   p->id, ret);
        return -1;
    }
    if (in_len != p->normal_num * p->page_size) {
        error_setg(errp, "multifd %u: QATzip failed to compress all input",
                   p->id);
        return -1;
    }

    p->iov[p->iovs_num].iov_base = q->out_buf;
    p->iov[p->iovs_num].iov_len = out_len;
    p->iovs_num++;
    p->next_packet_size = out_len;
    p->flags |= MULTIFD_FLAG_QATZIP;
    return 0;
}

/**
 * qatzip_recv_setup: Set up QATzip session and allocate private buffers.
 *
 * @param p    Multifd channel params
 * @param errp Pointer to error, which will be set in case of error
 * @return     0 on success, -1 on error (and *errp will be set)
 */
static int qatzip_recv_setup(MultiFDRecvParams *p, Error **errp)
{
    struct qatzip_data *q;
    QzSessionParamsDeflate_T params;
    const char *err_msg;
    int ret;

    q = g_new0(struct qatzip_data, 1);
    p->data = q;

    ret = qzInit(&q->sess, 0);
    if (ret != QZ_OK && ret != QZ_DUPLICATE) {
        err_msg = "qzInit failed";
        goto err_free_q;
    }

    ret = qzGetDefaultsDeflate(&params);
    if (ret != QZ_OK) {
        err_msg = "qzGetDefaultsDeflate failed";
        goto err_close;
    }

    /* Set maximum hardware buffer size for improved batching. */
    params.common_params.hw_buff_sz = QZ_HW_BUFF_MAX_SZ;

    /* Make sure to use configured QATzip compression level. */
    params.common_params.comp_lvl = migrate_multifd_qatzip_level();

    ret = qzSetupSessionDeflate(&q->sess, &params);
    if (ret != QZ_OK && ret != QZ_DUPLICATE) {
        err_msg = "qzSetupSessionDeflate failed";
        goto err_close;
    }

    /*
     * Mimic multifd-zlib, which reserves extra space for the incoming packet.
     */
    q->in_len = MULTIFD_PACKET_SIZE * 2;
    q->in_buf = g_try_malloc(q->in_len);
    if (!q->in_buf) {
        err_msg = "malloc failed";
        goto err_close;
    }

    q->out_len = MULTIFD_PACKET_SIZE;
    q->out_buf = g_try_malloc(q->out_len);
    if (!q->out_buf) {
        err_msg = "malloc failed";
        goto err_free_inbuf;
    }

    return 0;

err_free_inbuf:
    g_free(q->in_buf);
err_close:
    qzClose(&q->sess);
err_free_q:
    g_free(q);
    error_setg(errp, "multifd %u: %s", p->id, err_msg);
    return -1;
}

/**
 * qatzip_recv_cleanup: Tear down QATzip session and release private buffers.
 *
 * @param p    Multifd channel params
 * @return     None
 */
static void qatzip_recv_cleanup(MultiFDRecvParams *p)
{
    struct qatzip_data *q = p->data;

    /* Ignoring return values here due to function signature. */
    qzTeardownSession(&q->sess);
    qzClose(&q->sess);
    g_free(q->in_buf);
    g_free(q->out_buf);
    g_free(p->data);
}


/**
 * qatzip_recv_pages: Decompress pages and copy them to the appropriate
 * locations.
 *
 * @param p    Multifd channel params
 * @param errp Pointer to error, which will be set in case of error
 * @return     0 on success, -1 on error (and *errp will be set)
 */
static int qatzip_recv_pages(MultiFDRecvParams *p, Error **errp)
{
    struct qatzip_data *q = p->data;
    int ret;
    unsigned int in_len, out_len;
    uint32_t in_size = p->next_packet_size;
    uint32_t expected_size = p->normal_num * p->page_size;
    uint32_t flags = p->flags & MULTIFD_FLAG_COMPRESSION_MASK;

    if (in_size > q->in_len) {
        error_setg(errp, "multifd %u: received unexpectedly large packet",
                   p->id);
        return -1;
    }

    if (flags != MULTIFD_FLAG_QATZIP) {
        error_setg(errp, "multifd %u: flags received %x flags expected %x",
                   p->id, flags, MULTIFD_FLAG_QATZIP);
        return -1;
    }

    ret = qio_channel_read_all(p->c, (void *)q->in_buf, in_size, errp);
    if (ret != 0) {
        return ret;
    }

    in_len = in_size;
    out_len = q->out_len;
    ret = qzDecompress(&q->sess, q->in_buf, &in_len, q->out_buf, &out_len);
    if (ret != QZ_OK) {
        error_setg(errp, "multifd %u: qzDecompress failed", p->id);
        return -1;
    }
    if (out_len != expected_size) {
        error_setg(errp, "multifd %u: packet size received %u size expected %u",
                   p->id, out_len, expected_size);
        return -1;
    }

    /* Copy each page to its appropriate location. */
    for (int i = 0; i < p->normal_num; i++) {
        memcpy(p->host + p->normal[i],
               q->out_buf + p->page_size * i,
               p->page_size);
    }
    return 0;
}

static MultiFDMethods multifd_qatzip_ops = {
    .send_setup = qatzip_send_setup,
    .send_cleanup = qatzip_send_cleanup,
    .send_prepare = qatzip_send_prepare,
    .recv_setup = qatzip_recv_setup,
    .recv_cleanup = qatzip_recv_cleanup,
    .recv_pages = qatzip_recv_pages
};

static void multifd_qatzip_register(void)
{
    multifd_register_ops(MULTIFD_COMPRESSION_QATZIP, &multifd_qatzip_ops);
}

migration_init(multifd_qatzip_register);
