#ifndef HW_I386_PVH_H
#define HW_I386_PVH_H

size_t pvh_get_start_addr(void);

bool pvh_load_elfboot(const char *kernel_filename,
                      uint32_t *mh_load_addr,
                      uint32_t *elf_kernel_size);

#endif
