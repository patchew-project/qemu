#ifndef INSTRUMENT_H
#define INSTRUMENT_H

bool plugins_need_before_insn(target_ulong pc, CPUState *cpu);
void plugins_instrument_before_insn(target_ulong pc, CPUState *cpu);

#endif /* INSTRUMENT_H */
