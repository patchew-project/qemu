

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "sysemu/sysemu.h"
#include "qemu/main-loop.h"

#include "libqos/malloc.h"
#include "libqos/qgraph.h"
#include "libqos/qgraph_internal.h"

#include "hw/virtio/virtio-net.h"
#include "hw/virtio/virtio.h"
#include "libqos/virtio-net.h"
#include "fuzz.h"
#include "qos_fuzz.h"
#include "qos_helpers.h"
#include "tests/libqos/qgraph.h"
#include "tests/libqtest.h"


fuzz_memory_region *fuzz_memory_region_head;
fuzz_memory_region *fuzz_memory_region_tail;

uint64_t total_io_mem;
uint64_t total_ram_mem;


void fuzz_add_qos_target(const char *name,
        const char *description,
        const char *interface,
        QOSGraphTestOptions *opts,
        FuzzTarget *fuzz_opts
        )
{
    qos_add_test(name, interface, NULL, opts);
    fuzz_opts->main_argc = &qos_argc;
    fuzz_opts->main_argv = &qos_argv;
    fuzz_add_target(name, description, fuzz_opts);
}


/* Do what is normally done in qos_test.c:main */
void qos_setup(void){
    qtest_setup();
    qos_set_machines_devices_available();
    qos_graph_foreach_test_path(walk_path);
    qos_build_main_args();
}

void qos_init_path(void)
{
    qos_obj = qos_allocate_objects(global_qtest, &qos_alloc);
}
