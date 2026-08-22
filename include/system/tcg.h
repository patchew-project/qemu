/*
 * QEMU TCG support
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/* header to be included in non-TCG-specific code */

#ifndef SYSTEM_TCG_H
#define SYSTEM_TCG_H

#ifdef CONFIG_TCG
extern bool tcg_allowed;
#define tcg_enabled() (tcg_allowed)
#else
#define tcg_enabled() 0
#endif

/*
 * Recompute the parts of CPUState::tcg_cflags that TB dispatch consumes but
 * tcg_cflags_set() does not provide: gdb single-step, one-insn-per-tb and
 * the CPU_LOG_TB_NOCHAIN log flag.  Call whenever one of those changes.
 *
 * tcg_update_cflags() updates one CPU and must be called from that CPU's
 * thread, or with it stopped.  tcg_update_all_cflags() updates every CPU
 * and is safe to call from the monitor while the vCPUs run.
 */
void tcg_update_cflags(CPUState *cpu);
void tcg_update_all_cflags(void);

/*
 * Force @cpu's generated code back into the slow dispatch path, which
 * re-checks everything the inline jump cache probe assumes.  Safe to call
 * from any thread, and a no-op for a CPU that is not running TCG.  Call
 * whenever something the probe cannot see changes under a running vCPU;
 * the main loop undoes it once the reason is gone.
 */
void tcg_cpu_poison_jmp_cache(CPUState *cpu);

/**
 * qemu_tcg_mttcg_enabled:
 * Check whether we are running MultiThread TCG or not.
 *
 * Returns: %true if we are in MTTCG mode %false otherwise.
 */
bool qemu_tcg_mttcg_enabled(void);

#endif
