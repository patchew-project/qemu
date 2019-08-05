#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "sysemu/accel.h"
#include "sysemu/fuzz.h"
#include "sysemu/cpus.h"


static void fuzz_setup_post(MachineState *ms, AccelState *accel)
{
}

static int fuzz_init_accel(MachineState *ms)
{
    QemuOpts *opts = qemu_opts_create(qemu_find_opts("icount"), NULL, 0,
                                      &error_abort);
    qemu_opt_set(opts, "shift", "0", &error_abort);
    configure_icount(opts, &error_abort);
    qemu_opts_del(opts);
    return 0;
}

static void fuzz_accel_class_init(ObjectClass *oc, void *data)
{
    AccelClass *ac = ACCEL_CLASS(oc);
    ac->name = "fuzz";
    ac->init_machine = fuzz_init_accel;
    ac->setup_post = fuzz_setup_post;
    ac->allowed = &fuzz_allowed;
}

#define TYPE_FUZZ_ACCEL ACCEL_CLASS_NAME("fuzz")

static const TypeInfo fuzz_accel_type = {
    .name = TYPE_FUZZ_ACCEL,
    .parent = TYPE_ACCEL,
    .class_init = fuzz_accel_class_init,
};

static void fuzz_type_init(void)
{
    type_register_static(&fuzz_accel_type);
}

type_init(fuzz_type_init);

