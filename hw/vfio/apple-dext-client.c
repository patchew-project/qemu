/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * C client implementation for communicating with the VFIOUserPCIDriver dext
 * via IOKit IOUserClient.
 *
 * Copyright (c) 2026 Scott J. Goldman
 */

#include "qemu/osdep.h"

#include "apple-dext-client.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <dispatch/dispatch.h>
#include <string.h>

enum {
    kSelectorGetIdentity        = 0,
    kSelectorClaim              = 1,
    kSelectorTerminate          = 2,
    kSelectorAllocateDMABuffer  = 3,
    kSelectorFreeDMABuffer      = 4,
    kSelectorRegisterDMARegion  = 5,
    kSelectorUnregisterDMARegion = 6,
    kSelectorProbeDMARegion     = 7,
    kSelectorConfigRead         = 8,
    kSelectorConfigWrite        = 9,
    kSelectorGetBARInfo         = 10,
    kSelectorMMIORead           = 11,
    kSelectorMMIOWrite          = 12,
    kSelectorSetupInterrupts    = 13,
    kSelectorCheckInterrupt     = 14,
    kSelectorWaitInterrupt      = 15,
    kSelectorSetIRQMask         = 16,
    kSelectorResetDevice        = 17,
};

/*
 * Keep this in sync with PCIDriverKit BAR type encoding. Bit 3 indicates
 * prefetchability for memory BARs.
 */
#define APPLE_DEXT_BAR_PREFETCHABLE_MASK 0x08
#ifndef kIOMapWriteCombineCache
#define kIOMapWriteCombineCache 0x00000400
#endif

static bool
dext_service_matches_class(io_service_t service, const char *className)
{
    bool match = false;
    CFTypeRef ref;

    ref = IORegistryEntryCreateCFProperty(service, CFSTR("IOUserClass"),
                                          kCFAllocatorDefault, 0);
    if (ref == NULL) {
        return false;
    }

    if (CFGetTypeID(ref) == CFStringGetTypeID()) {
        CFStringRef expected = CFStringCreateWithCString(
            kCFAllocatorDefault, className, kCFStringEncodingUTF8);
        if (expected != NULL) {
            match = CFStringCompare((CFStringRef)ref, expected, 0)
                    == kCFCompareEqualTo;
            CFRelease(expected);
        }
    }
    CFRelease(ref);
    return match;
}

static bool
dext_connection_matches_bdf(io_connect_t connection,
                            uint8_t bus, uint8_t device, uint8_t function)
{
    uint64_t output[6] = {0};
    uint32_t outputCount = 6;
    kern_return_t kr;

    kr = IOConnectCallMethod(connection, kSelectorGetIdentity,
                             NULL, 0, NULL, 0,
                             output, &outputCount,
                             NULL, NULL);
    if (kr != KERN_SUCCESS || outputCount < 3) {
        return false;
    }

    return (uint8_t)output[0] == bus &&
           (uint8_t)output[1] == device &&
           (uint8_t)output[2] == function;
}

io_connect_t
apple_dext_connect(uint8_t bus, uint8_t device, uint8_t function)
{
    CFMutableDictionaryRef matching;
    io_iterator_t iterator = IO_OBJECT_NULL;
    io_connect_t result = IO_OBJECT_NULL;
    io_service_t service;
    kern_return_t kr;

    matching = IOServiceMatching("IOUserService");
    if (matching == NULL) {
        return IO_OBJECT_NULL;
    }

    kr = IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator);
    if (kr != KERN_SUCCESS) {
        return IO_OBJECT_NULL;
    }

    while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
        io_connect_t connection = IO_OBJECT_NULL;

        if (!dext_service_matches_class(service, "VFIOUserPCIDriver")) {
            IOObjectRelease(service);
            continue;
        }

        kr = IOServiceOpen(service, mach_task_self(), 0, &connection);
        IOObjectRelease(service);

        if (kr != KERN_SUCCESS) {
            continue;
        }

        if (dext_connection_matches_bdf(connection, bus, device, function)) {
            result = connection;
            break;
        }

        IOServiceClose(connection);
    }

    IOObjectRelease(iterator);
    return result;
}

void
apple_dext_disconnect(io_connect_t connection)
{
    if (connection != IO_OBJECT_NULL) {
        IOServiceClose(connection);
    }
}

kern_return_t
apple_dext_claim(io_connect_t connection)
{
    if (connection == IO_OBJECT_NULL) {
        return kIOReturnBadArgument;
    }

    return IOConnectCallMethod(connection,
                               kSelectorClaim,
                               NULL, 0, NULL, 0,
                               NULL, NULL, NULL, NULL);
}

kern_return_t
apple_dext_register_dma(io_connect_t connection,
                            uint64_t iova,
                            uint64_t client_va,
                            uint64_t size,
                            uint64_t *out_bus_addr,
                            uint64_t *out_bus_len)
{
    uint64_t input[3] = { iova, client_va, size };
    uint64_t output[3] = {0};
    uint32_t outputCount = 3;
    kern_return_t kr;

    if (connection == IO_OBJECT_NULL) {
        return kIOReturnBadArgument;
    }

    kr = IOConnectCallMethod(connection,
                             kSelectorRegisterDMARegion,
                             input, 3,
                             NULL, 0,
                             output, &outputCount,
                             NULL, NULL);
    if (kr != KERN_SUCCESS) {
        return kr;
    }

    if (out_bus_addr != NULL && outputCount >= 2) {
        *out_bus_addr = output[1];
    }
    if (out_bus_len != NULL && outputCount >= 3) {
        *out_bus_len = output[2];
    }

    return kIOReturnSuccess;
}

kern_return_t
apple_dext_unregister_dma(io_connect_t connection,
                              uint64_t iova)
{
    uint64_t input[1] = { iova };

    if (connection == IO_OBJECT_NULL) {
        return kIOReturnBadArgument;
    }

    return IOConnectCallMethod(connection,
                               kSelectorUnregisterDMARegion,
                               input, 1,
                               NULL, 0,
                               NULL, NULL,
                               NULL, NULL);
}

kern_return_t
apple_dext_probe_dma(io_connect_t connection,
                         uint64_t iova,
                         uint64_t offset,
                         uint64_t *out_word)
{
    uint64_t input[2] = { iova, offset };
    uint64_t output[1] = {0};
    uint32_t outputCount = 1;
    kern_return_t kr;

    if (connection == IO_OBJECT_NULL || out_word == NULL) {
        return kIOReturnBadArgument;
    }

    kr = IOConnectCallMethod(connection,
                             kSelectorProbeDMARegion,
                             input, 2,
                             NULL, 0,
                             output, &outputCount,
                             NULL, NULL);
    if (kr != KERN_SUCCESS) {
        return kr;
    }

    *out_word = output[0];
    return kIOReturnSuccess;
}

kern_return_t
apple_dext_config_read(io_connect_t connection,
                           uint64_t offset,
                           uint64_t width,
                           uint64_t *out_value)
{
    uint64_t input[2] = { offset, width };
    uint64_t output[1] = {0};
    uint32_t outputCount = 1;
    kern_return_t kr;

    if (connection == IO_OBJECT_NULL || out_value == NULL) {
        return kIOReturnBadArgument;
    }

    kr = IOConnectCallMethod(connection,
                             kSelectorConfigRead,
                             input, 2,
                             NULL, 0,
                             output, &outputCount,
                             NULL, NULL);
    if (kr != KERN_SUCCESS) {
        return kr;
    }

    *out_value = output[0];
    return kIOReturnSuccess;
}

kern_return_t
apple_dext_config_write(io_connect_t connection,
                            uint64_t offset,
                            uint64_t width,
                            uint64_t value)
{
    uint64_t input[3] = { offset, width, value };

    if (connection == IO_OBJECT_NULL) {
        return kIOReturnBadArgument;
    }

    return IOConnectCallMethod(connection,
                               kSelectorConfigWrite,
                               input, 3,
                               NULL, 0,
                               NULL, NULL,
                               NULL, NULL);
}

kern_return_t
apple_dext_config_read_block(io_connect_t connection,
                                 uint64_t offset,
                                 void *buf,
                                 size_t len)
{
    uint8_t *dst = (uint8_t *)buf;
    uint64_t pos = offset;
    size_t remaining = len;

    if (connection == IO_OBJECT_NULL || buf == NULL) {
        return kIOReturnBadArgument;
    }

    while (remaining >= 4) {
        uint64_t val = 0;
        uint32_t dword;
        kern_return_t kr;

        kr = apple_dext_config_read(connection, pos, 4, &val);
        if (kr != KERN_SUCCESS) {
            return kr;
        }
        dword = (uint32_t)val;
        memcpy(dst, &dword, 4);
        dst += 4;
        pos += 4;
        remaining -= 4;
    }

    while (remaining > 0) {
        uint64_t val = 0;
        kern_return_t kr;

        kr = apple_dext_config_read(connection, pos, 1, &val);
        if (kr != KERN_SUCCESS) {
            return kr;
        }
        *dst = (uint8_t)val;
        dst++;
        pos++;
        remaining--;
    }

    return kIOReturnSuccess;
}

kern_return_t
apple_dext_get_bar_info(io_connect_t connection,
                            uint8_t bar,
                            uint8_t *out_mem_idx,
                            uint64_t *out_size,
                            uint8_t *out_type)
{
    uint64_t input[1] = { bar };
    uint64_t output[3] = {0};
    uint32_t outputCount = 3;
    kern_return_t kr;

    if (connection == IO_OBJECT_NULL) {
        return kIOReturnBadArgument;
    }

    kr = IOConnectCallMethod(connection,
                             kSelectorGetBARInfo,
                             input, 1,
                             NULL, 0,
                             output, &outputCount,
                             NULL, NULL);
    if (kr != KERN_SUCCESS) {
        return kr;
    }

    if (out_mem_idx != NULL) {
        *out_mem_idx = (uint8_t)output[0];
    }
    if (out_size != NULL) {
        *out_size = output[1];
    }
    if (out_type != NULL) {
        *out_type = (uint8_t)output[2];
    }

    return kIOReturnSuccess;
}

kern_return_t
apple_dext_map_bar(io_connect_t connection,
                       uint8_t bar,
                       mach_vm_address_t *out_addr,
                       mach_vm_size_t *out_size,
                       uint8_t *out_type)
{
    uint64_t bar_size = 0;
    uint8_t bar_type = 0;
    uint32_t mem_type;
    mach_vm_address_t addr = 0;
    mach_vm_size_t size = 0;
    IOOptionBits opts = kIOMapAnywhere;
    kern_return_t kr;

    if (connection == IO_OBJECT_NULL || out_addr == NULL || out_size == NULL) {
        return kIOReturnBadArgument;
    }

    kr = apple_dext_get_bar_info(connection, bar, NULL,
                                     &bar_size, &bar_type);
    if (kr != KERN_SUCCESS) {
        return kr;
    }

    /*
     * The memory type for IOConnectMapMemory64 must match the dext's
     * CopyClientMemoryForType expectation:
     * kVFIOUserPCIDriverUserClientMemoryTypeBAR0 (= 1) plus the BAR index.
     * This is NOT the same as the PCIDriverKit internal memoryIndex returned
     * by GetBARInfo.
     */
    mem_type = 1 + (uint32_t)bar;

    if (bar_type & APPLE_DEXT_BAR_PREFETCHABLE_MASK) {
        opts |= kIOMapWriteCombineCache;
    }

    kr = IOConnectMapMemory64(connection, mem_type, mach_task_self(),
                              &addr, &size, opts);
    if (kr != KERN_SUCCESS) {
        return kr;
    }

    *out_addr = addr;
    *out_size = size;
    if (out_type != NULL) {
        *out_type = bar_type;
    }
    return kIOReturnSuccess;
}

kern_return_t
apple_dext_unmap_bar(io_connect_t connection,
                         uint8_t bar,
                         mach_vm_address_t addr)
{
    uint32_t mem_type = 1 + (uint32_t)bar;

    if (connection == IO_OBJECT_NULL || addr == 0) {
        return kIOReturnBadArgument;
    }

    return IOConnectUnmapMemory64(connection, mem_type, mach_task_self(), addr);
}

kern_return_t
apple_dext_mmio_read(io_connect_t connection,
                         uint8_t mem_idx,
                         uint64_t offset,
                         uint64_t width,
                         uint64_t *out_value)
{
    uint64_t input[3] = { mem_idx, offset, width };
    uint64_t output[1] = {0};
    uint32_t outputCount = 1;
    kern_return_t kr;

    if (connection == IO_OBJECT_NULL || out_value == NULL) {
        return kIOReturnBadArgument;
    }

    kr = IOConnectCallMethod(connection,
                             kSelectorMMIORead,
                             input, 3,
                             NULL, 0,
                             output, &outputCount,
                             NULL, NULL);
    if (kr != KERN_SUCCESS) {
        return kr;
    }

    *out_value = output[0];
    return kIOReturnSuccess;
}

kern_return_t
apple_dext_mmio_write(io_connect_t connection,
                          uint8_t mem_idx,
                          uint64_t offset,
                          uint64_t width,
                          uint64_t value)
{
    uint64_t input[4] = { mem_idx, offset, width, value };

    if (connection == IO_OBJECT_NULL) {
        return kIOReturnBadArgument;
    }

    return IOConnectCallMethod(connection,
                               kSelectorMMIOWrite,
                               input, 4,
                               NULL, 0,
                               NULL, NULL,
                               NULL, NULL);
}

kern_return_t
apple_dext_setup_interrupts(io_connect_t connection,
                                uint32_t *out_num_vectors)
{
    uint64_t output[1] = {0};
    uint32_t outputCount = 1;
    kern_return_t kr;

    if (connection == IO_OBJECT_NULL) {
        return kIOReturnBadArgument;
    }

    kr = IOConnectCallMethod(connection,
                             kSelectorSetupInterrupts,
                             NULL, 0,
                             NULL, 0,
                             output, &outputCount,
                             NULL, NULL);
    if (kr != KERN_SUCCESS) {
        return kr;
    }

    if (out_num_vectors != NULL && outputCount >= 1) {
        *out_num_vectors = (uint32_t)output[0];
    }

    return kIOReturnSuccess;
}

kern_return_t
apple_dext_reset_device(io_connect_t connection)
{
    if (connection == IO_OBJECT_NULL) {
        return kIOReturnBadArgument;
    }

    return IOConnectCallMethod(connection,
                               kSelectorResetDevice,
                               NULL, 0, NULL, 0,
                               NULL, NULL, NULL, NULL);
}

kern_return_t
apple_dext_set_irq_mask(io_connect_t connection, const uint64_t mask[4])
{
    if (connection == IO_OBJECT_NULL || mask == NULL) {
        return kIOReturnBadArgument;
    }

    return IOConnectCallMethod(connection,
                               kSelectorSetIRQMask,
                               mask, 4,
                               NULL, 0,
                               NULL, NULL,
                               NULL, NULL);
}

kern_return_t
apple_dext_read_pending_irqs(io_connect_t connection, uint64_t pending[4])
{
    uint64_t output[4] = {0};
    uint32_t outputCount = 4;
    kern_return_t kr;
    uint32_t i;

    if (connection == IO_OBJECT_NULL || pending == NULL) {
        return kIOReturnBadArgument;
    }

    kr = IOConnectCallMethod(connection,
                             kSelectorCheckInterrupt,
                             NULL, 0,
                             NULL, 0,
                             output, &outputCount,
                             NULL, NULL);
    if (kr != KERN_SUCCESS) {
        return kr;
    }

    for (i = 0; i < 4; i++) {
        pending[i] = (i < outputCount) ? output[i] : 0;
    }

    return kIOReturnSuccess;
}

struct AppleDextInterruptNotify {
    io_connect_t connection;
    IONotificationPortRef notifyPort;
    mach_port_t machPort;
    dispatch_queue_t dispatchQueue;
    void (*handler_fn)(void *opaque);
    void *opaque;
};

static void
apple_dext_async_callback(void *refcon, IOReturn result,
                          void **args, uint32_t numArgs)
{
    AppleDextInterruptNotify *notify = refcon;

    if (result == kIOReturnSuccess && notify->handler_fn) {
        notify->handler_fn(notify->opaque);
    }
}

static kern_return_t
apple_dext_interrupt_notify_arm(AppleDextInterruptNotify *notify)
{
    uint64_t asyncRef[kIOAsyncCalloutCount];

    asyncRef[kIOAsyncCalloutFuncIndex] =
        (uint64_t)(uintptr_t)apple_dext_async_callback;
    asyncRef[kIOAsyncCalloutRefconIndex] =
        (uint64_t)(uintptr_t)notify;

    return IOConnectCallAsyncMethod(notify->connection,
                                    kSelectorWaitInterrupt,
                                    notify->machPort,
                                    asyncRef, kIOAsyncCalloutCount,
                                    NULL, 0, NULL, 0,
                                    NULL, NULL, NULL, NULL);
}

AppleDextInterruptNotify *
apple_dext_interrupt_notify_create(io_connect_t connection,
                                   void (*handler_fn)(void *opaque),
                                   void *opaque)
{
    AppleDextInterruptNotify *notify;
    kern_return_t kr;

    if (connection == IO_OBJECT_NULL || handler_fn == NULL) {
        return NULL;
    }

    notify = g_new0(AppleDextInterruptNotify, 1);
    notify->connection = connection;
    notify->handler_fn = handler_fn;
    notify->opaque = opaque;

    notify->notifyPort = IONotificationPortCreate(kIOMainPortDefault);
    if (!notify->notifyPort) {
        g_free(notify);
        return NULL;
    }

    notify->dispatchQueue = dispatch_queue_create(
        "org.qemu.vfio-apple.irq-notify", DISPATCH_QUEUE_SERIAL);
    IONotificationPortSetDispatchQueue(notify->notifyPort,
                                       notify->dispatchQueue);
    notify->machPort = IONotificationPortGetMachPort(notify->notifyPort);

    kr = apple_dext_interrupt_notify_arm(notify);
    if (kr != KERN_SUCCESS) {
        IONotificationPortDestroy(notify->notifyPort);
        dispatch_release(notify->dispatchQueue);
        g_free(notify);
        return NULL;
    }

    return notify;
}

kern_return_t
apple_dext_interrupt_notify_rearm(AppleDextInterruptNotify *notify)
{
    if (!notify) {
        return kIOReturnBadArgument;
    }
    return apple_dext_interrupt_notify_arm(notify);
}

void
apple_dext_interrupt_notify_destroy(AppleDextInterruptNotify *notify)
{
    if (!notify) {
        return;
    }

    IONotificationPortDestroy(notify->notifyPort);
    dispatch_release(notify->dispatchQueue);
    g_free(notify);
}
