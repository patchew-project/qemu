/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "qemu/qemu-print.h"
#include "exec/cpu-common.h"
#include "exec/helper-proto-common.h"
#include "accel/tcg/getpc.h"
#include "tcg/tcg-print.h"

#define HELPER_H  "accel/tcg/tcg-runtime.h"
#include "exec/helper-info.c.inc"
#undef  HELPER_H

/* 32-bit helpers */

int32_t HELPER(div_i32)(int32_t arg1, int32_t arg2)
{
    return arg1 / arg2;
}

int32_t HELPER(rem_i32)(int32_t arg1, int32_t arg2)
{
    return arg1 % arg2;
}

uint32_t HELPER(divu_i32)(uint32_t arg1, uint32_t arg2)
{
    return arg1 / arg2;
}

uint32_t HELPER(remu_i32)(uint32_t arg1, uint32_t arg2)
{
    return arg1 % arg2;
}

/* 64-bit helpers */

uint64_t HELPER(shl_i64)(uint64_t arg1, uint64_t arg2)
{
    return arg1 << arg2;
}

uint64_t HELPER(shr_i64)(uint64_t arg1, uint64_t arg2)
{
    return arg1 >> arg2;
}

int64_t HELPER(sar_i64)(int64_t arg1, int64_t arg2)
{
    return arg1 >> arg2;
}

int64_t HELPER(div_i64)(int64_t arg1, int64_t arg2)
{
    return arg1 / arg2;
}

int64_t HELPER(rem_i64)(int64_t arg1, int64_t arg2)
{
    return arg1 % arg2;
}

uint64_t HELPER(divu_i64)(uint64_t arg1, uint64_t arg2)
{
    return arg1 / arg2;
}

uint64_t HELPER(remu_i64)(uint64_t arg1, uint64_t arg2)
{
    return arg1 % arg2;
}

uint64_t HELPER(muluh_i64)(uint64_t arg1, uint64_t arg2)
{
    uint64_t l, h;
    mulu64(&l, &h, arg1, arg2);
    return h;
}

int64_t HELPER(mulsh_i64)(int64_t arg1, int64_t arg2)
{
    uint64_t l, h;
    muls64(&l, &h, arg1, arg2);
    return h;
}

uint32_t HELPER(clz_i32)(uint32_t arg, uint32_t zero_val)
{
    return arg ? clz32(arg) : zero_val;
}

uint32_t HELPER(ctz_i32)(uint32_t arg, uint32_t zero_val)
{
    return arg ? ctz32(arg) : zero_val;
}

uint64_t HELPER(clz_i64)(uint64_t arg, uint64_t zero_val)
{
    return arg ? clz64(arg) : zero_val;
}

uint64_t HELPER(ctz_i64)(uint64_t arg, uint64_t zero_val)
{
    return arg ? ctz64(arg) : zero_val;
}

uint32_t HELPER(clrsb_i32)(uint32_t arg)
{
    return clrsb32(arg);
}

uint64_t HELPER(clrsb_i64)(uint64_t arg)
{
    return clrsb64(arg);
}

uint32_t HELPER(ctpop_i32)(uint32_t arg)
{
    return ctpop32(arg);
}

uint64_t HELPER(ctpop_i64)(uint64_t arg)
{
    return ctpop64(arg);
}

void HELPER(exit_atomic)(CPUArchState *env)
{
    cpu_loop_exit_atomic(env_cpu(env), GETPC());
}

static void tcg_print_skip_format(const char **pfmt)
{
    const char *p = *pfmt;

    while (*p) {
        char c = *p;

        if (c == 'l' || c == 'h' || c == 'z' || c == 't' ||
            c == 'j' || c == '*') {
            p++;
            continue;
        }
        if (strchr("diuoxXp", c) || c == '%') {
            p++;
            break;
        }
        p++;
    }
    *pfmt = p;
}

static bool tcg_print_emit_arg(GString *out, const char **pfmt,
                               TCGPrintArgType type, uint64_t value)
{
    const char *p = *pfmt;
    char prefix[32];
    size_t prelen = 0;

    if (*p == '\0') {
        tcg_print_skip_format(pfmt);
        return false;
    }

    prefix[prelen++] = '%';
    while (*p) {
        char c = *p;

        if (c == '*') {
            tcg_print_skip_format(pfmt);
            return false;
        }
        if (c == 'l' || c == 'h' || c == 'z' || c == 't' || c == 'j') {
            p++;
            continue;
        }
        if (strchr("diuoxXp", c)) {
            char fmtbuf[64];
            size_t len;

            if (prelen >= sizeof(fmtbuf)) {
                tcg_print_skip_format(pfmt);
                return false;
            }
            memcpy(fmtbuf, prefix, prelen);
            len = prelen;
            if (c != 'p' &&
                (type == TCG_PRINT_ARG_I64 ||
                (type == TCG_PRINT_ARG_PTR &&
                 sizeof(uintptr_t) == 8))) {
                if (len + 2 >= sizeof(fmtbuf)) {
                    tcg_print_skip_format(pfmt);
                    return false;
                }
                fmtbuf[len++] = 'l';
                fmtbuf[len++] = 'l';
            }
            if (len + 1 >= sizeof(fmtbuf)) {
                tcg_print_skip_format(pfmt);
                return false;
            }
            fmtbuf[len++] = c;
            fmtbuf[len] = '\0';

            char tmp[128];
            bool ok = true;

            switch (c) {
            case 'd':
            case 'i':
                if (type == TCG_PRINT_ARG_I64 ||
                    (type == TCG_PRINT_ARG_PTR &&
                     sizeof(uintptr_t) == 8)) {
                    g_snprintf(tmp, sizeof(tmp), fmtbuf,
                               (long long)(int64_t)value);
                } else if (type == TCG_PRINT_ARG_I32 ||
                           (type == TCG_PRINT_ARG_PTR &&
                            sizeof(uintptr_t) == 4)) {
                    g_snprintf(tmp, sizeof(tmp), fmtbuf,
                               (int)(int32_t)value);
                } else {
                    ok = false;
                }
                break;
            case 'u':
            case 'o':
            case 'x':
            case 'X':
                if (type == TCG_PRINT_ARG_I64 ||
                    (type == TCG_PRINT_ARG_PTR &&
                     sizeof(uintptr_t) == 8)) {
                    g_snprintf(tmp, sizeof(tmp), fmtbuf,
                               (unsigned long long)value);
                } else if (type == TCG_PRINT_ARG_I32 ||
                           (type == TCG_PRINT_ARG_PTR &&
                            sizeof(uintptr_t) == 4)) {
                    g_snprintf(tmp, sizeof(tmp), fmtbuf,
                               (unsigned int)(uint32_t)value);
                } else {
                    ok = false;
                }
                break;
            case 'p':
                g_snprintf(tmp, sizeof(tmp), fmtbuf,
                           (void *)(uintptr_t)value);
                break;
            default:
                ok = false;
                break;
            }

            if (!ok) {
                tcg_print_skip_format(pfmt);
                return false;
            }
            g_string_append(out, tmp);
            *pfmt = p + 1;
            return true;
        }
        if (prelen + 1 >= sizeof(prefix)) {
            tcg_print_skip_format(pfmt);
            return false;
        }
        prefix[prelen++] = c;
        p++;
    }
    tcg_print_skip_format(pfmt);
    return false;
}

void HELPER(tcg_print)(void *fmt_ptr, uint32_t desc,
                       uint64_t v0, uint64_t v1,
                       uint64_t v2, uint64_t v3,
                       uint64_t v4)
{
    const char *fmt = fmt_ptr;
    uint64_t values[TCG_PRINT_MAX_ARGS] = { v0, v1, v2, v3, v4 };
    TCGPrintArgType types[TCG_PRINT_MAX_ARGS];
    GString *msg = g_string_new(NULL);
    unsigned count = tcg_print_desc_count(desc);
    unsigned i;
    unsigned arg_index = 0;

    g_assert(count <= TCG_PRINT_MAX_ARGS);

    for (i = 0; i < count && i < TCG_PRINT_MAX_ARGS; i++) {
        types[i] = tcg_print_desc_type(desc, i);
    }

    while (*fmt) {
        if (*fmt != '%') {
            g_string_append_c(msg, *fmt++);
            continue;
        }
        fmt++;
        if (*fmt == '%') {
            g_string_append_c(msg, '%');
            fmt++;
            continue;
        }
        if (arg_index >= count) {
            tcg_print_skip_format(&fmt);
            g_string_append(msg, "<missing>");
            continue;
        }
        if (!tcg_print_emit_arg(msg, &fmt, types[arg_index],
                                 values[arg_index])) {
            g_string_append(msg, "<fmt?>");
        }
        arg_index++;
    }

    qemu_printf("%s", msg->str);
    g_string_free(msg, TRUE);
}
