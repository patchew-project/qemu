#ifndef INSTRUMENT_H
#define INSTRUMENT_H

typedef struct DisasContextBase DisasContextBase;

void qi_init(void);

bool qi_needs_before_insn(DisasContextBase *db, CPUState *cpu);
void qi_instrument_before_insn(DisasContextBase *db, CPUState *cpu);

#endif // INSTRUMENT_H
