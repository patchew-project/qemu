#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/gdbstub.h"
#include "qemu-common.h"
#include "qemu/host-utils.h"

#include "core-DE_233L_FPU/core-isa.h"
#include "core-DE_233L_FPU/core-matmap.h"
#include "overlay_tool.h"

#define xtensa_modules xtensa_modules_DE_233L_FPU
#include "core-DE_233L_FPU/xtensa-modules.inc.c"

static XtensaConfig DE_233L_FPU __attribute__((unused)) = {
    .name = "DE_233L_FPU",
    .gdb_regmap = {
        .reg = {
#include "core-DE_233L_FPU/gdb-config.inc.c"
        }
    },
    .isa_internal = &xtensa_modules,
    .clock_freq_khz = 40000,
    .opcode_translators = (const XtensaOpcodeTranslators *[]){
        &xtensa_core_opcodes,
        &xtensa_fpu_opcodes,
        NULL,
    },
    DEFAULT_SECTIONS
};

REGISTER_CORE(DE_233L_FPU)
