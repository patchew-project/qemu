#ifndef TCG_MODULE_I386_H
#define TCG_MODULE_I386_H

struct TCGI386ModuleOps {
    void (*update_fp_status)(CPUX86State *env);
    void (*update_mxcsr_status)(CPUX86State *env);
    void (*update_mxcsr_from_sse_status)(CPUX86State *env);
    void (*x86_register_ferr_irq)(qemu_irq irq);
    void (*cpu_set_ignne)(void);
    void (*cpu_x86_update_dr7)(CPUX86State *env, uint32_t new_dr7);
};
extern struct TCGI386ModuleOps tcg_i386;

#endif /* TCG_MODULE_I386_H */
