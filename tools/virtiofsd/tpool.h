/*
 * custom threadpool for virtiofsd
 *
 * Copyright (C) 2021 Red Hat, Inc.
 *
 * Authors:
 *     Ioannis Angelakopoulos <iangelak@redhat.com>
 *     Vivek Goyal <vgoyal@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

struct fv_ThreadPool;

struct fv_ThreadPool *fv_thread_pool_init(unsigned int thread_num);
void fv_thread_pool_destroy(struct fv_ThreadPool *tpool);
void fv_thread_pool_push(struct fv_ThreadPool *tpool,
                   void (*worker_func)(void *, void *), void *arg1, void *arg2);
