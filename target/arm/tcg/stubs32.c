/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "target/arm/tcg/translate.h"


void gen_a64_update_pc(DisasContext *s, int64_t diff)
{
    g_assert_not_reached();
}

void a64_translate_init(void)
{
    /* Don't initialize for 32 bits. Call site will be fixed later. */
}

const TranslatorOps aarch64_translator_ops;
