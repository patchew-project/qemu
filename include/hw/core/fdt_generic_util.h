// SPDX-License-Identifier: MIT

#ifndef FDT_GENERIC_UTIL_H
#define FDT_GENERIC_UTIL_H

#include "qemu/help-texts.h"
#include "fdt_generic.h"
#include "system/memory.h"
#include "qom/object.h"

/*
 * create a fdt_generic machine. the top level cpu irqs are required for
 * systems instantiating interrupt devices. The client is responsible for
 * destroying the returned FDTMachineInfo (using fdt_init_destroy_fdti)
 */

FDTMachineInfo *fdt_generic_create_machine(void *fdt, qemu_irq *cpu_irq);

#define TYPE_FDT_GENERIC_MMAP "fdt-generic-mmap"

#define FDT_GENERIC_MMAP_CLASS(klass) \
     OBJECT_CLASS_CHECK(FDTGenericMMapClass, (klass), TYPE_FDT_GENERIC_MMAP)
#define FDT_GENERIC_MMAP_GET_CLASS(obj) \
    OBJECT_GET_CLASS(FDTGenericMMapClass, (obj), TYPE_FDT_GENERIC_MMAP)
#define FDT_GENERIC_MMAP(obj) \
     INTERFACE_CHECK(FDTGenericMMap, (obj), TYPE_FDT_GENERIC_MMAP)

typedef struct FDTGenericMMap {
    /*< private >*/
    Object parent_obj;
} FDTGenericMMap;

/*
 * The number of "things" in the tuple. Not to be confused with the cell length
 * of the tuple (which is variable based on content
 */

#define FDT_GENERIC_REG_TUPLE_LENGTH 4

typedef struct FDTGenericRegPropInfo {
    int n;
    union {
        struct {
            uint64_t *a;
            uint64_t *s;
            uint64_t *b;
            uint64_t *p;
        };
        uint64_t *x[FDT_GENERIC_REG_TUPLE_LENGTH];
    };
    Object **parents;
} FDTGenericRegPropInfo;

typedef struct FDTGenericMMapClass {
    /*< private >*/
    InterfaceClass parent_class;

    /*< public >*/
    bool (*parse_reg)(FDTGenericMMap *obj, FDTGenericRegPropInfo info,
                      Error **errp);
} FDTGenericMMapClass;

#endif /* FDT_GENERIC_UTIL_H */
