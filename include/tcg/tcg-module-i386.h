#ifndef TCG_MODULE_I386_H
#define TCG_MODULE_I386_H

struct TCGI386ModuleOps {
    void (*update_fp_status)(CPUX86State *env);
};
extern struct TCGI386ModuleOps tcg_i386;

#endif /* TCG_MODULE_I386_H */
