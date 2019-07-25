#ifndef FUZZER_H_
#define FUZZER_H_

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "tests/libqtest.h"
#include "migration/qemu-file.h"
#include "ramfile.h"

#include <linux/userfaultfd.h>


extern QTestState *s;
extern QEMUFile *writefile;
extern QEMUFile *ramfile;
extern ram_disk *rd;

typedef struct FuzzTarget {
	GString* name;
	GString* description;
	void(*init_pre_main)(void);
	void(*init_pre_save)(void);
	void(*save_state)(void);
	void(*reset)(void);
	void(*pre_fuzz)(void);
	void(*fuzz)(const unsigned char*, size_t);
	void(*post_fuzz)(void);
	int* main_argc;
	char*** main_argv;
	QSLIST_ENTRY(FuzzTarget) target_list;

} FuzzTarget;

extern void* _ZN6fuzzer3TPCE;
/* extern uint8_t __sancov_trace_pc_guard_8bit_counters; */
/* extern uint8_t __sancov_trace_pc_pcs; */
extern void* __prof_nms_sect_data;
extern void* __prof_vnodes_sect_data;

#define TPC_SIZE 0x0443c00
#define PROFILE_SIZE ( &__prof_vnodes_sect_data - &__prof_nms_sect_data)

#define NUMPCS (1 << 21)
/* #define TPC_SIZE 0x33c00 */

extern uint8_t *TPCCopy;
extern uint8_t *ARGCopy;

void save_device_state(void);
void save_vm_state(void);
void reboot(void);

void load_device_state(void);
void load_vm_state(void);


void save_device_state(void);
void qtest_setup(void);
void fuzz_register_mr(const MemoryRegion *mr);

FuzzTarget* fuzz_get_target(char* name);

extern FuzzTarget* fuzz_target;

typedef struct fuzz_memory_region {
	bool io;
	uint64_t start;
	uint64_t length;
	struct fuzz_memory_region* next;
} fuzz_memory_region;

extern fuzz_memory_region *fuzz_memory_region_head;
extern fuzz_memory_region *fuzz_memory_region_tail;

extern uint64_t total_io_mem;
extern uint64_t total_ram_mem;

void fuzz_add_target(const char* name,
	const char* description,
	void(*init_pre_main)(void),
	void(*init_pre_save)(void),
	void(*save_state)(void),
	void(*reset)(void),
	void(*pre_fuzz)(void),
	void(*fuzz)(const unsigned char*, size_t),
	void(*post_fuzz)(void),
	int* main_argc,
	char*** main_argv);

int LLVMFuzzerTestOneInput(const unsigned char *Data, size_t Size);
int LLVMFuzzerInitialize(int *argc, char ***argv, char ***envp);

#endif

