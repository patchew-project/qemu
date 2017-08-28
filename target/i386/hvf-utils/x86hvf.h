#ifndef X86HVF_H
#define X86HVF_H
#include "cpu.h"
#include "x86_descr.h"

int hvf_process_events(CPUState *);
int hvf_put_registers(CPUState *);
int hvf_get_registers(CPUState *);
void hvf_inject_interrupts(CPUState *);
void hvf_set_segment(struct CPUState *cpu, struct vmx_segment *vmx_seg, SegmentCache *qseg, bool is_tr);
void hvf_get_segment(SegmentCache *qseg, struct vmx_segment *vmx_seg);
void hvf_put_xsave(CPUState *cpu_state);
void hvf_put_segments(CPUState *cpu_state);
void hvf_put_msrs(CPUState *cpu_state);
void hvf_get_xsave(CPUState *cpu_state);
void hvf_get_msrs(CPUState *cpu_state);
void vmx_clear_int_window_exiting(CPUState *cpu);
void hvf_get_segments(CPUState *cpu_state);
#endif
