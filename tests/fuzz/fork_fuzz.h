#ifndef FORK_FUZZ_H
#define FORK_FUZZ_H

extern uint8_t __FUZZ_COUNTERS_START;
extern uint8_t __FUZZ_COUNTERS_END;

void counter_shm_init(void);
void counter_shm_store(void);
void counter_shm_load(void);

#endif

