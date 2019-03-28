#define TARGET_LONG_BITS 32
/* Coldfire Linux uses 8k pages
 * and m68k linux uses 4k pages
 * use the smallest one
 */
#define TARGET_PAGE_BITS 12
#define TARGET_PHYS_ADDR_SPACE_BITS 32
#define TARGET_VIRT_ADDR_SPACE_BITS 32
#define NB_MMU_MODES 2
