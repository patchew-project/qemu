/*
 * QEMU BIOS e820 routines
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_I386_E820_H
#define HW_I386_E820_H

/**
 * E820Type: Type of the e820 address range.
 */
typedef enum {
    E820_RAM        = 1,
    E820_RESERVED   = 2,
    E820_ACPI       = 3,
    E820_NVS        = 4,
    E820_UNUSABLE   = 5
} E820Type;

#define E820_NR_ENTRIES 16

struct e820_entry {
    uint64_t address;
    uint64_t length;
    uint32_t type;
} QEMU_PACKED __attribute((__aligned__(4)));

struct e820_table {
    uint32_t count;
    struct e820_entry entry[E820_NR_ENTRIES];
} QEMU_PACKED __attribute((__aligned__(4)));

extern struct e820_table e820_reserve;
extern struct e820_entry *e820_table;

/**
 * e820_add_entry: Add an #e820_entry to the @e820_table.
 *
 * Returns the number of entries of the e820_table on success,
 *         or a negative errno otherwise.
 *
 * @address: The base address of the structure which the BIOS is to fill in.
 * @length: The length in bytes of the structure passed to the BIOS.
 * @type: The #E820Type of the address range.
 */
ssize_t e820_add_entry(uint64_t address, uint64_t length, E820Type type);

/**
 * e820_get_num_entries: The number of entries of the @e820_table.
 *
 * Returns the number of entries of the e820_table.
 */
size_t e820_get_num_entries(void);

/**
 * e820_get_entry: Get the address/length of an #e820_entry.
 *
 * If the #e820_entry stored at @index is of #E820Type @type, fills @address
 * and @length with the #e820_entry values and return @true.
 * Return @false otherwise.
 *
 * @index: The index of the #e820_entry to get values.
 * @type: The @E820Type of the address range expected.
 * @address: Pointer to the base address of the #e820_entry structure to
 *           be filled.
 * @length: Pointer to the length (in bytes) of the #e820_entry structure
 *          to be filled.
 * @return: true if the entry was found, false otherwise.
 */
bool e820_get_entry(unsigned int index, E820Type type,
                    uint64_t *address, uint64_t *length);

#endif
