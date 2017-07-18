/*
 * QEMU System Emulator
 *
 * Copyright (c) 2015-2017 Cambridge Greys Limited
 * Copyright (c) 2012-2014 Cisco Systems
 * Copyright (c) 2003-2008 Fabrice Bellard
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

#include "qemu/osdep.h"


#define BUFFER_ALIGN sysconf(_SC_PAGESIZE)
#define BUFFER_SIZE 2048
#define IOVSIZE 2
#define MAX_UNIFIED_MSGCNT 64
#define MAX_UNIFIED_IOVCNT (MAX_UNIFIED_MSGCNT * IOVSIZE)

#ifndef QEMU_NET_UNIFIED_H
#define QEMU_NET_UNIFIED_H

typedef struct NetUnifiedState {
    NetClientState nc;

    int fd;

    /*
     * these are used for xmit - that happens packet a time
     * and for first sign of life packet (easier to parse that once)
     */

    uint8_t *header_buf;
    struct iovec *vec;

    /*
     * these are used for receive - try to "eat" up to 32 packets at a time
     */

    struct mmsghdr *msgvec;

    /*
     * peer address
     */

    struct sockaddr_storage *dgram_dst;
    uint32_t dst_size;

    /*
     * Internal Queue
     */

    /*
    * DOS avoidance in error handling
    */

    /* Easier to keep l2tpv3 specific */

    bool header_mismatch;

    /*
     *
     * Ring buffer handling
     *
     */

    int queue_head;
    int queue_tail;
    int queue_depth;

    /*
     * Offset to data - common for all protocols
     */

    uint32_t offset;

    /*
     * Header size - common for all protocols
     */

    uint32_t header_size;
    /* Poll Control */

    bool read_poll;
    bool write_poll;

    /* Parameters */

    void *params;

    /* header forming functions */

    int (*verify_header)(void *s, uint8_t *buf);
    void (*form_header)(void *s);

} NetUnifiedState;

extern NetClientState *qemu_new_unified_net_client(const char *name,
                    NetClientState *peer);

extern void qemu_net_finalize_unified_init(NetUnifiedState *s, int fd);
#endif
