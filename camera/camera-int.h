/*
 * QEMU Camera subsystem internal API header file
 *
 * Copyright 2021-2022 Bytedance, Inc.
 *
 * Authors:
 *   zhenwei pi <pizhenwei@bytedance.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef QEMU_CAMERA_INT_H
#define QEMU_CAMERA_INT_H

void qemu_camera_alloc_image(QEMUCamera *camera, size_t size, Error **errp);
void qemu_camera_free_image(QEMUCamera *camera);
void qemu_camera_new_image(QEMUCamera *camera, const void *addr, size_t size);

#endif /* QEMU_CAMERA_INT_H */
