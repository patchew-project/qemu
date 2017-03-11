#ifndef DUMP_INFO_H
#define DUMP_INFO_H

typedef struct DumpInfo {
    bool received;
    bool has_phys_base;
    uint64_t phys_base;
    bool has_text;
    uint64_t text;
    char *vmcoreinfo;
} DumpInfo;

extern DumpInfo dump_info;

#endif /* DUMP_INFO_H */
