/*
 *  Memexpose core
 *
 *  Copyright (C) 2020 Samsung Electronics Co Ltd.
 *    Igor Kotrasinski, <i.kotrasinsk@partner.samsung.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "memexpose-core.h"
#include "exec/address-spaces.h"
#include "exec/cpu-common.h"

static int memexpose_pop_intr(MemexposeIntr *s)
{
    if (s->queue_count == 0) {
        MEMEXPOSE_DPRINTF("No queued interrupts\n");
        return 0;
    }
    struct memexpose_op_intr *head = &s->intr_queue[s->queue_start];
    s->intr_rx = *head;
    s->queue_start = (s->queue_start + 1) % MEMEXPOSE_INTR_QUEUE_SIZE;
    s->queue_count--;

    if (!s->queue_count) {
        s->ops.intr(s->ops.parent, 0);
    }
    MEMEXPOSE_DPRINTF("Popped interrupt %lx\n", s->intr_rx.type);
    return 1;
}

static void memexpose_push_intr(MemexposeIntr *s, struct memexpose_op_intr *msg)
{
    int signal = 0, free_slot;

    if (s->queue_count == MEMEXPOSE_INTR_QUEUE_SIZE) {
        MEMEXPOSE_DPRINTF("Interrupt queue is already full!\n");
        return;
    }
    free_slot = (s->queue_start + s->queue_count) % MEMEXPOSE_INTR_QUEUE_SIZE;
    s->intr_queue[free_slot] = *msg;
    if (!s->queue_count) {
        signal = 1;
    }
    s->queue_count++;

    if (signal) {
        s->ops.intr(s->ops.parent, 1);
    }
}

static void process_intr(void *opaque, struct memexpose_op *op, Error **err)
{
    MemexposeIntr *s = opaque;
    switch (op->head.ot) {
    case MOP_INTR:
        memexpose_push_intr(s, &op->body.intr);
        break;
    default:
        error_setg(err, "Unknown memexpose intr command %u", op->head.ot);
    }
}

static void memexpose_send_intr(MemexposeIntr *s)
{
    struct memexpose_op msg;

    msg.head.ot = MOP_INTR;
    msg.head.size = sizeof(msg.head) + sizeof(msg.body.intr);
    msg.head.prio = 0;
    msg.body.intr = s->intr_tx;
    memexpose_ep_write_async(&s->ep, &msg);
    MEMEXPOSE_DPRINTF("Sending interrupt %lx\n", msg.body.intr.type);
}

#define IN_INTR_DATA_RANGE(a, s, r) \
    (a >= r && \
     a < r + MEMEXPOSE_MAX_INTR_DATA_SIZE && \
     (s = MIN(s, r + MEMEXPOSE_MAX_INTR_DATA_SIZE - a), 1))

static uint64_t memexpose_intr_read(void *opaque, hwaddr addr,
                                    unsigned size)
{
    MemexposeIntr *s = opaque;
    uint64_t ret = 0;
    unsigned int boff = 8 * (addr & 0x7);

    switch (addr & (~0x7)) {
    case MEMEXPOSE_INTR_RX_TYPE_ADDR:
        ret = s->intr_rx.type;
        ret >>= boff;
        return ret;
    case MEMEXPOSE_INTR_TX_TYPE_ADDR:
        ret = s->intr_tx.type;
        ret >>= boff;
        return ret;
    case MEMEXPOSE_INTR_RECV_ADDR:
        /* Make multiple read calls in readq and such behave as expected */
        if (addr & 0x7) {
            return 0;
        }

        ret = memexpose_pop_intr(s);
        return ret;
    case MEMEXPOSE_INTR_ENABLE_ADDR:
        if (addr & 0x7) {
            return 0;
        }
        return s->enabled;
    default:
        break;
    }

    if (IN_INTR_DATA_RANGE(addr, size, MEMEXPOSE_INTR_RX_DATA_ADDR)) {
        uint64_t off = addr - MEMEXPOSE_INTR_RX_DATA_ADDR;
        memcpy(&ret, s->intr_rx.data + off, size);
        return ret;
    } else if (IN_INTR_DATA_RANGE(addr, size, MEMEXPOSE_INTR_TX_DATA_ADDR)) {
        uint64_t off = addr - MEMEXPOSE_INTR_TX_DATA_ADDR;
        memcpy(&ret, s->intr_tx.data + off, size);
        return ret;
    } else {
        MEMEXPOSE_DPRINTF("Invalid mmio read at " TARGET_FMT_plx "\n", addr);
        ret = 0;
        return ret;
    }
}

static void memexpose_intr_write(void *opaque, hwaddr addr,
                                 uint64_t val, unsigned size)
{
    MemexposeIntr *s = opaque;
    unsigned int boff = 8 * (addr & 0x7);
    uint64_t mask = ((1LL << (size * 8)) - 1) << boff;

    switch (addr & (~0x7)) {
    case MEMEXPOSE_INTR_RX_TYPE_ADDR:
        s->intr_rx.type &= ~mask;
        s->intr_rx.type |= (val << boff);
        return;
    case MEMEXPOSE_INTR_TX_TYPE_ADDR:
        s->intr_tx.type &= ~mask;
        s->intr_tx.type |= (val << boff);
        return;
    case MEMEXPOSE_INTR_SEND_ADDR:
        /* Make multiple write calls in writeq and such behave as expected */
        if (addr & 0x7) {
            return;
        }
        memexpose_send_intr(s);
        return;
    case MEMEXPOSE_INTR_ENABLE_ADDR:
        if (addr & 0x7) {
            return;
        }
        if (val) {
            if (s->ops.enable) {
                s->enabled = s->ops.enable(s->ops.parent) ? 0 : 1;
            } else {
                s->enabled = 1;
            }
        } else {
            if (s->ops.disable) {
                s->ops.disable(s->ops.parent);
            }
            s->enabled = 0;
        }
        return;
    }

    if (IN_INTR_DATA_RANGE(addr, size, MEMEXPOSE_INTR_RX_DATA_ADDR)) {
        uint64_t off = addr - MEMEXPOSE_INTR_RX_DATA_ADDR;
        memcpy(s->intr_rx.data + off, &val, size);
    } else if (IN_INTR_DATA_RANGE(addr, size, MEMEXPOSE_INTR_TX_DATA_ADDR)) {
        uint64_t off = addr - MEMEXPOSE_INTR_TX_DATA_ADDR;
        memcpy(s->intr_tx.data + off, &val, size);
    } else {
        MEMEXPOSE_DPRINTF("Invalid mmio write at " TARGET_FMT_plx "\n", addr);
    }
}

static const MemoryRegionOps memexpose_intr_ops = {
    .read = memexpose_intr_read,
    .write = memexpose_intr_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

void memexpose_intr_init(MemexposeIntr *s, struct memexpose_intr_ops *ops,
                         Object *parent, CharBackend *chr, Error **errp)
{
    if (!qemu_chr_fe_backend_connected(chr)) {
        error_setg(errp, "You must specify a 'intr_chardev'");
        return;
    }

    s->parent = parent;
    s->ops = *ops;
    s->enabled = 0;
    s->queue_start = 0;
    s->queue_count = 0;
    memexpose_ep_init(&s->ep, chr, s, 0, process_intr);
    s->ep.is_async = true;
    memory_region_init_io(&s->shmem, parent, &memexpose_intr_ops, s,
                          "memexpose-intr", MEMEXPOSE_INTR_MEM_SIZE);
}

int memexpose_intr_enable(MemexposeIntr *s)
{
    return memexpose_ep_connect(&s->ep);
}

void memexpose_intr_disable(MemexposeIntr *s)
{
    memexpose_ep_disconnect(&s->ep);
}

void memexpose_intr_destroy(MemexposeIntr *s)
{
    memexpose_intr_disable(s);
    /* Region will be collected with its parent */
    memexpose_ep_destroy(&s->ep);
}

static bool memshare_region_overlaps(MemexposeMem *s,
                                     struct memexpose_memshare_info_fd *share)
{
    MemexposeRemoteMemory *mem;
    QLIST_FOREACH(mem, &s->remote_regions, list) {
        uint64_t start = memory_region_get_ram_addr(&mem->region);
        uint64_t size = memory_region_size(&mem->region);
        MEMEXPOSE_DPRINTF("Comparing regions: received %"PRIx64"-%"PRIx64", "\
                          "current mapped %"PRIx64"-%"PRIx64"\n",
                          share->start, share->start + share->size,
                          start, start + size);
        if (start < share->start + share->size ||
            share->start < start + size)
            return true;
    }
    return false;
}

static void memshare_add_region(MemexposeMem *s, int fd,
                                struct memexpose_memshare_info_fd *share,
                                Error **errp)
{
    if (share->start >= s->shmem_size) {
        /* TODO - error out */
        MEMEXPOSE_DPRINTF("Shared memory start too high: "
                          "%" PRIx64 " >= %" PRIx64,
                          share->start, s->shmem_size);
        close(fd);
        return;
    }

    if (memshare_region_overlaps(s, share)) {
        /* TODO - error out */
        MEMEXPOSE_DPRINTF("Shared memory %" PRIx64 "-%" PRIx64
                          " overlaps with existing region",
                          share->start, share->start + share->size);
        close(fd);
        return;
    }

    uint64_t clamped_size = s->shmem_size - share->start;
    share->size = MIN(share->size, clamped_size);

    MemexposeRemoteMemory *mem = g_malloc(sizeof(*mem));
    char *rname = g_strdup_printf("Memexpose shmem "
                                  "%" PRIx64 "-%" PRIx64" -> %" PRIx64,
                                  share->start, share->start + share->size,
                                  share->mmap_start);

    MEMEXPOSE_DPRINTF("Mapping remote memory: %" PRIx64 \
                      "-%" PRIx64 ", fd offset %" PRIx64 "\n",
                      share->start, share->size, share->mmap_start);

    memory_region_init_ram_from_fd(&mem->region, s->parent, rname,
                                   share->size, share->mmap_start,
                                   true, fd, errp);
    if (*errp) {
        error_report_err(*errp);
        close(fd);
        return;
    }

    memory_region_set_nonvolatile(&mem->region, share->nonvolatile);
    memory_region_set_readonly(&mem->region, share->readonly);
    g_free(rname);
    memory_region_add_subregion_overlap(&s->shmem, share->start,
                                        &mem->region, 1);
    QLIST_INSERT_HEAD(&s->remote_regions, mem, list);
}

static void memshare_remove_region(MemexposeMem *s, MemexposeRemoteMemory *reg)
{
    /* TODO is this correct? Docs warn about leaked refcounts */
    QLIST_REMOVE(reg, list);
    memory_region_del_subregion(&s->shmem, &reg->region);
    object_unparent(OBJECT(&reg->region));
}

static void memshare_handle(MemexposeMem *s,
                            struct memexpose_memshare_info *share)
{
    int fd;
    switch (share->type) {
    case MEMSHARE_NONE:
        return;
    case MEMSHARE_FD:
        fd = memexpose_ep_recv_fd(&s->ep);
        MEMEXPOSE_DPRINTF("Received memshare fd: %d\n", fd);
        if (s->pending_invalidation) {
            close(fd);
            return;
        }
        Error *err = NULL;
        memshare_add_region(s, fd, &share->fd, &err); /* TODO - handle errors */
        return;
    default:
        MEMEXPOSE_DPRINTF("Invalid memshare type: %u\n", share->type);
        return;
    }
}

static MemTxResult memexpose_read_slow(void *opaque, hwaddr addr,
                                       uint64_t *data, unsigned size,
                                       MemTxAttrs attrs)
{
    MemexposeMem *s = opaque;

    struct memexpose_op msg;
    msg.head.size = sizeof(msg.head) + sizeof(msg.body.read);
    msg.head.ot = MOP_READ;
    msg.head.prio = memexpose_ep_msg_prio(&s->ep, msg.head.ot);
    msg.body.read.offset = addr;
    msg.body.read.size = size;
    memexpose_ep_write_sync(&s->ep, &msg);

    MemTxResult res = msg.body.read_ret.ret;
    if (res == MEMTX_OK) {
        memshare_handle(s, &msg.body.read_ret.share);
    }
    memcpy(data, &msg.body.read_ret.value, size);
    return res;
}

static MemTxResult memexpose_write_slow(void *opaque, hwaddr addr,
                                        uint64_t val, unsigned size,
                                        MemTxAttrs attrs)
{
    MemexposeMem *s = opaque;
    struct memexpose_op msg;
    msg.head.size = sizeof(msg.head) + sizeof(msg.body.write);
    msg.head.ot = MOP_WRITE;
    msg.head.prio = memexpose_ep_msg_prio(&s->ep, msg.head.ot);
    msg.body.write.offset = addr;
    msg.body.write.size = size;
    msg.body.write.value = val;
    memexpose_ep_write_sync(&s->ep, &msg);

    MemTxResult res = msg.body.write_ret.ret;
    if (res == MEMTX_OK) {
        memshare_handle(s, &msg.body.write_ret.share);
    }
    return res;
}

static const MemoryRegionOps memexpose_region_ops = {
    .read_with_attrs = memexpose_read_slow,
    .write_with_attrs = memexpose_write_slow,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static void prepare_memshare(MemexposeMem *s,
                             uint64_t size, uint64_t offset,
                             struct memexpose_memshare_info *info) {
    MemoryRegionSection section = memory_region_find_flat_range(
            s->as.root, offset, size);
    if (!section.mr) {
        MEMEXPOSE_DPRINTF("No memory region under %lu!\n", offset);
        goto unref;
    }

    int fd = memory_region_get_fd(section.mr);
    if (fd != -1 && qemu_ram_is_shared(section.mr->ram_block)) {
        info->type = MEMSHARE_FD;
        info->fd.mmap_start = section.offset_within_region;
        info->fd.start = section.offset_within_address_space;
        info->fd.size = section.size;
        info->fd.readonly = memory_region_is_rom(section.mr);
        info->fd.nonvolatile = memory_region_is_nonvolatile(section.mr);

        MEMEXPOSE_DPRINTF("Prepared a memshare fd: %" PRIx64 \
                          "-%" PRIx64 ", fd offset %" PRIx64 "\n",
                          info->fd.start, info->fd.size, info->fd.mmap_start);
        memexpose_ep_send_fd(&s->ep, fd);
        s->nothing_shared = false;
    } else {
        info->type = MEMSHARE_NONE;
    }
unref:
    memory_region_unref(section.mr);
}

static void memexpose_perform_read_request(
        MemexposeMem *s, struct memexpose_op_read *in,
        struct memexpose_op *out)
{
    out->head.ot = MOP_READ_RET;
    out->head.size = sizeof(out->head) + sizeof(out->body.read_ret);
    out->body.read_ret.ret = 0;
    out->body.read_ret.share.type = MEMSHARE_NONE;

    MEMEXPOSE_DPRINTF("Reading %u from %lx\n", in->size, in->offset);
    MemTxResult r = address_space_read(&s->as, in->offset,
                                       MEMTXATTRS_UNSPECIFIED,
                                       (uint8_t *) &out->body.read_ret.value,
                                       in->size);
    out->body.read_ret.ret = r;
    if (r != MEMTX_OK) {
        MEMEXPOSE_DPRINTF("Failed to read\n");
    } else {
        prepare_memshare(s, in->size, in->offset, &out->body.read_ret.share);
    }
}

static void memexpose_perform_write_request(
        MemexposeMem *s, struct memexpose_op_write *in,
        struct memexpose_op *out)
{
    out->head.ot = MOP_WRITE_RET;
    out->head.size = sizeof(out->head) + sizeof(out->body.write_ret);
    out->body.write_ret.ret = 0;
    out->body.write_ret.share.type = MEMSHARE_NONE;

    MEMEXPOSE_DPRINTF("Writing %u to %lx\n", in->size, in->offset);
    MemTxResult r = address_space_write(&s->as, in->offset,
                                        MEMTXATTRS_UNSPECIFIED,
                                        (uint8_t *) &in->value,
                                        in->size);
    if (r != MEMTX_OK) {
        out->body.write_ret.ret = -EIO;
        MEMEXPOSE_DPRINTF("Failed to write\n");
        return;
    }

    out->body.write_ret.ret = r;
    if (r != MEMTX_OK) {
        MEMEXPOSE_DPRINTF("Failed to read\n");
    } else {
        prepare_memshare(s, in->size, in->offset, &out->body.write_ret.share);
    }
}

static bool region_is_ours(MemexposeMem *s, MemoryRegion *mr)
{
    if (mr == &s->shmem) {
        return true;
    }

    MemexposeRemoteMemory *mem;
    QLIST_FOREACH(mem, &s->remote_regions, list) {
        if (mr == &mem->region) {
            return true;
        }
    }
    return false;
}

static void memexpose_remote_invalidate(MemoryListener *inv,
                                        MemoryRegionSection *sect)
{
    MemexposeMem *s = container_of(inv, MemexposeMem, remote_invalidator);
    struct memexpose_op msg;
    struct memexpose_op_reg_inv *ri = &msg.body.reg_inv;

    if (!sect->mr || region_is_ours(s, sect->mr)) {
        return;
    }
    if (s->nothing_shared) {
        return;
    }

    msg.head.size = sizeof(msg.head) + sizeof(msg.body.reg_inv);
    msg.head.ot = MOP_REG_INV;
    msg.head.prio = memexpose_ep_msg_prio(&s->ep, msg.head.ot);

    ri->start = sect->offset_within_address_space;
    ri->size = int128_get64(sect->size);
    MEMEXPOSE_DPRINTF("Region %"PRIx64"-%"PRIx64" changed, "
                      "sending invalidation request\n",
                      ri->start, ri->start + ri->size);
    memexpose_ep_write_sync(&s->ep, &msg);
}

static void memexpose_invalidate_region(MemexposeMem *s,
                                        struct memexpose_op_reg_inv *ri,
                                        struct memexpose_op *out)
{
    MemexposeRemoteMemory *mem;

    QLIST_FOREACH(mem, &s->remote_regions, list) {
        uint64_t start = memory_region_get_ram_addr(&mem->region);
        uint64_t size = memory_region_size(&mem->region);
        if (start < ri->start + ri->size ||
            start + size > ri->start) {
            mem->should_invalidate = true;
            s->pending_invalidation = true;
        }
    }

    if (s->pending_invalidation) {
        qemu_bh_schedule(s->reg_inv_bh);
    }

    out->head.ot = MOP_REG_INV_RET;
    out->head.size = sizeof(out->head);
}

static void memexpose_do_reg_inv_bh(void *opaque)
{
    MemexposeMem *s = opaque;

    MemexposeRemoteMemory *mem, *tmp;
    QLIST_FOREACH_SAFE(mem, &s->remote_regions, list, tmp) {
        if (mem->should_invalidate) {
            memshare_remove_region(s, mem);
        }
    }
    s->pending_invalidation = false;
}

static void process_mem(void *opaque, struct memexpose_op *op, Error **err)
{
    MemexposeMem *s = opaque;
    struct memexpose_op resp;
    resp.head.prio = op->head.prio;
    switch (op->head.ot) {
    case MOP_READ:
        memexpose_perform_read_request(s, &op->body.read, &resp);
        break;
    case MOP_WRITE:
        memexpose_perform_write_request(s, &op->body.write, &resp);
        break;
    case MOP_REG_INV:
        memexpose_invalidate_region(s, &op->body.reg_inv, &resp);
        break;
    default:
        error_setg(err, "Unknown memexpose command %u", op->head.ot);
        return;
    }
    memexpose_ep_write_async(&s->ep, &resp);
}

void memexpose_mem_init(MemexposeMem *s, Object *parent,
                        MemoryRegion *as_root,
                        CharBackend *chr, int prio, Error **errp)
{
    if (!qemu_chr_fe_backend_connected(chr)) {
        error_setg(errp, "You must specify a 'mem_chardev'");
        return;
    }

    QLIST_INIT(&s->remote_regions);
    s->parent = parent;
    address_space_init(&s->as, as_root, "Memexpose");

    memexpose_ep_init(&s->ep, chr, s, prio, process_mem);
    s->ep.is_async = false;
    memory_region_init_io(&s->shmem, parent, &memexpose_region_ops, s,
                          "memexpose-shmem", s->shmem_size);
    MEMEXPOSE_DPRINTF("Shmem size %lx\n", memory_region_size(&s->shmem));

    s->nothing_shared = true;
    s->remote_invalidator = (MemoryListener) {
        .region_add = memexpose_remote_invalidate,
            .region_del = memexpose_remote_invalidate,
    };
    s->reg_inv_bh = qemu_bh_new(memexpose_do_reg_inv_bh, s);
    memory_listener_register(&s->remote_invalidator, &s->as);
}

int memexpose_mem_enable(MemexposeMem *s)
{
    return memexpose_ep_connect(&s->ep);
}

void memexpose_mem_disable(MemexposeMem *s)
{
    memexpose_ep_disconnect(&s->ep);

    MemexposeRemoteMemory *mem, *tmp;
    QLIST_FOREACH_SAFE(mem, &s->remote_regions, list, tmp) {
        memshare_remove_region(s, mem);
    }
    qemu_bh_cancel(s->reg_inv_bh);
    s->pending_invalidation = false;
}

void memexpose_mem_destroy(MemexposeMem *s)
{
    memexpose_mem_disable(s);
    /* Region will be collected with its parent */
    memory_listener_unregister(&s->remote_invalidator);
    memexpose_ep_destroy(&s->ep);
    qemu_bh_delete(s->reg_inv_bh);
    address_space_destroy(&s->as);
}
