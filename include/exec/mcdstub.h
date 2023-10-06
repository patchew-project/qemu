#ifndef MCDSTUB_H
#define MCDSTUB_H

#define DEFAULT_MCDSTUB_PORT "1234"

/* MCD breakpoint/watchpoint types */
#define MCD_BREAKPOINT_SW        0
#define MCD_BREAKPOINT_HW        1
#define MCD_WATCHPOINT_WRITE     2
#define MCD_WATCHPOINT_READ      3
#define MCD_WATCHPOINT_ACCESS    4


/* Get or set a register.  Returns the size of the register.  */
typedef int (*gdb_get_reg_cb)(CPUArchState *env, GByteArray *buf, int reg);
typedef int (*gdb_set_reg_cb)(CPUArchState *env, uint8_t *buf, int reg);
void gdb_register_coprocessor(CPUState *cpu,
                              gdb_get_reg_cb get_reg, gdb_set_reg_cb set_reg,
                              int num_regs, const char *xml, int g_pos);

/**
 * mcdserver_start: start the mcd server
 * @port_or_device: connection spec for mcd
 *
 * This is a TCP port
 */
int mcdserver_start(const char *port_or_device);

void gdb_set_stop_cpu(CPUState *cpu);

#endif
