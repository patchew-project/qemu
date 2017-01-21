#include "qemu/osdep.h"
#include "qemu.h"
#include "exec/exec-all.h"
#include "qemu/cutils.h"

unsigned long guest_base;
int singlestep;

void mmap_lock(void)
{
    abort();
}

void mmap_unlock(void)
{
    abort();
}

bool qemu_cpu_is_self(CPUState *cpu)
{
    abort();
}

void qemu_cpu_kick(CPUState *cpu)
{
    abort();
}

#include "disas/disas.h"
#include "cpu.h"

void tb_link_page(TranslationBlock *tb, tb_page_addr_t phys_pc,
                  tb_page_addr_t phys_page2);

void test(void);
void test(void) {
  void *x = target_disas;
  x = &guest_base;
  x = &tcg_exec_init;
  x = &module_call_init;
  // cpu_init("");
#ifdef TARGET_X86_64
  X86_CPU(cpu_generic_init(TYPE_X86_CPU, ""));
#endif
  x = &cpu_reset;
  x = &qemu_set_log;
  x = &g_hash_table_foreach;
  x = &get_page_addr_code;
  x = &tcg_func_start;
  x = &gen_intermediate_code;
  x = &tb_link_page;
  // x = &target_mmap;
  x = &cpu_get_tb_cpu_state;
  (void) x;
}
