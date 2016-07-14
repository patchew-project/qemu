#ifndef BLOCK_BOCHS_H
#define BLOCK_BOCHS_H

#define HEADER_MAGIC "Bochs Virtual HD Image"
#define HEADER_VERSION 0x00020000
#define HEADER_V1 0x00010000
#define HEADER_SIZE 512

#define REDOLOG_TYPE "Redolog"
#define GROWING_TYPE "Growing"

// not allocated: 0xffffffff

// always little-endian
struct bochs_header {
    char magic[32];     /* "Bochs Virtual HD Image" */
    char type[16];      /* "Redolog" */
    char subtype[16];   /* "Undoable" / "Volatile" / "Growing" */
    uint32_t version;
    uint32_t header;    /* size of header */

    uint32_t catalog;   /* num of entries */
    uint32_t bitmap;    /* bitmap size */
    uint32_t extent;    /* extent size */

    union {
        struct {
            uint32_t reserved;  /* for ??? */
            uint64_t disk;      /* disk size */
            char padding[HEADER_SIZE - 64 - 20 - 12];
        } QEMU_PACKED redolog;
        struct {
            uint64_t disk;      /* disk size */
            char padding[HEADER_SIZE - 64 - 20 - 8];
        } QEMU_PACKED redolog_v1;
        char padding[HEADER_SIZE - 64 - 20];
    } extra;
} QEMU_PACKED;

#endif
