#include "cpu.h"

#define BIOS_SIZE (4 * M_BYTE)
#ifdef TARGET_WORDS_BIGENDIAN
#define BIOS_FILENAME "mips_bios.bin"
#else
#define BIOS_FILENAME "mipsel_bios.bin"
#endif
