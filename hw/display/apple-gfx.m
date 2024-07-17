/*
 * QEMU Apple ParavirtualizedGraphics.framework device
 *
 * Copyright © 2023 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * ParavirtualizedGraphics.framework is a set of libraries that macOS provides
 * which implements 3d graphics passthrough to the host as well as a
 * proprietary guest communication channel to drive it. This device model
 * implements support to drive that library from within QEMU.
 */

#include "apple-gfx.h"
#include "trace.h"
#include "qemu-main.h"
#include "qemu/main-loop.h"
#include "qemu/cutils.h"
#include "qapi/visitor.h"
#include "qapi/error.h"
#include "ui/console.h"
#include "monitor/monitor.h"
#include "qapi/error.h"
#include "migration/blocker.h"
#include <mach/mach_vm.h>
#import <ParavirtualizedGraphics/ParavirtualizedGraphics.h>

static const AppleGFXDisplayMode apple_gfx_default_modes[] = {
    { 1920, 1080, 60 },
    { 1440, 1080, 60 },
    { 1280, 1024, 60 },
};

typedef struct PGTask_s { // Name matches forward declaration in PG header
    QTAILQ_ENTRY(PGTask_s) node;
    mach_vm_address_t address;
    uint64_t len;
} AppleGFXTask;

static Error *apple_gfx_mig_blocker;

static void apple_gfx_render_frame_completed(AppleGFXState *s, void *vram,
                                             id<MTLTexture> texture);

static AppleGFXTask *apple_gfx_new_task(AppleGFXState *s, uint64_t len)
{
    mach_vm_address_t task_mem;
    AppleGFXTask *task;
    kern_return_t r;

    r = mach_vm_allocate(mach_task_self(), &task_mem, len, VM_FLAGS_ANYWHERE);
    if (r != KERN_SUCCESS || task_mem == 0) {
        return NULL;
    }

    task = g_new0(AppleGFXTask, 1);

    task->address = task_mem;
    task->len = len;
    QTAILQ_INSERT_TAIL(&s->tasks, task, node);

    return task;
}

static uint64_t apple_gfx_read(void *opaque, hwaddr offset, unsigned size)
{
    AppleGFXState *s = opaque;
    uint64_t res = 0;

    res = [s->pgdev mmioReadAtOffset:offset];

    trace_apple_gfx_read(offset, res);

    return res;
}

static void apple_gfx_write(void *opaque, hwaddr offset, uint64_t val,
                            unsigned size)
{
    AppleGFXState *s = opaque;

    trace_apple_gfx_write(offset, val);

#ifdef __x86_64__
    /* If we use this code on aarch64, the guest fails to bring up the
     * device... */
    id<PGDevice> dev = s->pgdev;
    dispatch_queue_t bg_queue = NULL;

    bg_queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0ul);
    [dev retain];
    dispatch_async(bg_queue, ^{
        [dev mmioWriteAtOffset:offset value:val];
        [dev release];
    });
#else
    /* ... and if we use the following on x86-64, graphics output eventually
     * hangs with warnings about reentrant MMIO. */
    bql_unlock();
    [s->pgdev mmioWriteAtOffset:offset value:val];
    bql_lock();
#endif
}

static const MemoryRegionOps apple_gfx_ops = {
    .read = apple_gfx_read,
    .write = apple_gfx_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void apple_gfx_render_new_frame(AppleGFXState *s)
{
    BOOL r;
    void *vram = s->vram;
    uint32_t width = surface_width(s->surface);
    uint32_t height = surface_height(s->surface);
    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    id<MTLCommandBuffer> command_buffer = [s->mtl_queue commandBuffer];
    id<MTLTexture> texture = s->texture;
    r = [s->pgdisp encodeCurrentFrameToCommandBuffer:command_buffer
                                             texture:texture
                                              region:region];
    if (!r) {
        return;
    }
    [texture retain];
    if (s->using_managed_texture_storage) {
        /* "Managed" textures exist in both VRAM and RAM and must be synced. */
        id<MTLBlitCommandEncoder> blit = [command_buffer blitCommandEncoder];
        [blit synchronizeResource:texture];
        [blit endEncoding];
    }
    [command_buffer retain];
    [command_buffer addCompletedHandler:
        ^(id<MTLCommandBuffer> cb)
        {
            dispatch_async(s->render_queue, ^{
                apple_gfx_render_frame_completed(s, vram, texture);
                [texture release];
            });
            [command_buffer release];
        }];
    [command_buffer commit];
}

static void copy_mtl_texture_to_surface_mem(id<MTLTexture> texture, void *vram)
{
    /* TODO: Skip this entirely on a pure Metal or headless/guest-only
     * rendering path, else use a blit command encoder? Needs careful
     * (double?) buffering design. */
    size_t width = texture.width, height = texture.height;
    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    [texture getBytes:vram
          bytesPerRow:(width * 4)
        bytesPerImage:(width * height * 4)
           fromRegion:region
          mipmapLevel:0
                slice:0];
}

static void apple_gfx_render_frame_completed(AppleGFXState *s, void *vram,
                                             id<MTLTexture> texture)
{
    --s->pending_frames;
    assert(s->pending_frames >= 0);

    if (vram != s->vram) {
        /* Display mode has changed, drop this old frame. */
        assert(texture != s->texture);
        g_free(vram);
    } else {
        copy_mtl_texture_to_surface_mem(texture, vram);
        if (s->gfx_update_requested) {
            s->gfx_update_requested = false;
            dpy_gfx_update_full(s->con);
            graphic_hw_update_done(s->con);
            s->new_frame_ready = false;
        } else {
            s->new_frame_ready = true;
        }
    }
    if (s->pending_frames > 0) {
        apple_gfx_render_new_frame(s);
    }
}

static void apple_gfx_fb_update_display(void *opaque)
{
    AppleGFXState *s = opaque;

    dispatch_async(s->render_queue, ^{
        if (s->pending_frames > 0) {
            s->gfx_update_requested = true;
        } else {
            if (s->new_frame_ready) {
                dpy_gfx_update_full(s->con);
                s->new_frame_ready = false;
            }
            graphic_hw_update_done(s->con);
        }
    });
}

static const GraphicHwOps apple_gfx_fb_ops = {
    .gfx_update = apple_gfx_fb_update_display,
    .gfx_update_async = true,
};

static void update_cursor(AppleGFXState *s)
{
    dpy_mouse_set(s->con, s->pgdisp.cursorPosition.x,
                  s->pgdisp.cursorPosition.y, s->cursor_show);
}

static void set_mode(AppleGFXState *s, uint32_t width, uint32_t height)
{
    void *vram = NULL;
    DisplaySurface *surface;
    MTLTextureDescriptor *textureDescriptor;
    id<MTLTexture> texture = nil;
    __block bool no_change = false;

    dispatch_sync(s->render_queue,
        ^{
            if (s->surface &&
                width == surface_width(s->surface) &&
                height == surface_height(s->surface)) {
                no_change = true;
            }
        });

    if (no_change) {
        return;
    }

    vram = g_malloc0(width * height * 4);
    surface = qemu_create_displaysurface_from(width, height, PIXMAN_LE_a8r8g8b8,
                                              width * 4, vram);

    @autoreleasepool {
        textureDescriptor =
            [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                             width:width
                                            height:height
                                         mipmapped:NO];
        textureDescriptor.usage = s->pgdisp.minimumTextureUsage;
        texture = [s->mtl newTextureWithDescriptor:textureDescriptor];
    }

    s->using_managed_texture_storage =
        (texture.storageMode == MTLStorageModeManaged);

    dispatch_sync(s->render_queue,
        ^{
            id<MTLTexture> old_texture = nil;
            void *old_vram = s->vram;
            s->vram = vram;
            s->surface = surface;

            dpy_gfx_replace_surface(s->con, surface);

            old_texture = s->texture;
            s->texture = texture;
            [old_texture release];

            if (s->pending_frames == 0) {
                g_free(old_vram);
            }
        });
}

static void create_fb(AppleGFXState *s)
{
    s->con = graphic_console_init(NULL, 0, &apple_gfx_fb_ops, s);

    s->cursor_show = true;
}

static size_t apple_gfx_get_default_mmio_range_size(void)
{
    size_t mmio_range_size;
    @autoreleasepool {
        PGDeviceDescriptor *desc = [PGDeviceDescriptor new];
        mmio_range_size = desc.mmioLength;
        [desc release];
    }
    return mmio_range_size;
}

void apple_gfx_common_init(Object *obj, AppleGFXState *s, const char* obj_name)
{
    Error *local_err = NULL;
    int r;
    size_t mmio_range_size = apple_gfx_get_default_mmio_range_size();

    trace_apple_gfx_common_init(obj_name, mmio_range_size);
    memory_region_init_io(&s->iomem_gfx, obj, &apple_gfx_ops, s, obj_name,
                          mmio_range_size);

    /* TODO: PVG framework supports serialising device state: integrate it! */
    if (apple_gfx_mig_blocker == NULL) {
        error_setg(&apple_gfx_mig_blocker,
                  "Migration state blocked by apple-gfx display device");
        r = migrate_add_blocker(&apple_gfx_mig_blocker, &local_err);
        if (r < 0) {
            error_report_err(local_err);
        }
    }

    cocoa_enable_runloop_on_main_thread();
}

static void apple_gfx_register_task_mapping_handlers(AppleGFXState *s,
                                                     PGDeviceDescriptor *desc)
{
    desc.createTask = ^(uint64_t vmSize, void * _Nullable * _Nonnull baseAddress) {
        AppleGFXTask *task = apple_gfx_new_task(s, vmSize);
        *baseAddress = (void*)task->address;
        trace_apple_gfx_create_task(vmSize, *baseAddress);
        return task;
    };

    desc.destroyTask = ^(AppleGFXTask * _Nonnull task) {
        trace_apple_gfx_destroy_task(task);
        QTAILQ_REMOVE(&s->tasks, task, node);
        mach_vm_deallocate(mach_task_self(), task->address, task->len);
        g_free(task);
    };

    desc.mapMemory = ^(AppleGFXTask * _Nonnull task, uint32_t rangeCount,
                       uint64_t virtualOffset, bool readOnly,
                       PGPhysicalMemoryRange_t * _Nonnull ranges) {
        kern_return_t r;
        mach_vm_address_t target, source;
        trace_apple_gfx_map_memory(task, rangeCount, virtualOffset, readOnly);
        for (int i = 0; i < rangeCount; i++) {
            PGPhysicalMemoryRange_t *range = &ranges[i];
            MemoryRegion *tmp_mr;
            /* TODO: Bounds checks? r/o? */
            bql_lock();

            trace_apple_gfx_map_memory_range(i, range->physicalAddress,
                                             range->physicalLength);

            target = task->address + virtualOffset;
            source = (mach_vm_address_t)gpa2hva(&tmp_mr,
                                                range->physicalAddress,
                                                range->physicalLength, NULL);
            vm_prot_t cur_protection = 0;
            vm_prot_t max_protection = 0;
            // Map guest RAM at range->physicalAddress into PG task memory range
            r = mach_vm_remap(mach_task_self(),
                              &target, range->physicalLength, vm_page_size - 1,
                              VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE,
                              mach_task_self(),
                              source, false /* shared mapping, no copy */,
                              &cur_protection, &max_protection,
                              VM_INHERIT_COPY);
            trace_apple_gfx_remap(r, source, target);
            g_assert(r == KERN_SUCCESS);

            bql_unlock();

            virtualOffset += range->physicalLength;
        }
        return (bool)true;
    };

    desc.unmapMemory = ^(AppleGFXTask * _Nonnull task, uint64_t virtualOffset,
                         uint64_t length) {
        kern_return_t r;
        mach_vm_address_t range_address;

        trace_apple_gfx_unmap_memory(task, virtualOffset, length);

        /* Replace task memory range with fresh pages, undoing the mapping
         * from guest RAM. */
        range_address = task->address + virtualOffset;
        r = mach_vm_allocate(mach_task_self(), &range_address, length,
                             VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE);
        g_assert(r == KERN_SUCCESS);

        return (bool)true;
    };

    desc.readMemory = ^(uint64_t physicalAddress, uint64_t length,
                        void * _Nonnull dst) {
        trace_apple_gfx_read_memory(physicalAddress, length, dst);
        cpu_physical_memory_read(physicalAddress, dst, length);
        return (bool)true;
    };

}

static PGDisplayDescriptor *apple_gfx_prepare_display_handlers(AppleGFXState *s)
{
    PGDisplayDescriptor *disp_desc = [PGDisplayDescriptor new];

    disp_desc.name = @"QEMU display";
    disp_desc.sizeInMillimeters = NSMakeSize(400., 300.); /* A 20" display */
    disp_desc.queue = dispatch_get_main_queue();
    disp_desc.newFrameEventHandler = ^(void) {
        trace_apple_gfx_new_frame();
        dispatch_async(s->render_queue, ^{
            /* Drop frames if we get too far ahead. */
            if (s->pending_frames >= 2)
                return;
            ++s->pending_frames;
            if (s->pending_frames > 1) {
                return;
            }
            @autoreleasepool {
                apple_gfx_render_new_frame(s);
            }
        });
    };
    disp_desc.modeChangeHandler = ^(PGDisplayCoord_t sizeInPixels,
                                    OSType pixelFormat) {
        trace_apple_gfx_mode_change(sizeInPixels.x, sizeInPixels.y);
        set_mode(s, sizeInPixels.x, sizeInPixels.y);
    };
    disp_desc.cursorGlyphHandler = ^(NSBitmapImageRep *glyph,
                                     PGDisplayCoord_t hotSpot) {
        uint32_t bpp = glyph.bitsPerPixel;
        size_t width = glyph.pixelsWide;
        size_t height = glyph.pixelsHigh;
        size_t padding_bytes_per_row = glyph.bytesPerRow - width * 4;
        const uint8_t* px_data = glyph.bitmapData;

        trace_apple_gfx_cursor_set(bpp, width, height);

        if (s->cursor) {
            cursor_unref(s->cursor);
            s->cursor = NULL;
        }

        if (bpp == 32) { /* Shouldn't be anything else, but just to be safe...*/
            s->cursor = cursor_alloc(width, height);
            s->cursor->hot_x = hotSpot.x;
            s->cursor->hot_y = hotSpot.y;

            uint32_t *dest_px = s->cursor->data;

            for (size_t y = 0; y < height; ++y) {
                for (size_t x = 0; x < width; ++x) {
                    /* NSBitmapImageRep's red & blue channels are swapped
                     * compared to QEMUCursor's. */
                    *dest_px =
                        (px_data[0] << 16u) |
                        (px_data[1] <<  8u) |
                        (px_data[2] <<  0u) |
                        (px_data[3] << 24u);
                    ++dest_px;
                    px_data += 4;
                }
                px_data += padding_bytes_per_row;
            }
            dpy_cursor_define(s->con, s->cursor);
            update_cursor(s);
        }
    };
    disp_desc.cursorShowHandler = ^(BOOL show) {
        trace_apple_gfx_cursor_show(show);
        s->cursor_show = show;
        update_cursor(s);
    };
    disp_desc.cursorMoveHandler = ^(void) {
        trace_apple_gfx_cursor_move();
        update_cursor(s);
    };

    return disp_desc;
}

static NSArray<PGDisplayMode*>* apple_gfx_create_display_mode_array(
    const AppleGFXDisplayMode display_modes[], int display_mode_count)
{
    PGDisplayMode **modes = alloca(sizeof(modes[0]) * display_mode_count);
    NSArray<PGDisplayMode*>* mode_array = nil;
    int i;

    for (i = 0; i < display_mode_count; i++) {
        const AppleGFXDisplayMode *mode = &display_modes[i];
        PGDisplayCoord_t mode_size = { mode->width_px, mode->height_px };
        modes[i] =
            [[PGDisplayMode alloc] initWithSizeInPixels:mode_size
                                        refreshRateInHz:mode->refresh_rate_hz];
    }

    mode_array = [NSArray arrayWithObjects:modes count:display_mode_count];

    for (i = 0; i < display_mode_count; i++) {
        [modes[i] release];
        modes[i] = nil;
    }

    return mode_array;
}

static id<MTLDevice> copy_suitable_metal_device(void)
{
    id<MTLDevice> dev = nil;
    NSArray<id<MTLDevice>> *devs = MTLCopyAllDevices();

    /* Prefer a unified memory GPU. Failing that, pick a non-removable GPU. */
    for (size_t i = 0; i < devs.count; ++i) {
        if (devs[i].hasUnifiedMemory) {
            dev = devs[i];
            break;
        }
        if (!devs[i].removable) {
            dev = devs[i];
        }
    }

    if (dev != nil) {
        [dev retain];
    } else {
        dev = MTLCreateSystemDefaultDevice();
    }
    [devs release];

    return dev;
}

void apple_gfx_common_realize(AppleGFXState *s, PGDeviceDescriptor *desc)
{
    PGDisplayDescriptor *disp_desc = nil;
    const AppleGFXDisplayMode *display_modes = apple_gfx_default_modes;
    int num_display_modes = ARRAY_SIZE(apple_gfx_default_modes);

    QTAILQ_INIT(&s->tasks);
    s->render_queue = dispatch_queue_create("apple-gfx.render",
                                            DISPATCH_QUEUE_SERIAL);
    s->mtl = copy_suitable_metal_device();
    s->mtl_queue = [s->mtl newCommandQueue];

    desc.device = s->mtl;

    apple_gfx_register_task_mapping_handlers(s, desc);

    s->pgdev = PGNewDeviceWithDescriptor(desc);

    disp_desc = apple_gfx_prepare_display_handlers(s);
    s->pgdisp = [s->pgdev newDisplayWithDescriptor:disp_desc
                                              port:0 serialNum:1234];
    [disp_desc release];

    if (s->display_modes.modes != NULL && s->display_modes.modes->len > 0) {
        display_modes =
            &g_array_index(s->display_modes.modes, AppleGFXDisplayMode, 0);
        num_display_modes = s->display_modes.modes->len;
    }
    s->pgdisp.modeList =
        apple_gfx_create_display_mode_array(display_modes, num_display_modes);

    create_fb(s);
}

void apple_gfx_get_display_modes(AppleGFXDisplayModeList *mode_list, Visitor *v,
                                 const char *name, Error **errp)
{
    GArray *modes = mode_list->modes;
    /* 3 uint16s (max 5 digits) and 3 separator characters per mode + nul. */
    size_t buffer_size = (5 + 1) * 3 * modes->len + 1;

    char *buffer = alloca(buffer_size);
    char *pos = buffer;

    unsigned used = 0;
    buffer[0] = '\0';
    for (guint i = 0; i < modes->len; ++i)
    {
        AppleGFXDisplayMode *mode =
            &g_array_index(modes, AppleGFXDisplayMode, i);
        int rc = snprintf(pos, buffer_size - used,
                          "%s%"PRIu16"x%"PRIu16"@%"PRIu16,
                          i > 0 ? ":" : "",
                          mode->width_px, mode->height_px,
                          mode->refresh_rate_hz);
        used += rc;
        pos += rc;
        assert(used < buffer_size);
    }

    pos = buffer;
    visit_type_str(v, name, &pos, errp);
}

void apple_gfx_set_display_modes(AppleGFXDisplayModeList *mode_list, Visitor *v,
                                 const char *name, Error **errp)
{
    Error *local_err = NULL;
    const char *endptr;
    char *str;
    int ret;
    unsigned int val;
    uint32_t num_modes;
    GArray *modes;
    uint32_t mode_idx;

    visit_type_str(v, name, &str, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    // Count colons to estimate modes. No leading/trailing colons so start at 1.
    num_modes = 1;
    for (size_t i = 0; str[i] != '\0'; ++i)
    {
        if (str[i] == ':') {
            ++num_modes;
        }
    }

    modes = g_array_sized_new(false, true, sizeof(AppleGFXDisplayMode), num_modes);

    endptr = str;
    for (mode_idx = 0; mode_idx < num_modes; ++mode_idx)
    {
        AppleGFXDisplayMode mode = {};
        if (mode_idx > 0)
        {
            if (*endptr != ':') {
                goto separator_error;
            }
            ++endptr;
        }

        ret = qemu_strtoui(endptr, &endptr, 10, &val);
        if (ret || val > UINT16_MAX || val == 0) {
            error_setg(errp, "width of '%s' must be a decimal integer number "
                       "of pixels in the range 1..65535", name);
            goto out;
        }
        mode.width_px = val;
        if (*endptr != 'x') {
            goto separator_error;
        }

        ret = qemu_strtoui(endptr + 1, &endptr, 10, &val);
        if (ret || val > UINT16_MAX || val == 0) {
            error_setg(errp, "height of '%s' must be a decimal integer number "
                       "of pixels in the range 1..65535", name);
            goto out;
        }
        mode.height_px = val;
        if (*endptr != '@') {
            goto separator_error;
        }

        ret = qemu_strtoui(endptr + 1, &endptr, 10, &val);
        if (ret) {
            error_setg(errp, "refresh rate of '%s'"
                       " must be a non-negative decimal integer (Hertz)", name);
        }
        mode.refresh_rate_hz = val;
        g_array_append_val(modes, mode);
    }

    mode_list->modes = modes;
    goto out;

separator_error:
    error_setg(errp, "Each display mode takes the format "
               "'<width>x<height>@<rate>', modes are separated by colons. (:)");
out:
    g_free(str);
    return;
}
