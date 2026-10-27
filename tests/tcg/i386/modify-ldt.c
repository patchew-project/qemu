/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include <asm/ldt.h>
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <unistd.h>

static void check_entry(const struct user_desc *desc, int func,
                        uint64_t expected)
{
    uint64_t entries[2];

    assert(syscall(SYS_modify_ldt, func, desc, sizeof(*desc)) == 0);
    assert(syscall(SYS_modify_ldt, 0, entries, sizeof(entries)) ==
           sizeof(entries));
    /* Linux sets the accessed bit when installing a descriptor. */
    assert((entries[desc->entry_number] & ~(UINT64_C(1) << 40)) == expected);
}

int main(void)
{
    struct user_desc desc = {
        .entry_number = 1,
        .base_addr = 0x89abcdef,
        .limit = 0x54321,
        .seg_32bit = 1,
        .limit_in_pages = 1,
        .useable = 1,
    };
    const unsigned int invalid_sizes[] = {0, 15, 17, 24};

    _Static_assert(sizeof(desc) == 16, "modify_ldt user_desc must be 16 bytes");
    for (unsigned int i = 0;
         i < sizeof(invalid_sizes) / sizeof(invalid_sizes[0]); i++) {
        long ret = syscall(SYS_modify_ldt, 0x11, &desc, invalid_sizes[i]);

        /* Linux returns int-sized errors; libc may have translated them. */
        assert((int)ret == -EINVAL || (ret == -1 && errno == EINVAL));
    }
    for (int func = 1; func <= 0x11; func += 0x10) {
        for (int contents = MODIFY_LDT_CONTENTS_DATA;
             contents <= MODIFY_LDT_CONTENTS_CODE; contents += 2) {
            uint64_t expected = UINT64_C(0x89c5f2abcdef4321);

            desc.contents = contents;
            if (contents == MODIFY_LDT_CONTENTS_CODE) {
                expected |= UINT64_C(1) << 43;
            }
            if (func == 0x11) {
                expected |= UINT64_C(1) << 52;
            }
            check_entry(&desc, func, expected);
#ifdef __x86_64__
            desc.lm = 1;
            check_entry(&desc, func, expected);
            desc.lm = 0;
#endif
        }
    }
    desc = (struct user_desc) {
        .entry_number = 1,
        .read_exec_only = 1,
        .seg_not_present = 1,
    };
#ifdef __x86_64__
    desc.lm = 1;
#endif
    check_entry(&desc, 0x11, 0);
    return 0;
}
