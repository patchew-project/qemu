#ifndef QEMU_APPLE_GFX_H
#define QEMU_APPLE_GFX_H

#define TYPE_APPLE_GFX_VMAPPLE      "apple-gfx-vmapple"
#define TYPE_APPLE_GFX_PCI          "apple-gfx-pci"

#include "qemu/typedefs.h"

typedef struct AppleGFXState AppleGFXState;

void apple_gfx_common_init(Object *obj, AppleGFXState *s, const char* obj_name);

#ifdef __OBJC__

#include "qemu/osdep.h"
#include "exec/memory.h"
#include "ui/surface.h"
#include <dispatch/dispatch.h>

@class PGDeviceDescriptor;
@protocol PGDevice;
@protocol PGDisplay;
@protocol MTLDevice;
@protocol MTLTexture;
@protocol MTLCommandQueue;

typedef QTAILQ_HEAD(, PGTask_s) AppleGFXTaskList;

struct AppleGFXState {
    MemoryRegion iomem_gfx;
    id<PGDevice> pgdev;
    id<PGDisplay> pgdisp;
    AppleGFXTaskList tasks;
    QemuConsole *con;
    id<MTLDevice> mtl;
    id<MTLCommandQueue> mtl_queue;
    bool handles_frames;
    bool new_frame;
    bool cursor_show;
    QEMUCursor *cursor;

    dispatch_queue_t render_queue;
    /* The following fields should only be accessed from render_queue: */
    bool gfx_update_requested;
    bool new_frame_ready;
    int32_t pending_frames;
    void *vram;
    DisplaySurface *surface;
    id<MTLTexture> texture;
};

void apple_gfx_common_realize(AppleGFXState *s, PGDeviceDescriptor *desc);

#endif /* __OBJC__ */

#endif
