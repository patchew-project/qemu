#ifndef QCOW_H
#define QCOW_H

#define QCOW_MAGIC (('Q' << 24) | ('F' << 16) | ('I' << 8) | 0xfb)
#define QCOW_VERSION 1

typedef struct QCowHeader {
    uint32_t magic;
    uint32_t version;
    uint64_t backing_file_offset;
    uint32_t backing_file_size;
    uint32_t mtime;
    uint64_t size; /* in bytes */
    uint8_t cluster_bits;
    uint8_t l2_bits;
    uint16_t padding;
    uint32_t crypt_method;
    uint64_t l1_table_offset;
} QEMU_PACKED QCowHeader;

#endif
