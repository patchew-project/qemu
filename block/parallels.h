#ifndef PARALLELS_H
#define PARALLELS_H

#define HEADER_MAGIC "WithoutFreeSpace"
#define HEADER_MAGIC2 "WithouFreSpacExt"
#define HEADER_VERSION 2
#define HEADER_INUSE_MAGIC  (0x746F6E59)

#define DEFAULT_CLUSTER_SIZE 1048576        /* 1 MiB */


// always little-endian
typedef struct ParallelsHeader {
    char magic[16]; // "WithoutFreeSpace"
    uint32_t version;
    uint32_t heads;
    uint32_t cylinders;
    uint32_t tracks;
    uint32_t bat_entries;
    uint64_t nb_sectors;
    uint32_t inuse;
    uint32_t data_off;
    char padding[12];
} QEMU_PACKED ParallelsHeader;

#endif
