#ifndef DUMP_INFO_H
#define DUMP_INFO_H

typedef struct DumpInfo {
    bool received;
    /* kernel base address */
    bool has_phys_base;
    uint64_t phys_base;
    /* "_text" symbol location */
    bool has_text;
    uint64_t text;
    /* the content of /sys/kernel/vmcoreinfo on Linux */
    char *vmcoreinfo;
} DumpInfo;

extern DumpInfo dump_info;

#endif /* DUMP_INFO_H */
