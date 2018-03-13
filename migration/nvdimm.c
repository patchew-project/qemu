/*
 * QEMU System Emulator
 *
 * Authors:
 *  He Junyan<Junyan.he@intel.com>
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
#include "hw/mem/nvdimm.h"
#include "cpu.h"
#include "qemu/cutils.h"
#include "exec/ram_addr.h"
#include "exec/target_page.h"
#include "qemu/rcu_queue.h"
#include "qemu/error-report.h"
#include "migration.h"
#include "qapi/error.h"
#include "migration/register.h"
#include "migration/ram.h"
#include "migration/qemu-file.h"
#include "migration.h"
#include "migration/misc.h"
#include "migration/savevm.h"
#include "block/snapshot.h"
#include "migration/snapshot.h"

#define NVDIMM_MIG_VERSION 0x01

/* PADDING data, useless */
#define NVDIMM_PADDING_BYTE 0xce
/* PAGE id, is all zero */
#define NVDIMM_ZERO_PAGE_ID 0xaabc250f
#define NVDIMM_NONZERO_PAGE_ID 0xacbc250e
/* No usage date, for alignment only */
#define NVDIMM_SECTION_PADDING_ID 0xaaceccea
/* Section for dirty log kind */
#define NVDIMM_SECTION_DIRTY_LOG_ID 0xbbcd0c1e
/* Section for raw data, no bitmap, dump the whole mem */
#define NVDIMM_SECTION_DATA_ID 0x76bbcae3
/* Section for setup */
#define NVDIMM_SECTION_SETUP 0x7ace0cfa
/* Section for setup */
#define NVDIMM_SECTION_COMPLETE 0x8ace0cfa
/* Section end symbol */
#define NVDIMM_SECTION_END_ID 0xccbe8752
/************************  Sections** ***********************
Padding section
----------------------------------------------------
| PADDING_ID | size | PADDING_BYTE ...... | END_ID |
----------------------------------------------------
Dirty log section
------------------------------------------------------------------------------------
| DIRTY_BITMAP_ID | total size | ram name size | ram name | ram size | bitmap size |
------------------------------------------------------------------------------------
    -----------------------------------------------------------------
     bitmap data... | dirty page size | dirty page data... | END_ID |
    -----------------------------------------------------------------
Raw data section
---------------------------------------------------------------------------------------
| DATA_ID | size | ram name size | ram name | ram size | data size | data... | END_ID |
---------------------------------------------------------------------------------------
*************************************************************/

/* State of NVDimm for migration */
struct NVDimmState {
    /* Whether the block driver support dependency
       between snapshots */
    char *depend_snapshot_id;
    int64_t depend_offset;
    int64_t depend_size;
    char *cur_snapshot_id;
    int64_t cur_offset;
    int64_t cur_size;
    RAMBlock **blocks;
    int block_num;
    bool dirty_logging;
};
typedef struct NVDimmState NVDimmState;

static NVDimmState *nvdimm_state_p;

static int nvdimm_device_list_append(Object *obj, void *opaque)
{
    GSList **list = opaque;

    if (object_dynamic_cast(obj, TYPE_NVDIMM)) {
        *list = g_slist_append(*list, DEVICE(obj));
    }

    object_child_foreach(obj, nvdimm_device_list_append, opaque);
    return 0;
}

static bool ram_block_is_nvdimm(RAMBlock *block)
{
    GSList *list = NULL;
    GSList *device_list = NULL;
    bool ret = false;

    object_child_foreach(qdev_get_machine(),
                         nvdimm_device_list_append, &device_list);

    if (device_list == NULL) {
        return false;
    }

    for (list = device_list; list; list = list->next) {
        NVDIMMDevice *nvd = list->data;
        MemoryRegion *mr = &nvd->nvdimm_mr;
        int fd = memory_region_get_fd(mr);

        if (fd >= 0 && fd == block->fd) {
            ret = true;
            break;
        }
    }

    g_slist_free(device_list);
    return ret;
}

bool ram_block_is_nvdimm_active(RAMBlock *block)
{
    if (block == NULL) {
        return false;
    }

    if (get_current_snapshot_info(NULL) == false) {
        return false;
    }

    return ram_block_is_nvdimm(block);
}

/* Just support snapshot, live migration use ram's handlers */
static bool nvdimm_is_active(void *opaque)
{
    RAMBlock *block;
    int ret = get_current_snapshot_info(NULL);
    if (ret) {
        return true;
    }

    if (!ram_bytes_total()) {
        return false;
    }

    rcu_read_lock();
    RAMBLOCK_FOREACH(block)
    {
        if (ram_block_is_nvdimm_active(block)) {
            rcu_read_unlock();
            return true;
        }
    }
    rcu_read_unlock();

    return false;
}

static int nvdimm_padding_to_alignment(QEMUFile *f, int section_id,
                                       int32_t alignment, bool add_footer)
{
    int64_t cur_pos;
    int32_t padding_sz;
    int ret = 0;

    cur_pos = qemu_ftell(f);
    /* We need to insert some padding section here. */
    padding_sz = (int32_t)(QEMU_ALIGN_UP(cur_pos, alignment) - cur_pos);
    ret = padding_sz;

    padding_sz -= sizeof(int32_t); // NVDIMM_SECTION_PADDING_ID
    padding_sz -= sizeof(int32_t); // NVDIMM_PADDING_BYTE size
    padding_sz -= sizeof(int32_t); // NVDIMM_SECTION_END_ID
    if (migrate_get_current()->send_section_footer) {
        padding_sz -= sizeof(int8_t);
        padding_sz -= sizeof(int32_t);
    }

    if (padding_sz <= 0) {
        padding_sz += alignment;
        ret += alignment;
    }

    qemu_put_be32(f, NVDIMM_SECTION_PADDING_ID);
    qemu_put_be32(f, padding_sz);
    while (padding_sz) {
        qemu_put_byte(f, NVDIMM_PADDING_BYTE);
        padding_sz--;
    }
    qemu_put_be32(f, NVDIMM_SECTION_END_ID);
    if (add_footer && migrate_get_current()->send_section_footer) {
        qemu_put_byte(f, QEMU_VM_SECTION_FOOTER);
        qemu_put_be32(f, section_id);
    }

    cur_pos = qemu_ftell(f);
    assert(QEMU_IS_ALIGNED(cur_pos, alignment) || add_footer == false);
    return ret;
}

static int nvdimm_state_save_dependency(QEMUFile *f, NVDimmState *nvdimm_state)
{
    int64_t cur_pos;
    int ret;
    int32_t alignment = 0;

    if (qemu_file_is_support_dependency(f, &alignment) == false) {
        error_report("Enable nvdimm dependent snapshot without"
                     "file dependency support");
        return -ENOTSUP;
    }

    cur_pos = qemu_ftell(f);
    assert(QEMU_IS_ALIGNED(cur_pos, alignment));
    assert(QEMU_IS_ALIGNED(nvdimm_state->depend_offset, alignment));
    assert(QEMU_IS_ALIGNED(nvdimm_state->depend_size, alignment));

    ret = qemu_file_set_ref_name(f, nvdimm_state->depend_snapshot_id);
    assert(ret);

    ret = qemu_file_save_dependency(f, nvdimm_state->depend_offset,
                                    nvdimm_state->depend_size);
    if (ret < 0) {
        error_report("save file dependency failed, depend_offset = %lx "
                     "depend_size is %ld, ret is %d",
                     nvdimm_state->depend_offset,
                     nvdimm_state->depend_size, ret);
        return ret;
    }

    cur_pos = qemu_ftell(f);
    assert(QEMU_IS_ALIGNED(cur_pos, alignment));

    return ret;
}

static inline void *nvdimm_host_from_ram_block_offset(RAMBlock *block,
                                                      ram_addr_t offset)
{
    if (!offset_in_ramblock(block, offset)) {
        return NULL;
    }

    return block->host + offset;
}

static int nvdimm_state_save_all_pages(QEMUFile *f,
                                       NVDimmState *nvdimm_state, int i)
{
    hwaddr addr;
    uint64_t total_sz;
    int name_sz;
    uint64_t data_sz;
    void *host_ptr;

    if (memory_region_size(nvdimm_state->blocks[i]->mr) == 0) {
        return 0;
    }

    data_sz = 0;
    for (addr = 0; addr < memory_region_size(nvdimm_state->blocks[i]->mr);
         addr += 1 << TARGET_PAGE_BITS) {
        assert(QEMU_IS_ALIGNED(addr, 1 << TARGET_PAGE_BITS));
        host_ptr =
            nvdimm_host_from_ram_block_offset(nvdimm_state->blocks[i], addr);
        if (!host_ptr) {
            error_report("Illegal RAM offset " RAM_ADDR_FMT, addr);
            return -EINVAL;
        }

        if (buffer_is_zero(host_ptr, 1 << TARGET_PAGE_BITS)) {
            data_sz += sizeof(int); // Zero page, just a ID
        } else {
            data_sz += ((1 << TARGET_PAGE_BITS) + sizeof(int)); // ID + page
        }
    }

    total_sz = sizeof(unsigned int); // NVDIMM_SECTION_DIRTY_BITMAP_ID
    total_sz += sizeof(uint64_t);    // the total size itself
    total_sz += sizeof(int);         // ram name size
    name_sz = strlen(nvdimm_state->blocks[i]->idstr) + 1;
    total_sz += name_sz;
    total_sz += sizeof(uint64_t); // ram size
    total_sz += sizeof(uint64_t); // data size
    total_sz += data_sz;
    total_sz += sizeof(unsigned int); // NVDIMM_SECTION_END_ID

    qemu_put_be32(f, NVDIMM_SECTION_DATA_ID);
    qemu_put_be64(f, total_sz);
    qemu_put_be32(f, name_sz);
    qemu_put_buffer(f, (uint8_t *)nvdimm_state->blocks[i]->idstr, name_sz);
    qemu_put_be64(f, memory_region_size(nvdimm_state->blocks[i]->mr));
    qemu_put_be64(f, data_sz);

    for (addr = 0; addr < memory_region_size(nvdimm_state->blocks[i]->mr);
         addr += 1 << TARGET_PAGE_BITS) {
        host_ptr =
            nvdimm_host_from_ram_block_offset(nvdimm_state->blocks[i], addr);

        if (buffer_is_zero(host_ptr, 1 << TARGET_PAGE_BITS)) {
            qemu_put_be32(f, NVDIMM_ZERO_PAGE_ID);
            data_sz -= sizeof(int);
        } else {
            qemu_put_be32(f, NVDIMM_NONZERO_PAGE_ID);
            data_sz -= sizeof(int);
            qemu_put_buffer(f, host_ptr, 1 << TARGET_PAGE_BITS);
            data_sz -= 1 << TARGET_PAGE_BITS;
        }
    }
    assert(data_sz == 0);
    qemu_put_be32(f, NVDIMM_SECTION_END_ID);

    return 1;
}

static int nvdimm_state_save_dirty_pages(QEMUFile *f,
                                         NVDimmState *nvdimm_state, int i)
{
    DirtyBitmapSnapshot *snap;
    uint64_t bit_sz;
    uint64_t total_sz;
    int name_sz;
    uint64_t data_sz;
    hwaddr addr;

    if (memory_region_size(nvdimm_state->blocks[i]->mr) == 0) {
        return 0;
    }

    snap = memory_region_snapshot_and_clear_dirty(
        nvdimm_state->blocks[i]->mr,
        0,
        memory_region_size(nvdimm_state->blocks[i]->mr),
        DIRTY_MEMORY_MIGRATION);
    if (snap == NULL) {
        error_report("Can not create snapshot bitmap for block %s",
                     nvdimm_state->blocks[i]->idstr);
        return -1;
    }

    bit_sz =
        memory_region_size(nvdimm_state->blocks[i]->mr) >> (TARGET_PAGE_BITS + 3);

    data_sz = 0;
    for (addr = 0; addr < memory_region_size(nvdimm_state->blocks[i]->mr);
         addr += 1 << TARGET_PAGE_BITS) {
        assert(QEMU_IS_ALIGNED(addr, 1 << TARGET_PAGE_BITS));
        if (memory_region_snapshot_get_dirty(nvdimm_state->blocks[i]->mr,
                                             snap, addr, 1 << TARGET_PAGE_BITS)) {
            data_sz += 1 << TARGET_PAGE_BITS;
        }
    }

    total_sz = sizeof(unsigned int); // NVDIMM_SECTION_DIRTY_BITMAP_ID
    total_sz += sizeof(uint64_t);    // the total size itself
    total_sz += sizeof(int);         // ram name size
    name_sz = strlen(nvdimm_state->blocks[i]->idstr) + 1;
    total_sz += name_sz;
    total_sz += sizeof(uint64_t); // ram size
    total_sz += sizeof(uint64_t); // bitmap size
    total_sz += bit_sz;
    total_sz += sizeof(uint64_t); // data size
    total_sz += data_sz;
    total_sz += sizeof(unsigned int); // NVDIMM_SECTION_END_ID

    qemu_put_be32(f, NVDIMM_SECTION_DIRTY_LOG_ID);
    qemu_put_be64(f, total_sz);
    qemu_put_be32(f, name_sz);
    qemu_put_buffer(f, (uint8_t *)nvdimm_state->blocks[i]->idstr, name_sz);
    qemu_put_be64(f, memory_region_size(nvdimm_state->blocks[i]->mr));
    qemu_put_be64(f, bit_sz);
    qemu_put_buffer(f, (uint8_t *)memory_region_snapshot_get_dirty_bitmap(snap),
                    bit_sz);

    qemu_put_be64(f, data_sz);
    if (data_sz != 0) {
        for (addr = 0; addr < memory_region_size(nvdimm_state->blocks[i]->mr);
             addr += 1 << TARGET_PAGE_BITS) {
            assert(QEMU_IS_ALIGNED(addr, 1 << TARGET_PAGE_BITS));
            if (memory_region_snapshot_get_dirty(nvdimm_state->blocks[i]->mr,
                                                 snap, addr, 1 << TARGET_PAGE_BITS)) {
                qemu_put_buffer(f, nvdimm_state->blocks[i]->host + addr,
                                1 << TARGET_PAGE_BITS);
                data_sz -= 1 << TARGET_PAGE_BITS;
            }
        }
        assert(data_sz == 0);
    }

    qemu_put_be32(f, NVDIMM_SECTION_END_ID);
    g_free(snap);

    return 1;
}

/**
 * nvdimm_save_iterate: iterative stage for migration
 *
 * Returns zero to indicate success and negative for error
 *
 * @f: QEMUFile where to send the data
 * @opaque: NVDimmState pointer
 */
static int nvdimm_save_iterate(QEMUFile *f, void *opaque, int section_id)
{
    NVDimmState *nvdimm_state = *(void **)opaque;
    int ret = 0;
    int i;
    int32_t alignment;
    int64_t begin_pos, cur_pos;
    bool padded = false;

    /* Must support dependency */
    ret = qemu_file_is_support_dependency(f, &alignment);
    assert(ret == true);

    cur_pos = qemu_ftell(f);
    if (!QEMU_IS_ALIGNED(cur_pos, alignment)) {
        ret = nvdimm_padding_to_alignment(f, section_id, alignment, true);
        if (ret < 0) {
            error_report("NVDIMM saving, failed to padding to aligment");
            return ret;
        }
        padded = true;
    }

    begin_pos = qemu_ftell(f);
    assert(QEMU_IS_ALIGNED(begin_pos, alignment));
    nvdimm_state->cur_offset = begin_pos;

    if (nvdimm_state->dirty_logging) {
        ret = nvdimm_state_save_dependency(f, nvdimm_state);
        if (ret < 0) {
            error_report("NVDIMM saving, failed to save dependency");
            return ret;
        }

        for (i = 0; i < nvdimm_state->block_num; i++) {
            cpu_physical_memory_test_and_clear_dirty(
                memory_region_get_ram_addr(nvdimm_state->blocks[i]->mr),
                memory_region_size(nvdimm_state->blocks[i]->mr),
                DIRTY_MEMORY_MIGRATION);
        }

        if (padded) {
            qemu_put_byte(f, QEMU_VM_SECTION_PART);
            qemu_put_be32(f, section_id);
        }

        for (i = 0; i < nvdimm_state->block_num; i++) {
            ret = nvdimm_state_save_dirty_pages(f, nvdimm_state, i);
            if (ret < 0) {
                error_report("NVDIMM saving, failed to save dirty pages");
                return ret;
            }
        }
    } else {
        if (padded) {
            qemu_put_byte(f, QEMU_VM_SECTION_PART);
            qemu_put_be32(f, section_id);
        }

        /* Save the whole content of nvdimm, no dependency needed */
        for (i = 0; i < nvdimm_state->block_num; i++) {
            ret = nvdimm_state_save_all_pages(f, nvdimm_state, i);
            if (ret < 0) {
                error_report("NVDIMM saving, failed to save all pages");
                return ret;
            }
        }
    }

    /* Need to add padding to make the whole data aligned, include
       QEMU_VM_SECTION_FOOTER and section_id */
    cur_pos = qemu_ftell(f);
    if (migrate_get_current()->send_section_footer) {
        cur_pos += (1 + sizeof(int));
    }

    if (QEMU_IS_ALIGNED(cur_pos, alignment)) { // Already aligned
        nvdimm_state->cur_size = cur_pos - begin_pos;
        assert(QEMU_IS_ALIGNED(nvdimm_state->cur_size, alignment));
        return ret;
    }

    /* Appending the footer if needed */
    if (migrate_get_current()->send_section_footer) {
        qemu_put_byte(f, QEMU_VM_SECTION_FOOTER);
        qemu_put_be32(f, section_id);
        qemu_put_byte(f, QEMU_VM_SECTION_PART);
        qemu_put_be32(f, section_id);
    }
    ret = nvdimm_padding_to_alignment(f, section_id, alignment, false);
    if (ret < 0) {
        error_report("NVDIMM saving, failed to save all pages");
        return ret;
    }

    cur_pos = qemu_ftell(f);
    nvdimm_state->cur_size = cur_pos - begin_pos;
    if (migrate_get_current()->send_section_footer) {
        nvdimm_state->cur_size += (1 + sizeof(int));
    }
    assert(QEMU_IS_ALIGNED(nvdimm_state->cur_size, alignment));

    return ret;
}

static void nvdimm_destroy_nvdimm_state(NVDimmState *nvdimm_state)
{
    if (nvdimm_state) {
        /* disable all dirty log trace */
        if (nvdimm_state->depend_snapshot_id) {
            int i;
            for (i = 0; i < nvdimm_state->block_num; i++) {
                memory_region_set_log(nvdimm_state->blocks[i]->mr, false,
                                      DIRTY_MEMORY_MIGRATION);
            }
        }

        if (nvdimm_state->cur_snapshot_id) {
            g_free(nvdimm_state->cur_snapshot_id);
        }
        if (nvdimm_state->blocks) {
            g_free(nvdimm_state->blocks);
        }
        g_free(nvdimm_state);
    }
}

static NVDimmState *nvdimm_alloc_nvdimm_state(void)
{
    NVDimmState *nvdimm_state = g_try_new0(NVDimmState, 1);
    RAMBlock *block;

    if (nvdimm_state == NULL) {
        return NULL;
    }

    rcu_read_lock();
    RAMBLOCK_FOREACH(block)
    {
        if (ram_block_is_nvdimm(block)) {
            nvdimm_state->block_num++;
            nvdimm_state->blocks =
                g_try_renew(RAMBlock *,
                            nvdimm_state->blocks,
                            nvdimm_state->block_num);
            if (nvdimm_state->blocks == NULL) {
                rcu_read_unlock();
                nvdimm_destroy_nvdimm_state(nvdimm_state);
                return NULL;
            }

            nvdimm_state->blocks[nvdimm_state->block_num - 1] = block;
        }
    }
    rcu_read_unlock();

    return nvdimm_state;
}

/**
 * nvdimm_save_setup: Setup nvdimm for migration
 *
 * Returns zero to indicate success and negative for error
 *
 * @f: QEMUFile where to send the data
 * @opaque: NVDimmState pointer
 */
static int nvdimm_save_setup(QEMUFile *f, void *opaque)
{
    NVDimmState *nvdimm_state = *(void **)opaque;
    int ret = 0;
    QEMUSnapshotInfo sn;
    RAMBlock *block;
    int i;

    ret = get_current_snapshot_info(&sn);
    if (ret == 0) { /* Just enable in snapshot mode */
        info_report("Not in snapshot saving, no nvdimm snapshot optimization");
        return -1;
    }

    /* No dependency support, just let the ram common logic do its job */
    if (qemu_file_is_support_dependency(f, NULL) == false) {
        assert(nvdimm_state == NULL);
        info_report("The drive file does not support dependent snapshot");
        return -1;
    }

    if (nvdimm_state == NULL) { /* First time */
        nvdimm_state = nvdimm_alloc_nvdimm_state();
        if (nvdimm_state == NULL) {
            error_report("Alloc the nvdimm state for snapshot saving failed");
            goto failed;
        }

        nvdimm_state->cur_snapshot_id = g_strdup(sn.name);
        *(void **)opaque = nvdimm_state;
    } else {
        assert(nvdimm_state->cur_snapshot_id);
        if (nvdimm_state->depend_snapshot_id) {
            g_free(nvdimm_state->depend_snapshot_id);
        }
        nvdimm_state->depend_snapshot_id = nvdimm_state->cur_snapshot_id;
        nvdimm_state->depend_offset = nvdimm_state->cur_offset;
        nvdimm_state->depend_size = nvdimm_state->cur_size;
        nvdimm_state->cur_snapshot_id = g_strdup(sn.name);
        nvdimm_state->cur_offset = 0;
        nvdimm_state->cur_size = 0;

        rcu_read_lock();
        RAMBLOCK_FOREACH(block)
        {
            if (ram_block_is_nvdimm_active(block)) {
                for (i = 0; i < nvdimm_state->block_num; i++) {
                    if (block == nvdimm_state->blocks[i]) {
                        break;
                    }
                }

                // Can not find the same block?
                if (i == nvdimm_state->block_num) {
                    rcu_read_unlock();
                    error_report("Can not find the block %s", block->idstr);
                    goto failed;
                }
            }
        }
        rcu_read_unlock();
    }

    qemu_put_be32(f, NVDIMM_SECTION_SETUP);
    qemu_put_be32(f, NVDIMM_SECTION_END_ID);

    return ret;

failed:
    nvdimm_destroy_nvdimm_state(nvdimm_state);
    *(void **)opaque = NULL;
    return -1;
}

/**
 * nvdimm_save_complete: function called to send the remaining amount of ram
 *
 * Returns zero to indicate success
 *
 * Called with iothread lock
 *
 * @f: QEMUFile where to send the data
 * @opaque: NVDimmState pointer
 */
static int nvdimm_save_complete(QEMUFile *f, void *opaque)
{
    NVDimmState *nvdimm_state = *(void **)opaque;
    int i;

    for (i = 0; i < nvdimm_state->block_num; i++) {
        memory_region_set_log(nvdimm_state->blocks[i]->mr, true,
                              DIRTY_MEMORY_MIGRATION);
    }

    /* Enable the dirty logging for next time usage */
    nvdimm_state->dirty_logging = true;

    qemu_put_be32(f, NVDIMM_SECTION_COMPLETE);
    qemu_put_be32(f, NVDIMM_SECTION_END_ID);

    return 0;
}

static bool nvdimm_has_postcopy(void *opaque)
{
    return false;
}

static void nvdimm_save_pending(QEMUFile *f, void *opaque, uint64_t max_size,
                                uint64_t *non_postcopiable_pending,
                                uint64_t *postcopiable_pending)
{
}

static void nvdimm_save_cleanup(void *opaque)
{
    return;
}

/**
 * nvdimm_load_setup: Setup NVDimm for migration incoming side
 *
 * Returns zero to indicate success and negative for error
 *
 * @f: QEMUFile where to receive the data
 * @opaque: NVDimmState pointer
 */
static int nvdimm_load_setup(QEMUFile *f, void *opaque)
{
    NVDimmState *nvdimm_state = *(void **)opaque;

    if (nvdimm_state) {
        nvdimm_destroy_nvdimm_state(nvdimm_state);
        *(void **)opaque = NULL;
    }

    nvdimm_state = nvdimm_alloc_nvdimm_state();
    if (nvdimm_state == NULL) {
        return -1;
    }

    *(void **)opaque = nvdimm_state;
    return 1;
}

static int nvdimm_load_cleanup(void *opaque)
{
    NVDimmState *nvdimm_state = *(void **)opaque;
    nvdimm_destroy_nvdimm_state(nvdimm_state);
    *(void **)opaque = NULL;

    return 0;
}

static int nvdimm_load_dirty_pages(QEMUFile *f, NVDimmState *nvdimm_state)
{
    int64_t total_sz = qemu_get_be64(f);
    int ret = 0;
    int name_sz;
    int64_t sz;
    uint8_t *name_buf = NULL;
    uint8_t *bitmap_buf = NULL;
    RAMBlock *block;
    int64_t ram_sz = 0;
    int64_t bitmap_sz = 0;
    int64_t data_sz = 0;
    hwaddr addr;
    void *host_ptr;

    if (total_sz <= 0) {
        ret = -EINVAL;
        return ret;
    }

    name_sz = qemu_get_be32(f);
    if (name_sz <= 0) {
        ret = -EINVAL;
        return ret;
    }

    name_buf = g_malloc(name_sz);
    if (name_buf == NULL) {
        ret = -ENOMEM;
        return ret;
    }

    sz = qemu_get_buffer(f, name_buf, name_sz);
    if (sz != name_sz) {
        ret = -EINVAL;
        return ret;
    }

    block = qemu_ram_block_by_name((char *)name_buf);
    if (block == NULL || block->host == NULL) {
        ret = -EINVAL;
        return ret;
    }
    g_free(name_buf);
    name_buf = NULL;

    ram_sz = qemu_get_be64(f);
    if (ram_sz != memory_region_size(block->mr)) {
        ret = -EINVAL;
        return ret;
    }

    bitmap_sz = qemu_get_be64(f);
    if (bitmap_sz <= 0) {
        ret = -EINVAL;
        return ret;
    }

    bitmap_buf = g_malloc(bitmap_sz);
    if (bitmap_buf == NULL) {
        ret = -ENOMEM;
        return ret;
    }

    sz = qemu_get_buffer(f, bitmap_buf, bitmap_sz);
    if (sz != bitmap_sz) {
        ret = -EINVAL;
        goto out;
    }

    data_sz = qemu_get_be64(f);
    if (data_sz < 0) {
        ret = -EINVAL;
        goto out;
    }

    assert(QEMU_IS_ALIGNED(data_sz, TARGET_PAGE_SIZE));
    addr = 0;
    while (data_sz) {
        addr = find_next_bit((unsigned long *)bitmap_buf,
                             ram_sz >> TARGET_PAGE_BITS, addr);
        host_ptr = nvdimm_host_from_ram_block_offset(block, addr);
        if (!host_ptr) {
            error_report("Illegal RAM offset " RAM_ADDR_FMT, addr);
            ret = -EINVAL;
            goto out;
        }

        qemu_get_buffer(f, host_ptr, TARGET_PAGE_SIZE);
        data_sz -= TARGET_PAGE_SIZE;
    }

    if (qemu_get_be32(f) != NVDIMM_SECTION_END_ID) {
        ret = -EINVAL;
        goto out;
    }

out:
    if (bitmap_buf) {
        g_free(bitmap_buf);
    }
    return ret;
}

static int nvdimm_load_all_pages(QEMUFile *f, NVDimmState *nvdimm_state)
{
    int64_t total_sz = qemu_get_be64(f);
    int64_t sz;
    int64_t data_sz;
    int name_sz;
    int ret = 0;
    uint8_t *buf = NULL;
    RAMBlock *block;
    hwaddr addr;
    void *host_ptr;
    int64_t ram_sz = 0;
    int tag;

    if (total_sz <= 0) {
        ret = -EINVAL;
        return ret;
    }

    name_sz = qemu_get_be32(f);
    if (name_sz <= 0) {
        ret = -EINVAL;
        return ret;
    }

    buf = g_malloc(name_sz);
    if (buf == NULL) {
        ret = -ENOMEM;
        return ret;
    }

    sz = qemu_get_buffer(f, buf, name_sz);
    if (sz != name_sz) {
        ret = -EINVAL;
        goto out;
    }

    block = qemu_ram_block_by_name((char *)buf);
    if (block == NULL || block->host == NULL) {
        ret = -EINVAL;
        goto out;
    }

    ram_sz = qemu_get_be64(f);
    if (ram_sz != memory_region_size(block->mr)) {
        ret = -EINVAL;
        goto out;
    }

    data_sz = qemu_get_be64(f);
    if (data_sz <= 0) {
        ret = -EINVAL;
        goto out;
    }

    for (addr = 0; addr < ram_sz; addr += 1 << TARGET_PAGE_BITS) {
        assert(QEMU_IS_ALIGNED(addr, 1 << TARGET_PAGE_BITS));
        host_ptr = nvdimm_host_from_ram_block_offset(block, addr);
        if (!host_ptr) {
            error_report("Illegal RAM offset " RAM_ADDR_FMT, addr);
            ret = -EINVAL;
            goto out;
        }

        tag = qemu_get_be32(f);
        if (tag == NVDIMM_ZERO_PAGE_ID) {
            memset(host_ptr, 0, 1 << TARGET_PAGE_BITS);
        } else {
            assert(tag == NVDIMM_NONZERO_PAGE_ID);
            qemu_get_buffer(f, host_ptr, TARGET_PAGE_SIZE);
        }
        host_ptr += TARGET_PAGE_SIZE;
    }

    tag = qemu_get_be32(f);
    if (tag != NVDIMM_SECTION_END_ID) {
        ret = -EINVAL;
        goto out;
    }

out:
    if (buf) {
        g_free(buf);
    }

    return ret;
}

static int nvdimm_load(QEMUFile *f, void *opaque, int version_id)
{
    int ret = 0;
    unsigned int sec_id;
    uint8_t *buf = NULL;
    size_t sz;
    NVDimmState *nvdimm_state = *(void **)opaque;

    if (version_id != NVDIMM_MIG_VERSION) {
        ret = -EINVAL;
        goto failed;
    }

    sec_id = qemu_get_be32(f);
    if (sec_id == NVDIMM_SECTION_PADDING_ID) {
        /* Just skip all this padding section. */
        int padding_sz = qemu_get_be32(f);
        unsigned int end_id;
        buf = g_malloc(padding_sz);
        if (buf == NULL) {
            ret = -ENOMEM;
            goto failed;
        }

        sz = qemu_get_buffer(f, buf, padding_sz);
        if (sz != padding_sz) {
            ret = -EINVAL;
            goto failed;
        }
        padding_sz--;
        while (padding_sz >= 0) {
            if (buf[padding_sz] != NVDIMM_PADDING_BYTE) {
                ret = -EINVAL;
                goto failed;
            }
            padding_sz--;
        }

        end_id = qemu_get_be32(f);
        if (end_id != NVDIMM_SECTION_END_ID) {
            ret = -EINVAL;
            goto failed;
        }

        g_free(buf);
        buf = NULL;
    } else if (sec_id == NVDIMM_SECTION_DIRTY_LOG_ID) {
        ret = nvdimm_load_dirty_pages(f, nvdimm_state);
        if (ret < 0) {
            goto failed;
        }
    } else if (sec_id == NVDIMM_SECTION_DATA_ID) {
        ret = nvdimm_load_all_pages(f, nvdimm_state);
        if (ret < 0) {
            goto failed;
        }
    } else if (sec_id == NVDIMM_SECTION_SETUP ||
               sec_id == NVDIMM_SECTION_COMPLETE) {
        unsigned int d = qemu_get_be32(f);
        if (d != NVDIMM_SECTION_END_ID) {
            ret = -EINVAL;
            goto failed;
        }
    } else {
        error_report("NVDIMM load, can not recognize SEC id %d", sec_id);
        ret = -EINVAL;
        goto failed;
    }

    return ret;

failed:
    if (buf) {
        g_free(buf);
    }

    return ret;
}

static SaveVMHandlers savevm_nvdimm_handlers = {
    .is_active = nvdimm_is_active,
    .save_setup = nvdimm_save_setup,
    .save_live_iterate = nvdimm_save_iterate,
    .save_live_complete_precopy = nvdimm_save_complete,
    .has_postcopy = nvdimm_has_postcopy,
    .save_live_pending = nvdimm_save_pending,
    .load_state = nvdimm_load,
    .save_cleanup = nvdimm_save_cleanup,
    .load_setup = nvdimm_load_setup,
    .load_cleanup = nvdimm_load_cleanup,
};

void nvdimm_snapshot_init(void)
{
    register_savevm_live(NULL, "nvdimm", 0, NVDIMM_MIG_VERSION,
                         &savevm_nvdimm_handlers, &nvdimm_state_p);
}
