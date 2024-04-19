/*
 * SPDX-License-Identifier: MIT
 *
 * QemuDmaBuf struct and helpers used for accessing its data
 *
 * Copyright (c) 2024 Dongwon Kim <dongwon.kim@intel.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef DMABUF_H
#define DMABUF_H

typedef struct QemuDmaBuf QemuDmaBuf;

QemuDmaBuf *qemu_dmabuf_new(uint32_t width, uint32_t height,
                                   uint32_t stride, uint32_t x,
                                   uint32_t y, uint32_t backing_width,
                                   uint32_t backing_height, uint32_t fourcc,
                                   uint64_t modifier, int32_t dmabuf_fd,
                                   bool allow_fences, bool y0_top);
void qemu_dmabuf_free(QemuDmaBuf *dmabuf);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(QemuDmaBuf, qemu_dmabuf_free);

int32_t qemu_dmabuf_get_fd(QemuDmaBuf *dmabuf);
uint32_t qemu_dmabuf_get_width(QemuDmaBuf *dmabuf);
uint32_t qemu_dmabuf_get_height(QemuDmaBuf *dmabuf);
uint32_t qemu_dmabuf_get_stride(QemuDmaBuf *dmabuf);
uint32_t qemu_dmabuf_get_fourcc(QemuDmaBuf *dmabuf);
uint64_t qemu_dmabuf_get_modifier(QemuDmaBuf *dmabuf);
uint32_t qemu_dmabuf_get_texture(QemuDmaBuf *dmabuf);
uint32_t qemu_dmabuf_get_x(QemuDmaBuf *dmabuf);
uint32_t qemu_dmabuf_get_y(QemuDmaBuf *dmabuf);
uint32_t qemu_dmabuf_get_backing_width(QemuDmaBuf *dmabuf);
uint32_t qemu_dmabuf_get_backing_height(QemuDmaBuf *dmabuf);
bool qemu_dmabuf_get_y0_top(QemuDmaBuf *dmabuf);
void *qemu_dmabuf_get_sync(QemuDmaBuf *dmabuf);
int32_t qemu_dmabuf_get_fence_fd(QemuDmaBuf *dmabuf);
bool qemu_dmabuf_get_allow_fences(QemuDmaBuf *dmabuf);
bool qemu_dmabuf_get_draw_submitted(QemuDmaBuf *dmabuf);
void qemu_dmabuf_set_texture(QemuDmaBuf *dmabuf, uint32_t texture);
void qemu_dmabuf_set_fence_fd(QemuDmaBuf *dmabuf, int32_t fence_fd);
void qemu_dmabuf_set_sync(QemuDmaBuf *dmabuf, void *sync);
void qemu_dmabuf_set_draw_submitted(QemuDmaBuf *dmabuf, bool draw_submitted);
void qemu_dmabuf_set_fd(QemuDmaBuf *dmabuf, int32_t fd);

#endif
