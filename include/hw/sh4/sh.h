#ifndef QEMU_SH_H
#define QEMU_SH_H
/* Definitions for SH board emulation.  */

#include "hw/sh4/sh_intc.h"
#include "target/sh4/cpu-qom.h"

#define A7ADDR(x) ((x) & 0x1fffffff)
#define P4ADDR(x) ((x) | 0xe0000000)

/* sh7750.c */
struct SH7750State;
struct MemoryRegion;

struct SH7750State *sh7750_init(SuperHCPU *cpu, struct MemoryRegion *sysmem);

typedef struct {
    /* The callback will be triggered if any of the designated lines change */
    uint16_t portamask_trigger;
    uint16_t portbmask_trigger;
    /* Return 0 if no action was taken */
    int (*port_change_cb) (uint16_t porta, uint16_t portb,
			   uint16_t * periph_pdtra,
			   uint16_t * periph_portdira,
			   uint16_t * periph_pdtrb,
			   uint16_t * periph_portdirb);
} sh7750_io_device;

int sh7750_register_io_device(struct SH7750State *s,
			      sh7750_io_device * device);
/* sh7750.c */
qemu_irq sh7750_irl(struct SH7750State *s);

/* tc58128.c */
int tc58128_init(struct SH7750State *s, const char *zone1, const char *zone2);

#endif
