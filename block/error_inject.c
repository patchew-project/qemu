/*
 * Error injection code for block devices
 *
 * Copyright (c) 2019 Red Hat, Inc.
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

#include "error_inject.h"

#include <gmodule.h>

#include "qemu/thread.h"

static QemuMutex error_inject_lock;
static GHashTable *error_inject_data;


static void delete_lba_entries(void *entry)
{
    GSequence *e = (GSequence *) entry;
    g_sequence_free(e);
}

struct value {
    uint64_t error_lba;

    /*
     * TODO Actually do something with behavior
     */
    MediaErrorBehavior behavior;
    /*
     * TODO Add data for generating bitrot, when we do change free function
     */
};

static int key_compare(gconstpointer a, gconstpointer b, gpointer data)
{
    uint64_t left = ((struct value *)a)->error_lba;
    uint64_t right = ((struct value *)b)->error_lba;

    if (left < right) {
        return -1;
    } else if (left > right) {
        return 1;
    } else {
        return 0;
    }
}

static uint64_t error_lba_get(GSequenceIter *iter)
{
    gpointer tmp = g_sequence_get(iter);
    return ((struct value *)tmp)->error_lba;
}

void media_error_create(const char *device_id, uint64_t lba,
                        MediaErrorBehavior behavior)
{
    qemu_mutex_lock(&error_inject_lock);

    GSequence *block_device = g_hash_table_lookup(error_inject_data, device_id);
    if (!block_device) {
        block_device = g_sequence_new(g_free);
        char *key = strdup(device_id);
        g_hash_table_insert(error_inject_data, key, block_device);
    }

    struct value lookup = {lba, MEDIA_ERROR_BEHAVIOR__MAX};
    if (!g_sequence_lookup(block_device, &lookup, key_compare, NULL)) {
        struct value *val = g_new(struct value, 1);
        val->error_lba = lba;
        val->behavior = behavior;

        g_sequence_insert_sorted(block_device, val, key_compare, NULL);
    }

    qemu_mutex_unlock(&error_inject_lock);
}

void media_error_delete(const char *device_id, uint64_t lba)
{
    qemu_mutex_lock(&error_inject_lock);

    GSequence *block_device = g_hash_table_lookup(error_inject_data, device_id);
    if (block_device) {
        struct value find = { lba, MEDIA_ERROR_BEHAVIOR__MAX};
        GSequenceIter *found = g_sequence_lookup(block_device, &find,
                                                 key_compare, NULL);
        if (found) {
            g_sequence_remove(found);
        }
    }

    qemu_mutex_unlock(&error_inject_lock);
}

int error_in_read(const char *device_id, uint64_t lba, uint64_t len,
                  uint64_t *error_lba)
{
    uint64_t error_sector = 0;
    const uint64_t transfer_end = lba + len;
    int ec = 0;
    *error_lba = 0xFFFFFFFFFFFFFFFF;

    qemu_mutex_lock(&error_inject_lock);

    GSequence *block_device = g_hash_table_lookup(error_inject_data, device_id);
    if (block_device && g_sequence_get_length(block_device) != 0) {
        struct value find = {lba, MEDIA_ERROR_BEHAVIOR__MAX};
        GSequenceIter *iter = g_sequence_search(block_device, &find,
                                                key_compare, NULL);

        /*
         * g_sequence_seach returns where the item would be inserted.
         * In the case of a direct match, it's spot is inserted after the
         * existing, thus we need to check the one immediately before
         * the insertion point to see if it is the one we are looking for.
         */
        GSequenceIter *prev = g_sequence_iter_prev(iter);
        error_sector = error_lba_get(prev);

        if (error_sector >= lba && error_sector < transfer_end) {
            *error_lba = error_sector;
            ec = 1;
        } else {
            /*
             * Lets look at next until we find one in our transfer or bail
             * if the error(s) logical block address are greater than the
             * end of our transfer.
             */
            while (!g_sequence_iter_is_end(iter)) {
                error_sector = error_lba_get(iter);

                if (error_sector >= transfer_end) {
                    break;
                }
                if (error_sector >= lba && error_sector < transfer_end) {
                    *error_lba = error_sector;
                    ec = 1;
                    break;
                } else {
                    iter = g_sequence_iter_next(iter);
                }
            }
        }
    }

    qemu_mutex_unlock(&error_inject_lock);

    return ec;
}


static void __attribute__((__constructor__)) error_inject_init(void)
{
    qemu_mutex_init(&error_inject_lock);

    error_inject_data = g_hash_table_new_full(g_str_hash,
                                              g_str_equal,
                                              g_free,
                                              delete_lba_entries);
}
