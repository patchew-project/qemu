#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/gdbstub.h"
#include "qemu-common.h"
#include "qemu/host-utils.h"

#include "core-dsp3400/core-isa.h"
#include "core-dsp3400/core-matmap.h"
#include "overlay_tool.h"

#define xtensa_modules xtensa_modules_dsp3400
#include "core-dsp3400/xtensa-modules.inc.c"

static XtensaConfig dsp3400 __attribute__((unused)) = {
    .name = "dsp3400",
    .gdb_regmap = {
        .reg = {
#include "core-dsp3400/gdb-config.inc.c"
        }
    },
    .isa_internal = &xtensa_modules,
    .clock_freq_khz = 40000,
    .opcode_translators = (const XtensaOpcodeTranslators *[]){
        &xtensa_core_opcodes,
        &xtensa_fpu2000_opcodes,
        NULL,
    },
    DEFAULT_SECTIONS
};

REGISTER_CORE(dsp3400)
