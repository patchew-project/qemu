

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

uint64_t total_io_mem = 0;
uint64_t total_ram_mem = 0;


//TODO: Put arguments in a neater struct
void fuzz_add_qos_target(const char* name,
		const char* description,
		const char* interface,
		QOSGraphTestOptions* opts,
		void(*init_pre_main)(void),
		void(*init_pre_save)(void),
		void(*save_state)(void),
		void(*reset)(void),
		void(*pre_fuzz)(void),
		void(*fuzz)(const unsigned char*, size_t),
		void(*post_fuzz)(void))
{
	qos_add_test(name, interface, NULL, opts);
	fuzz_add_target(name, description, init_pre_main, init_pre_save,
			save_state, reset, pre_fuzz, fuzz, post_fuzz, &qos_argc, &qos_argv);
}


// Do what is normally done in qos_test.c:main
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
