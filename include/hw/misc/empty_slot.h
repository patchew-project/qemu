/*
 * QEMU Empty Slot
 *
 * The empty_slot device emulates known to a bus but not connected devices.
 *
 * Copyright (c) 2010 Artyom Tarasenko
 *
 * This code is licensed under the GNU GPL v2 or (at your option) any later
 * version.
 */

#ifndef HW_EMPTY_SLOT_H
#define HW_EMPTY_SLOT_H

/**
 * empty_slot_init: create and map a RAZ/WI device
 * @name: name of the device for debug logging
 * @base: base address of the device's MMIO region
 * @size: size of the device's MMIO region
 *
 * This utility function creates and maps an instance of empty slot,
 * which is a dummy device which simply read as zero, and ignore writes.
 * An empty slot sit on a bus, and no bus errors are generated when it is
 * accessed.
 * Guest accesses can be traced, using the '-trace empty_slot\*' command
 * line argument.
 * The device is mapped at priority -10000, which means that you can
 * use it to cover a large region and then map other devices on top of it
 * if necessary.
 */
void empty_slot_init(const char *name, hwaddr addr, uint64_t slot_size);

#endif
