#define TARGET_LONG_BITS 64
#define TARGET_PHYS_ADDR_SPACE_BITS 64
#define TARGET_VIRT_ADDR_SPACE_BITS 64
/* FIXME: MB uses variable pages down to 1K but linux only uses 4k.  */
#define TARGET_PAGE_BITS 12
#define NB_MMU_MODES 3
