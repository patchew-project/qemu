/*
 * QEMU VM Introspection Handshake
 *
 */

#ifndef QEMU_VMI_HANDSHAKE_H
#define QEMU_VMI_HANDSHAKE_H

enum { QEMU_VMI_NAME_SIZE = 64 };
enum { QEMU_VMI_COOKIE_HASH_SIZE = 20};

enum {
    QEMU_VMI_CPU_TYPE_I386 = 0,
    QEMU_VMI_CPU_TYPE_X86_64 = 1,
    QEMU_VMI_CPU_TYPE_UNKNOWN = 255
};

typedef struct qemu_vmi_e820_entry {
    uint64_t address;
    uint64_t length;
    uint32_t type;
    uint32_t padding;
} qemu_vmi_e820_entry;

typedef struct qemu_vmi_to_introspector_x86 {
   uint8_t e820_count;
   uint8_t padding[3];
   qemu_vmi_e820_entry e820_entries[0];
} qemu_vmi_to_introspector_x86;

/**
 * qemu_vmi_to_introspector:
 *
 * This structure is passed to the introspection tool during the handshake.
 *
 * @struct_size: the structure size
 * @uuid: the UUID
 * @start_time: the VM start time
 * @name: the VM name
 */
typedef struct qemu_vmi_to_introspector {
    uint32_t struct_size;
    uint8_t  uuid[16];
    uint8_t  cpu_type;
    uint8_t  padding[3];
    int64_t  start_time;
    char     name[QEMU_VMI_NAME_SIZE];
    qemu_vmi_to_introspector_x86 arch;
    /* ... */
} qemu_vmi_to_introspector;

/**
 * qemu_vmi_from_introspector:
 *
 * This structure is passed by the introspection tool during the handshake.
 *
 * @struct_size: the structure size
 * @cookie_hash: the hash of the cookie know by the introspection tool
 */
typedef struct qemu_vmi_from_introspector {
    uint32_t struct_size;
    uint8_t  cookie_hash[QEMU_VMI_COOKIE_HASH_SIZE];
    /* ... */
} qemu_vmi_from_introspector;

#endif /* QEMU_VMI_HANDSHAKE_H */
