// SPDX-License-Identifier: (MIT OR GPL-2.0)

/*
 * Copyright © 2021 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#ifndef _UAPI_LINUX_VIRTIO_VDMABUF_H
#define _UAPI_LINUX_VIRTIO_VDMABUF_H

typedef struct {
	__u64 id;
	/* 8B long Random number */
	int rng_key[2];
} virtio_vdmabuf_buf_id_t;

struct virtio_vdmabuf_e_hdr {
	/* buf_id of new buf */
	virtio_vdmabuf_buf_id_t buf_id;
	/* size of private data */
	int size;
};

struct virtio_vdmabuf_e_data {
	struct virtio_vdmabuf_e_hdr hdr;
	/* ptr to private data */
	void *data;
};

#define VIRTIO_VDMABUF_IOCTL_IMPORT \
_IOC(_IOC_NONE, 'G', 2, sizeof(struct virtio_vdmabuf_import))
#define VIRTIO_VDMABUF_IOCTL_RELEASE \
_IOC(_IOC_NONE, 'G', 3, sizeof(struct virtio_vdmabuf_import))
struct virtio_vdmabuf_import {
	/* IN parameters */
	/* vdmabuf id to be imported */
	virtio_vdmabuf_buf_id_t buf_id;
	/* flags */
	int flags;
	/* OUT parameters */
	/* exported dma buf fd */
	int fd;
};

#define VIRTIO_VDMABUF_IOCTL_EXPORT \
_IOC(_IOC_NONE, 'G', 4, sizeof(struct virtio_vdmabuf_export))
struct virtio_vdmabuf_export {
	/* IN parameters */
	/* DMA buf fd to be exported */
	int fd;
	/* exported dma buf id */
	virtio_vdmabuf_buf_id_t buf_id;
	int sz_priv;
	char *priv;
};

#endif
