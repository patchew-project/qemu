/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * C API for connecting to the VFIOUserPCIDriver DriverKit extension.
 *
 * The vfio-user server process uses this to:
 *  1. Find and open an IOUserClient to the dext for a given PCI BDF.
 *  2. Claim the device so the dext opens its IOPCIDevice provider.
 *  3. Register client-owned memory (QEMU guest RAM mapped via shared file)
 *     for DMA by the physical PCI device.
 *  4. Unregister DMA regions when QEMU removes them.
 *
 * Integration with libvfio-user:
 *   vfu_dma_register_cb_t  ->  apple_dext_register_dma()
 *   vfu_dma_unregister_cb_t -> apple_dext_unregister_dma()
 *
 * Copyright (c) 2026 Scott J. Goldman
 */

#ifndef HW_VFIO_APPLE_DEXT_CLIENT_H
#define HW_VFIO_APPLE_DEXT_CLIENT_H

#include <IOKit/IOKitLib.h>
#include <stdint.h>

/*
 * Find the VFIOUserPCIDriver dext instance matching the given PCI BDF
 * and open an IOUserClient connection to it.
 * Returns IO_OBJECT_NULL on failure.
 */
io_connect_t apple_dext_connect(uint8_t bus, uint8_t device,
                                    uint8_t function);

/*
 * Close a previously opened connection.
 */
void apple_dext_disconnect(io_connect_t connection);

/*
 * Claim the PCI device through the dext (opens the IOPCIDevice provider).
 * Must be called before registering DMA regions.
 */
kern_return_t apple_dext_claim(io_connect_t connection);

/*
 * Register a region of this process's address space for DMA.
 *
 * @iova:         guest IOVA (device-visible DMA address)
 * @client_va:    virtual address of the memory in this process
 * @size:         region size in bytes
 * @out_bus_addr: receives first DMA bus address segment (may be NULL)
 * @out_bus_len:  receives first DMA bus address segment length (may be NULL)
 *
 * The memory at client_va must remain valid and mapped until the region
 * is unregistered.
 */
kern_return_t apple_dext_register_dma(io_connect_t connection,
                                          uint64_t iova,
                                          uint64_t client_va,
                                          uint64_t size,
                                          uint64_t *out_bus_addr,
                                          uint64_t *out_bus_len);

/*
 * Unregister a previously registered DMA region identified by its IOVA.
 */
kern_return_t apple_dext_unregister_dma(io_connect_t connection,
                                            uint64_t iova);

/*
 * Read 8 bytes from a registered DMA region's IOMemoryDescriptor.
 * Used to verify the descriptor references the same physical pages
 * as the client's virtual mapping.
 *
 * @iova:     base IOVA of the registered region
 * @offset:   byte offset within the region to read from
 * @out_word: receives the 8-byte value read from the descriptor
 */
kern_return_t apple_dext_probe_dma(io_connect_t connection,
                                       uint64_t iova,
                                       uint64_t offset,
                                       uint64_t *out_word);

/*
 * Read from PCI configuration space.
 *
 * @offset: byte offset into config space
 * @width:  access width in bytes (1, 2, or 4)
 * @out_value: receives the value read
 */
kern_return_t apple_dext_config_read(io_connect_t connection,
                                         uint64_t offset,
                                         uint64_t width,
                                         uint64_t *out_value);

/*
 * Write to PCI configuration space.
 *
 * @offset: byte offset into config space
 * @width:  access width in bytes (1, 2, or 4)
 * @value:  value to write
 */
kern_return_t apple_dext_config_write(io_connect_t connection,
                                          uint64_t offset,
                                          uint64_t width,
                                          uint64_t value);

/*
 * Read a contiguous block of PCI configuration space.
 * Internally issues repeated 32-bit reads, with a final
 * narrower read for any trailing bytes.
 *
 * @offset: starting byte offset
 * @buf:    destination buffer
 * @len:    number of bytes to read
 */
kern_return_t apple_dext_config_read_block(io_connect_t connection,
                                               uint64_t offset,
                                               void *buf,
                                               size_t len);

/*
 * Query BAR information from the PCI device.
 *
 * @bar:          BAR index (0-5)
 * @out_mem_idx:  receives the memory index for MemoryRead/Write calls
 * @out_size:     receives the BAR size in bytes
 * @out_type:     receives the BAR type (mem32, mem64, io, etc.)
 */
kern_return_t apple_dext_get_bar_info(io_connect_t connection,
                                          uint8_t bar,
                                          uint8_t *out_mem_idx,
                                          uint64_t *out_size,
                                          uint8_t *out_type);

/*
 * Map a PCI BAR directly into this process through the dext.
 *
 * The dext supplies the BAR's IOMemoryDescriptor and IOKit applies the
 * appropriate cache mode for the BAR type (default-cache for BAR0 style
 * register windows, write-combine for prefetchable apertures).
 *
 * @bar:      BAR index (0-5)
 * @out_addr: receives the mapped virtual address
 * @out_size: receives the mapped size
 * @out_type: receives the BAR type (may be NULL)
 */
kern_return_t apple_dext_map_bar(io_connect_t connection,
                                     uint8_t bar,
                                     mach_vm_address_t *out_addr,
                                     mach_vm_size_t *out_size,
                                     uint8_t *out_type);

/*
 * Unmap a BAR previously mapped with apple_dext_map_bar().
 */
kern_return_t apple_dext_unmap_bar(io_connect_t connection,
                                       uint8_t bar,
                                       mach_vm_address_t addr);

/*
 * Read from a PCI BAR (MMIO).
 *
 * @mem_idx: memory index from apple_dext_get_bar_info
 * @offset:  byte offset within the BAR
 * @width:   access width in bytes (1, 2, 4, or 8)
 * @out_value: receives the value read
 */
kern_return_t apple_dext_mmio_read(io_connect_t connection,
                                       uint8_t mem_idx,
                                       uint64_t offset,
                                       uint64_t width,
                                       uint64_t *out_value);

/*
 * Write to a PCI BAR (MMIO).
 *
 * @mem_idx: memory index from apple_dext_get_bar_info
 * @offset:  byte offset within the BAR
 * @width:   access width in bytes (1, 2, 4, or 8)
 * @value:   value to write
 */
kern_return_t apple_dext_mmio_write(io_connect_t connection,
                                        uint8_t mem_idx,
                                        uint64_t offset,
                                        uint64_t width,
                                        uint64_t value);

/*
 * Set up interrupt forwarding for the PCI device.
 * Creates IOInterruptDispatchSource handlers for all available
 * MSI/MSI-X vectors in the dext. Interrupts are queued in a ring
 * buffer and retrieved via apple_dext_check_interrupt().
 *
 * @out_num_vectors: receives the number of interrupt vectors registered
 */
kern_return_t apple_dext_setup_interrupts(io_connect_t connection,
                                              uint32_t *out_num_vectors);

/*
 * Reset the PCI device via the dext.  Tries FLR first, then falls
 * back to PM reset (D3hot → D0 transition).
 */
kern_return_t apple_dext_reset_device(io_connect_t connection);

/*
 * Set the IRQ enable mask in the dext.  Only vectors with their
 * corresponding bit set will be recorded as pending when the
 * hardware fires.  mask[] is 4 x uint64_t covering 256 vectors.
 */
kern_return_t apple_dext_set_irq_mask(io_connect_t connection,
                                      const uint64_t mask[4]);

/*
 * Read and clear all pending interrupt bits from the dext.
 * Returns up to 256 bits (4 MSI/MSI-X vectors per bit) across
 * 4 uint64_t words.  Each bit that was set is atomically cleared
 * in the dext.
 */
kern_return_t apple_dext_read_pending_irqs(io_connect_t connection,
                                           uint64_t pending[4]);

/*
 * Opaque state for async interrupt notification from the dext.
 */
typedef struct AppleDextInterruptNotify AppleDextInterruptNotify;

/*
 * Create async interrupt notification.  handler_fn is called on a GCD
 * dispatch queue whenever the dext signals that one or more interrupt
 * bits have been set.  The handler should wake the QEMU main loop,
 * which then calls apple_dext_read_pending_irqs() to drain the bits.
 *
 * The notification is armed immediately upon creation.
 */
AppleDextInterruptNotify *
apple_dext_interrupt_notify_create(io_connect_t connection,
                                   void (*handler_fn)(void *opaque),
                                   void *opaque);

/*
 * Re-arm the async interrupt notification after draining pending bits.
 * Must be called after each wakeup to receive subsequent notifications.
 */
kern_return_t
apple_dext_interrupt_notify_rearm(AppleDextInterruptNotify *notify);

/*
 * Tear down and free async interrupt notification state.
 */
void apple_dext_interrupt_notify_destroy(AppleDextInterruptNotify *notify);

#endif /* HW_VFIO_APPLE_DEXT_CLIENT_H */
