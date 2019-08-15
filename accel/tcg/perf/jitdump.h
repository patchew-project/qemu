#ifdef __linux__
#ifndef JITDUMP_H
#define JITDUMP_H

#include "qemu/osdep.h"

#include "disas/disas.h"
#include "exec/exec-all.h"

void start_jitdump_file(void);

void append_load_in_jitdump_file(TranslationBlock *tb);
void append_move_in_jitdump_file(TranslationBlock *tb);

void close_jitdump_file(void);

#endif

#endif
