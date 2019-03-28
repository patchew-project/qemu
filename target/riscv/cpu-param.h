#if defined(TARGET_RISCV64)
# define TARGET_LONG_BITS 64
# define TARGET_PHYS_ADDR_SPACE_BITS 56 /* 44-bit PPN */
# define TARGET_VIRT_ADDR_SPACE_BITS 48 /* sv48 */
#elif defined(TARGET_RISCV32)
# define TARGET_LONG_BITS 32
# define TARGET_PHYS_ADDR_SPACE_BITS 34 /* 22-bit PPN */
# define TARGET_VIRT_ADDR_SPACE_BITS 32 /* sv32 */
#endif
#define TARGET_PAGE_BITS 12 /* 4 KiB Pages */
#define NB_MMU_MODES 4
