#ifndef QEMU_AARCH64_CPUID_H
#define QEMU_AARCH64_CPUID_H

#if defined(__aarch64__)
uint64_t get_aarch64_cpu_id(void);
bool is_thunderx_pass2_cpu(void);
#endif

#endif
