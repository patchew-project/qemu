/*
 * Cache for small sequential write requests.
 *
 * Copyright (c) 2021 Virtuozzo International GmbH.
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

#ifndef QEMU_SEQCACHE_H
#define QEMU_SEQCACHE_H

typedef struct SeqCache SeqCache;

SeqCache *seqcache_new(int64_t cluster_size);
void seqcache_free(SeqCache *s);

void seqcache_write(SeqCache *s, int64_t offset, int64_t bytes, uint8_t *buf);
int64_t seqcache_read(SeqCache *s, int64_t offset, int64_t bytes, uint8_t *buf);

bool seqcache_get_next_flush(SeqCache *s, int64_t *offset, int64_t *bytes,
                             uint8_t **buf, bool *unfinished);
void seqcache_discard_cluster(SeqCache *s, int64_t offset);

uint64_t seqcache_nb_clusters(SeqCache *s);

#endif /* QEMU_SEQCACHE_H */
