#include "ebpf/ebpf.h"
#include <sys/syscall.h>
#include "trace.h"

#define ptr_to_u64(x) ((uint64_t)(uintptr_t)x)

static inline int ebpf(enum bpf_cmd cmd, union bpf_attr *attr,
        unsigned int size)
{
    int ret = syscall(__NR_bpf, cmd, attr, size);
    if (ret < 0) {
        trace_ebpf_error("eBPF syscall error", strerror(errno));
    }

    return ret;
}

int bpf_create_map(enum bpf_map_type map_type,
                   unsigned int key_size,
                   unsigned int value_size,
                   unsigned int max_entries)
{
    union bpf_attr attr = {
            .map_type    = map_type,
            .key_size    = key_size,
            .value_size  = value_size,
            .max_entries = max_entries
    };

    return ebpf(BPF_MAP_CREATE, &attr, sizeof(attr));
}

int bpf_lookup_elem(int fd, const void *key, void *value)
{
    union bpf_attr attr = {
            .map_fd = (uint32_t)fd,
            .key    = ptr_to_u64(key),
            .value  = ptr_to_u64(value),
    };

    return ebpf(BPF_MAP_LOOKUP_ELEM, &attr, sizeof(attr));
}

int bpf_update_elem(int fd, const void *key, const void *value,
                    uint64_t flags)
{
    union bpf_attr attr = {
            .map_fd = (uint32_t)fd,
            .key    = ptr_to_u64(key),
            .value  = ptr_to_u64(value),
            .flags  = flags,
    };

    return ebpf(BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
}

int bpf_delete_elem(int fd, const void *key)
{
    union bpf_attr attr = {
            .map_fd = (uint32_t)fd,
            .key    = ptr_to_u64(key),
    };

    return ebpf(BPF_MAP_DELETE_ELEM, &attr, sizeof(attr));
}

#define BPF_LOG_BUF_SIZE (UINT32_MAX >> 8)
static char bpf_log_buf[BPF_LOG_BUF_SIZE] = {};

int bpf_prog_load(enum bpf_prog_type type,
                  const struct bpf_insn *insns, int insn_cnt,
                  const char *license)
{
    int ret = 0;
    union bpf_attr attr = {};
    attr.prog_type = type;
    attr.insns     = ptr_to_u64(insns);
    attr.insn_cnt  = (uint32_t)insn_cnt;
    attr.license   = ptr_to_u64(license);
    attr.log_buf   = ptr_to_u64(bpf_log_buf);
    attr.log_size  = BPF_LOG_BUF_SIZE;
    attr.log_level = 1;

    ret = ebpf(BPF_PROG_LOAD, &attr, sizeof(attr));
    if (ret < 0) {
        trace_ebpf_error("eBPF program load error:", bpf_log_buf);
    }

    return ret;
}

unsigned int bpf_fixup_mapfd(struct fixup_mapfd_t *table,
                             size_t table_size, struct bpf_insn *insn,
                             size_t insn_len, const char *map_name, int fd) {
    unsigned int ret = 0;
    int i = 0;

    for (; i < table_size; ++i) {
        if (strcmp(table[i].map_name, map_name) == 0) {
            insn[table[i].instruction_num].src_reg = 1;
            insn[table[i].instruction_num].imm = fd;
            ++ret;
        }
    }

    return ret;
}
