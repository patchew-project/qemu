/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch LASX helper functions.
 *
 * Copyright (c) 2023 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "internals.h"
#include "vec.h"

#define XDO_ODD_EVEN(NAME, BIT, E1, E2, DO_OP)                       \
void HELPER(NAME)(CPULoongArchState *env,                            \
                  uint32_t xd, uint32_t xj, uint32_t xk)             \
{                                                                    \
    int i;                                                           \
    XReg *Xd = &(env->fpr[xd].xreg);                                 \
    XReg *Xj = &(env->fpr[xj].xreg);                                 \
    XReg *Xk = &(env->fpr[xk].xreg);                                 \
    typedef __typeof(Xd->E1(0)) TD;                                  \
                                                                     \
    for (i = 0; i < LASX_LEN / BIT; i++) {                           \
        Xd->E1(i) = DO_OP((TD)Xj->E2(2 * i + 1), (TD)Xk->E2(2 * i)); \
    }                                                                \
}

XDO_ODD_EVEN(xvhaddw_h_b, 16, XH, XB, DO_ADD)
XDO_ODD_EVEN(xvhaddw_w_h, 32, XW, XH, DO_ADD)
XDO_ODD_EVEN(xvhaddw_d_w, 64, XD, XW, DO_ADD)

void HELPER(xvhaddw_q_d)(CPULoongArchState *env,
                         uint32_t xd, uint32_t xj, uint32_t xk)
{
    XReg *Xd = &(env->fpr[xd].xreg);
    XReg *Xj = &(env->fpr[xj].xreg);
    XReg *Xk = &(env->fpr[xk].xreg);

    Xd->XQ(0) = int128_add(int128_makes64(Xj->XD(1)),
                           int128_makes64(Xk->XD(0)));
    Xd->XQ(1) = int128_add(int128_makes64(Xj->XD(3)),
                           int128_makes64(Xk->XD(2)));
}

XDO_ODD_EVEN(xvhsubw_h_b, 16, XH, XB, DO_SUB)
XDO_ODD_EVEN(xvhsubw_w_h, 32, XW, XH, DO_SUB)
XDO_ODD_EVEN(xvhsubw_d_w, 64, XD, XW, DO_SUB)

void HELPER(xvhsubw_q_d)(CPULoongArchState *env,
                         uint32_t xd, uint32_t xj, uint32_t xk)
{
    XReg *Xd = &(env->fpr[xd].xreg);
    XReg *Xj = &(env->fpr[xj].xreg);
    XReg *Xk = &(env->fpr[xk].xreg);

    Xd->XQ(0) = int128_sub(int128_makes64(Xj->XD(1)),
                           int128_makes64(Xk->XD(0)));
    Xd->XQ(1) = int128_sub(int128_makes64(Xj->XD(3)),
                           int128_makes64(Xk->XD(2)));
}

XDO_ODD_EVEN(xvhaddw_hu_bu, 16, UXH, UXB, DO_ADD)
XDO_ODD_EVEN(xvhaddw_wu_hu, 32, UXW, UXH, DO_ADD)
XDO_ODD_EVEN(xvhaddw_du_wu, 64, UXD, UXW, DO_ADD)

void HELPER(xvhaddw_qu_du)(CPULoongArchState *env,
                           uint32_t xd, uint32_t xj, uint32_t xk)
{
    XReg *Xd = &(env->fpr[xd].xreg);
    XReg *Xj = &(env->fpr[xj].xreg);
    XReg *Xk = &(env->fpr[xk].xreg);

    Xd->XQ(0) = int128_add(int128_make64(Xj->UXD(1)),
                           int128_make64(Xk->UXD(0)));
    Xd->XQ(1) = int128_add(int128_make64(Xj->UXD(3)),
                           int128_make64(Xk->UXD(2)));
}

XDO_ODD_EVEN(xvhsubw_hu_bu, 16, UXH, UXB, DO_SUB)
XDO_ODD_EVEN(xvhsubw_wu_hu, 32, UXW, UXH, DO_SUB)
XDO_ODD_EVEN(xvhsubw_du_wu, 64, UXD, UXW, DO_SUB)

void HELPER(xvhsubw_qu_du)(CPULoongArchState *env,
                           uint32_t xd, uint32_t xj, uint32_t xk)
{
    XReg *Xd = &(env->fpr[xd].xreg);
    XReg *Xj = &(env->fpr[xj].xreg);
    XReg *Xk = &(env->fpr[xk].xreg);

    Xd->XQ(0) = int128_sub(int128_make64(Xj->UXD(1)),
                           int128_make64(Xk->UXD(0)));
    Xd->XQ(1) = int128_sub(int128_make64(Xj->UXD(3)),
                           int128_make64(Xk->UXD(2)));
}

#define XDO_EVEN(NAME, BIT, E1, E2, DO_OP)                       \
void HELPER(NAME)(void *xd, void *xj, void *xk, uint32_t v)      \
{                                                                \
    int i;                                                       \
    XReg *Xd = (XReg *)xd;                                       \
    XReg *Xj = (XReg *)xj;                                       \
    XReg *Xk = (XReg *)xk;                                       \
    typedef __typeof(Xd->E1(0)) TD;                              \
    for (i = 0; i < LASX_LEN / BIT; i++) {                       \
        Xd->E1(i) = DO_OP((TD)Xj->E2(2 * i), (TD)Xk->E2(2 * i)); \
    }                                                            \
}

#define XDO_ODD(NAME, BIT, E1, E2, DO_OP)                                \
void HELPER(NAME)(void *xd, void *xj, void *xk, uint32_t v)              \
{                                                                        \
    int i;                                                               \
    XReg *Xd = (XReg *)xd;                                               \
    XReg *Xj = (XReg *)xj;                                               \
    XReg *Xk = (XReg *)xk;                                               \
    typedef __typeof(Xd->E1(0)) TD;                                      \
    for (i = 0; i < LASX_LEN / BIT; i++) {                               \
        Xd->E1(i) = DO_OP((TD)Xj->E2(2 * i + 1), (TD)Xk->E2(2 * i + 1)); \
    }                                                                    \
}

void HELPER(xvaddwev_q_d)(void *xd, void *xj, void *xk, uint32_t v)
{
    XReg *Xd = (XReg *)xd;
    XReg *Xj = (XReg *)xj;
    XReg *Xk = (XReg *)xk;

    Xd->XQ(0) = int128_add(int128_makes64(Xj->XD(0)),
                           int128_makes64(Xk->XD(0)));
    Xd->XQ(1) = int128_add(int128_makes64(Xj->XD(2)),
                           int128_makes64(Xk->XD(2)));
}

XDO_EVEN(xvaddwev_h_b, 16, XH, XB, DO_ADD)
XDO_EVEN(xvaddwev_w_h, 32, XW, XH, DO_ADD)
XDO_EVEN(xvaddwev_d_w, 64, XD, XW, DO_ADD)

void HELPER(xvaddwod_q_d)(void *xd, void *xj, void *xk, uint32_t v)
{
    XReg *Xd = (XReg *)xd;
    XReg *Xj = (XReg *)xj;
    XReg *Xk = (XReg *)xk;

    Xd->XQ(0) = int128_add(int128_makes64(Xj->XD(1)),
                           int128_makes64(Xk->XD(1)));
    Xd->XQ(1) = int128_add(int128_makes64(Xj->XD(3)),
                           int128_makes64(Xk->XD(3)));
}

XDO_ODD(xvaddwod_h_b, 16, XH, XB, DO_ADD)
XDO_ODD(xvaddwod_w_h, 32, XW, XH, DO_ADD)
XDO_ODD(xvaddwod_d_w, 64, XD, XW, DO_ADD)

void HELPER(xvsubwev_q_d)(void *xd, void *xj, void *xk, uint32_t v)
{
    XReg *Xd = (XReg *)xd;
    XReg *Xj = (XReg *)xj;
    XReg *Xk = (XReg *)xk;

    Xd->XQ(0) = int128_sub(int128_makes64(Xj->XD(0)),
                           int128_makes64(Xk->XD(0)));
    Xd->XQ(1) = int128_sub(int128_makes64(Xj->XD(2)),
                           int128_makes64(Xk->XD(2)));
}

XDO_EVEN(xvsubwev_h_b, 16, XH, XB, DO_SUB)
XDO_EVEN(xvsubwev_w_h, 32, XW, XH, DO_SUB)
XDO_EVEN(xvsubwev_d_w, 64, XD, XW, DO_SUB)

void HELPER(xvsubwod_q_d)(void *xd, void *xj, void *xk, uint32_t v)
{
    XReg *Xd = (XReg *)xd;
    XReg *Xj = (XReg *)xj;
    XReg *Xk = (XReg *)xk;

    Xd->XQ(0) = int128_sub(int128_makes64(Xj->XD(1)),
                           int128_makes64(Xk->XD(1)));
    Xd->XQ(1) = int128_sub(int128_makes64(Xj->XD(3)),
                           int128_makes64(Xk->XD(3)));
}

XDO_ODD(xvsubwod_h_b, 16, XH, XB, DO_SUB)
XDO_ODD(xvsubwod_w_h, 32, XW, XH, DO_SUB)
XDO_ODD(xvsubwod_d_w, 64, XD, XW, DO_SUB)

void HELPER(xvaddwev_q_du)(void *xd, void *xj, void *xk, uint32_t v)
{
    XReg *Xd = (XReg *)xd;
    XReg *Xj = (XReg *)xj;
    XReg *Xk = (XReg *)xk;

    Xd->XQ(0) = int128_add(int128_make64(Xj->UXD(0)),
                           int128_make64(Xk->UXD(0)));
    Xd->XQ(1) = int128_add(int128_make64(Xj->UXD(2)),
                           int128_make64(Xk->UXD(2)));
}

XDO_EVEN(xvaddwev_h_bu, 16, UXH, UXB, DO_ADD)
XDO_EVEN(xvaddwev_w_hu, 32, UXW, UXH, DO_ADD)
XDO_EVEN(xvaddwev_d_wu, 64, UXD, UXW, DO_ADD)

void HELPER(xvaddwod_q_du)(void *xd, void *xj, void *xk, uint32_t v)
{
    XReg *Xd = (XReg *)xd;
    XReg *Xj = (XReg *)xj;
    XReg *Xk = (XReg *)xk;

    Xd->XQ(0) = int128_add(int128_make64(Xj->UXD(1)),
                           int128_make64(Xk->UXD(1)));
    Xd->XQ(1) = int128_add(int128_make64(Xj->UXD(3)),
                           int128_make64(Xk->UXD(3)));
}

XDO_ODD(xvaddwod_h_bu, 16, UXH, UXB, DO_ADD)
XDO_ODD(xvaddwod_w_hu, 32, UXW, UXH, DO_ADD)
XDO_ODD(xvaddwod_d_wu, 64, UXD, UXW, DO_ADD)

void HELPER(xvsubwev_q_du)(void *xd, void *xj, void *xk, uint32_t v)
{
    XReg *Xd = (XReg *)xd;
    XReg *Xj = (XReg *)xj;
    XReg *Xk = (XReg *)xk;

    Xd->XQ(0) = int128_sub(int128_make64(Xj->UXD(0)),
                           int128_make64(Xk->UXD(0)));
    Xd->XQ(1) = int128_sub(int128_make64(Xj->UXD(2)),
                           int128_make64(Xk->UXD(2)));
}

XDO_EVEN(xvsubwev_h_bu, 16, UXH, UXB, DO_SUB)
XDO_EVEN(xvsubwev_w_hu, 32, UXW, UXH, DO_SUB)
XDO_EVEN(xvsubwev_d_wu, 64, UXD, UXW, DO_SUB)

void HELPER(xvsubwod_q_du)(void *xd, void *xj, void *xk, uint32_t v)
{
    XReg *Xd = (XReg *)xd;
    XReg *Xj = (XReg *)xj;
    XReg *Xk = (XReg *)xk;

    Xd->XQ(0) = int128_sub(int128_make64(Xj->UXD(1)),
                           int128_make64(Xk->UXD(1)));
    Xd->XQ(1) = int128_sub(int128_make64(Xj->UXD(3)),
                           int128_make64(Xk->UXD(3)));
}

XDO_ODD(xvsubwod_h_bu, 16, UXH, UXB, DO_SUB)
XDO_ODD(xvsubwod_w_hu, 32, UXW, UXH, DO_SUB)
XDO_ODD(xvsubwod_d_wu, 64, UXD, UXW, DO_SUB)

#define XDO_EVEN_U_S(NAME, BIT, ES1, EU1, ES2, EU2, DO_OP)            \
void HELPER(NAME)(void *xd, void *xj, void *xk, uint32_t v)           \
{                                                                     \
    int i;                                                            \
    XReg *Xd = (XReg *)xd;                                            \
    XReg *Xj = (XReg *)xj;                                            \
    XReg *Xk = (XReg *)xk;                                            \
    typedef __typeof(Xd->ES1(0)) TDS;                                 \
    typedef __typeof(Xd->EU1(0)) TDU;                                 \
    for (i = 0; i < LASX_LEN / BIT; i++) {                            \
        Xd->ES1(i) = DO_OP((TDU)Xj->EU2(2 * i), (TDS)Xk->ES2(2 * i)); \
    }                                                                 \
}

#define XDO_ODD_U_S(NAME, BIT, ES1, EU1, ES2, EU2, DO_OP)                     \
void HELPER(NAME)(void *xd, void *xj, void *xk, uint32_t v)                   \
{                                                                             \
    int i;                                                                    \
    XReg *Xd = (XReg *)xd;                                                    \
    XReg *Xj = (XReg *)xj;                                                    \
    XReg *Xk = (XReg *)xk;                                                    \
    typedef __typeof(Xd->ES1(0)) TDS;                                         \
    typedef __typeof(Xd->EU1(0)) TDU;                                         \
    for (i = 0; i < LSX_LEN / BIT; i++) {                                     \
        Xd->ES1(i) = DO_OP((TDU)Xj->EU2(2 * i + 1), (TDS)Xk->ES2(2 * i + 1)); \
    }                                                                         \
}

void HELPER(xvaddwev_q_du_d)(void *xd, void *xj, void *xk, uint32_t v)
{
    XReg *Xd = (XReg *)xd;
    XReg *Xj = (XReg *)xj;
    XReg *Xk = (XReg *)xk;

    Xd->XQ(0) = int128_add(int128_make64(Xj->UXD(0)),
                           int128_makes64(Xk->XD(0)));
    Xd->XQ(1) = int128_add(int128_make64(Xj->UXD(2)),
                           int128_makes64(Xk->XD(2)));
}

XDO_EVEN_U_S(xvaddwev_h_bu_b, 16, XH, UXH, XB, UXB, DO_ADD)
XDO_EVEN_U_S(xvaddwev_w_hu_h, 32, XW, UXW, XH, UXH, DO_ADD)
XDO_EVEN_U_S(xvaddwev_d_wu_w, 64, XD, UXD, XW, UXW, DO_ADD)

void HELPER(xvaddwod_q_du_d)(void *xd, void *xj, void *xk, uint32_t v)
{
    XReg *Xd = (XReg *)xd;
    XReg *Xj = (XReg *)xj;
    XReg *Xk = (XReg *)xk;

    Xd->XQ(0) = int128_add(int128_make64(Xj->UXD(1)),
                           int128_makes64(Xk->XD(1)));
    Xd->XQ(1) = int128_add(int128_make64(Xj->UXD(3)),
                           int128_makes64(Xk->XD(3)));
}

XDO_ODD_U_S(xvaddwod_h_bu_b, 16, XH, UXH, XB, UXB, DO_ADD)
XDO_ODD_U_S(xvaddwod_w_hu_h, 32, XW, UXW, XH, UXH, DO_ADD)
XDO_ODD_U_S(xvaddwod_d_wu_w, 64, XD, UXD, XW, UXW, DO_ADD)

#define XDO_3OP(NAME, BIT, E, DO_OP)                        \
void HELPER(NAME)(void *xd, void *xj, void *xk, uint32_t v) \
{                                                           \
    int i;                                                  \
    XReg *Xd = (XReg *)xd;                                  \
    XReg *Xj = (XReg *)xj;                                  \
    XReg *Xk = (XReg *)xk;                                  \
    for (i = 0; i < LASX_LEN / BIT; i++) {                  \
        Xd->E(i) = DO_OP(Xj->E(i), Xk->E(i));               \
    }                                                       \
}

XDO_3OP(xvavg_b, 8, XB, DO_VAVG)
XDO_3OP(xvavg_h, 16, XH, DO_VAVG)
XDO_3OP(xvavg_w, 32, XW, DO_VAVG)
XDO_3OP(xvavg_d, 64, XD, DO_VAVG)
XDO_3OP(xvavgr_b, 8, XB, DO_VAVGR)
XDO_3OP(xvavgr_h, 16, XH, DO_VAVGR)
XDO_3OP(xvavgr_w, 32, XW, DO_VAVGR)
XDO_3OP(xvavgr_d, 64, XD, DO_VAVGR)
XDO_3OP(xvavg_bu, 8, UXB, DO_VAVG)
XDO_3OP(xvavg_hu, 16, UXH, DO_VAVG)
XDO_3OP(xvavg_wu, 32, UXW, DO_VAVG)
XDO_3OP(xvavg_du, 64, UXD, DO_VAVG)
XDO_3OP(xvavgr_bu, 8, UXB, DO_VAVGR)
XDO_3OP(xvavgr_hu, 16, UXH, DO_VAVGR)
XDO_3OP(xvavgr_wu, 32, UXW, DO_VAVGR)
XDO_3OP(xvavgr_du, 64, UXD, DO_VAVGR)

XDO_3OP(xvabsd_b, 8, XB, DO_VABSD)
XDO_3OP(xvabsd_h, 16, XH, DO_VABSD)
XDO_3OP(xvabsd_w, 32, XW, DO_VABSD)
XDO_3OP(xvabsd_d, 64, XD, DO_VABSD)
XDO_3OP(xvabsd_bu, 8, UXB, DO_VABSD)
XDO_3OP(xvabsd_hu, 16, UXH, DO_VABSD)
XDO_3OP(xvabsd_wu, 32, UXW, DO_VABSD)
XDO_3OP(xvabsd_du, 64, UXD, DO_VABSD)

#define XDO_VADDA(NAME, BIT, E, DO_OP)                      \
void HELPER(NAME)(void *xd, void *xj, void *xk, uint32_t v) \
{                                                           \
    int i;                                                  \
    XReg *Xd = (XReg *)xd;                                  \
    XReg *Xj = (XReg *)xj;                                  \
    XReg *Xk = (XReg *)xk;                                  \
    for (i = 0; i < LASX_LEN / BIT; i++) {                  \
        Xd->E(i) = DO_OP(Xj->E(i)) + DO_OP(Xk->E(i));       \
    }                                                       \
}

XDO_VADDA(xvadda_b, 8, XB, DO_VABS)
XDO_VADDA(xvadda_h, 16, XH, DO_VABS)
XDO_VADDA(xvadda_w, 32, XW, DO_VABS)
XDO_VADDA(xvadda_d, 64, XD, DO_VABS)

#define XVMINMAXI(NAME, BIT, E, DO_OP)                          \
void HELPER(NAME)(void *xd, void *xj, uint64_t imm, uint32_t v) \
{                                                               \
    int i;                                                      \
    XReg *Xd = (XReg *)xd;                                      \
    XReg *Xj = (XReg *)xj;                                      \
    typedef __typeof(Xd->E(0)) TD;                              \
                                                                \
    for (i = 0; i < LASX_LEN / BIT; i++) {                      \
        Xd->E(i) = DO_OP(Xj->E(i), (TD)imm);                    \
    }                                                           \
}

XVMINMAXI(xvmini_b, 8, XB, DO_MIN)
XVMINMAXI(xvmini_h, 16, XH, DO_MIN)
XVMINMAXI(xvmini_w, 32, XW, DO_MIN)
XVMINMAXI(xvmini_d, 64, XD, DO_MIN)
XVMINMAXI(xvmaxi_b, 8, XB, DO_MAX)
XVMINMAXI(xvmaxi_h, 16, XH, DO_MAX)
XVMINMAXI(xvmaxi_w, 32, XW, DO_MAX)
XVMINMAXI(xvmaxi_d, 64, XD, DO_MAX)
XVMINMAXI(xvmini_bu, 8, UXB, DO_MIN)
XVMINMAXI(xvmini_hu, 16, UXH, DO_MIN)
XVMINMAXI(xvmini_wu, 32, UXW, DO_MIN)
XVMINMAXI(xvmini_du, 64, UXD, DO_MIN)
XVMINMAXI(xvmaxi_bu, 8, UXB, DO_MAX)
XVMINMAXI(xvmaxi_hu, 16, UXH, DO_MAX)
XVMINMAXI(xvmaxi_wu, 32, UXW, DO_MAX)
XVMINMAXI(xvmaxi_du, 64, UXD, DO_MAX)

#define DO_XVMUH(NAME, BIT, E1, E2)                         \
void HELPER(NAME)(void *xd, void *xj, void *xk, uint32_t v) \
{                                                           \
    int i;                                                  \
    XReg *Xd = (XReg *)xd;                                  \
    XReg *Xj = (XReg *)xj;                                  \
    XReg *Xk = (XReg *)xk;                                  \
    typedef __typeof(Xd->E1(0)) T;                          \
                                                            \
    for (i = 0; i < LASX_LEN / BIT; i++) {                  \
        Xd->E2(i) = ((T)Xj->E2(i)) * ((T)Xk->E2(i)) >> BIT; \
    }                                                       \
}

void HELPER(xvmuh_d)(void *xd, void *xj, void *xk, uint32_t v)
{
    uint64_t l, h;
    XReg *Xd = (XReg *)xd;
    XReg *Xj = (XReg *)xj;
    XReg *Xk = (XReg *)xk;
    int i;

    for (i = 0; i < 4; i++) {
        muls64(&l, &h, Xj->XD(i), Xk->XD(i));
        Xd->XD(i) = h;
    }
}

DO_XVMUH(xvmuh_b, 8, XH, XB)
DO_XVMUH(xvmuh_h, 16, XW, XH)
DO_XVMUH(xvmuh_w, 32, XD, XW)

void HELPER(xvmuh_du)(void *xd, void *xj, void *xk, uint32_t v)
{
    uint64_t l, h;
    XReg *Xd = (XReg *)xd;
    XReg *Xj = (XReg *)xj;
    XReg *Xk = (XReg *)xk;
    int i;

    for (i = 0; i < 4; i++) {
        mulu64(&l, &h, Xj->XD(i), Xk->XD(i));
        Xd->XD(i) = h;
    }
}

DO_XVMUH(xvmuh_bu, 8, UXH, UXB)
DO_XVMUH(xvmuh_hu, 16, UXW, UXH)
DO_XVMUH(xvmuh_wu, 32, UXD, UXW)

XDO_EVEN(xvmulwev_h_b, 16, XH, XB, DO_MUL)
XDO_EVEN(xvmulwev_w_h, 32, XW, XH, DO_MUL)
XDO_EVEN(xvmulwev_d_w, 64, XD, XW, DO_MUL)

XDO_ODD(xvmulwod_h_b, 16, XH, XB, DO_MUL)
XDO_ODD(xvmulwod_w_h, 32, XW, XH, DO_MUL)
XDO_ODD(xvmulwod_d_w, 64, XD, XW, DO_MUL)

XDO_EVEN(xvmulwev_h_bu, 16, UXH, UXB, DO_MUL)
XDO_EVEN(xvmulwev_w_hu, 32, UXW, UXH, DO_MUL)
XDO_EVEN(xvmulwev_d_wu, 64, UXD, UXW, DO_MUL)

XDO_ODD(xvmulwod_h_bu, 16, UXH, UXB, DO_MUL)
XDO_ODD(xvmulwod_w_hu, 32, UXW, UXH, DO_MUL)
XDO_ODD(xvmulwod_d_wu, 64, UXD, UXW, DO_MUL)

XDO_EVEN_U_S(xvmulwev_h_bu_b, 16, XH, UXH, XB, UXB, DO_MUL)
XDO_EVEN_U_S(xvmulwev_w_hu_h, 32, XW, UXW, XH, UXH, DO_MUL)
XDO_EVEN_U_S(xvmulwev_d_wu_w, 64, XD, UXD, XW, UXW, DO_MUL)

XDO_ODD_U_S(xvmulwod_h_bu_b, 16, XH, UXH, XB, UXB, DO_MUL)
XDO_ODD_U_S(xvmulwod_w_hu_h, 32, XW, UXW, XH, UXH, DO_MUL)
XDO_ODD_U_S(xvmulwod_d_wu_w, 64, XD, UXD, XW, UXW, DO_MUL)

#define XVMADDSUB(NAME, BIT, E, DO_OP)                      \
void HELPER(NAME)(void *xd, void *xj, void *xk, uint32_t v) \
{                                                           \
    int i;                                                  \
    XReg *Xd = (XReg *)xd;                                  \
    XReg *Xj = (XReg *)xj;                                  \
    XReg *Xk = (XReg *)xk;                                  \
    for (i = 0; i < LASX_LEN / BIT; i++) {                  \
        Xd->E(i) = DO_OP(Xd->E(i), Xj->E(i), Xk->E(i));     \
    }                                                       \
}

XVMADDSUB(xvmadd_b, 8, XB, DO_MADD)
XVMADDSUB(xvmadd_h, 16, XH, DO_MADD)
XVMADDSUB(xvmadd_w, 32, XW, DO_MADD)
XVMADDSUB(xvmadd_d, 64, XD, DO_MADD)
XVMADDSUB(xvmsub_b, 8, XB, DO_MSUB)
XVMADDSUB(xvmsub_h, 16, XH, DO_MSUB)
XVMADDSUB(xvmsub_w, 32, XW, DO_MSUB)
XVMADDSUB(xvmsub_d, 64, XD, DO_MSUB)

#define XVMADDWEV(NAME, BIT, E1, E2, DO_OP)                       \
void HELPER(NAME)(void *xd, void *xj, void *xk, uint32_t v)       \
{                                                                 \
    int i;                                                        \
    XReg *Xd = (XReg *)xd;                                        \
    XReg *Xj = (XReg *)xj;                                        \
    XReg *Xk = (XReg *)xk;                                        \
    typedef __typeof(Xd->E1(0)) TD;                               \
                                                                  \
    for (i = 0; i < LASX_LEN / BIT; i++) {                        \
        Xd->E1(i) += DO_OP((TD)Xj->E2(2 * i), (TD)Xk->E2(2 * i)); \
    }                                                             \
}

XVMADDWEV(xvmaddwev_h_b, 16, XH, XB, DO_MUL)
XVMADDWEV(xvmaddwev_w_h, 32, XW, XH, DO_MUL)
XVMADDWEV(xvmaddwev_d_w, 64, XD, XW, DO_MUL)
XVMADDWEV(xvmaddwev_h_bu, 16, UXH, UXB, DO_MUL)
XVMADDWEV(xvmaddwev_w_hu, 32, UXW, UXH, DO_MUL)
XVMADDWEV(xvmaddwev_d_wu, 64, UXD, UXW, DO_MUL)

#define XVMADDWOD(NAME, BIT, E1, E2, DO_OP)                 \
void HELPER(NAME)(void *xd, void *xj, void *xk, uint32_t v) \
{                                                           \
    int i;                                                  \
    XReg *Xd = (XReg *)xd;                                  \
    XReg *Xj = (XReg *)xj;                                  \
    XReg *Xk = (XReg *)xk;                                  \
    typedef __typeof(Xd->E1(0)) TD;                         \
                                                            \
    for (i = 0; i < LASX_LEN / BIT; i++) {                  \
        Xd->E1(i) += DO_OP((TD)Xj->E2(2 * i + 1),           \
                           (TD)Xk->E2(2 * i + 1));          \
    }                                                       \
}

XVMADDWOD(xvmaddwod_h_b, 16, XH, XB, DO_MUL)
XVMADDWOD(xvmaddwod_w_h, 32, XW, XH, DO_MUL)
XVMADDWOD(xvmaddwod_d_w, 64, XD, XW, DO_MUL)
XVMADDWOD(xvmaddwod_h_bu, 16,  UXH, UXB, DO_MUL)
XVMADDWOD(xvmaddwod_w_hu, 32,  UXW, UXH, DO_MUL)
XVMADDWOD(xvmaddwod_d_wu, 64,  UXD, UXW, DO_MUL)

#define XVMADDWEV_U_S(NAME, BIT, ES1, EU1, ES2, EU2, DO_OP) \
void HELPER(NAME)(void *xd, void *xj, void *xk, uint32_t v) \
{                                                           \
    int i;                                                  \
    XReg *Xd = (XReg *)xd;                                  \
    XReg *Xj = (XReg *)xj;                                  \
    XReg *Xk = (XReg *)xk;                                  \
    typedef __typeof(Xd->ES1(0)) TS1;                       \
    typedef __typeof(Xd->EU1(0)) TU1;                       \
                                                            \
    for (i = 0; i < LASX_LEN / BIT; i++) {                  \
        Xd->ES1(i) += DO_OP((TU1)Xj->EU2(2 * i),            \
                            (TS1)Xk->ES2(2 * i));           \
    }                                                       \
}

XVMADDWEV_U_S(xvmaddwev_h_bu_b, 16, XH, UXH, XB, UXB, DO_MUL)
XVMADDWEV_U_S(xvmaddwev_w_hu_h, 32, XW, UXW, XH, UXH, DO_MUL)
XVMADDWEV_U_S(xvmaddwev_d_wu_w, 64, XD, UXD, XW, UXW, DO_MUL)

#define XVMADDWOD_U_S(NAME, BIT, ES1, EU1, ES2, EU2, DO_OP) \
void HELPER(NAME)(void *xd, void *xj, void *xk, uint32_t v) \
{                                                           \
    int i;                                                  \
    XReg *Xd = (XReg *)xd;                                  \
    XReg *Xj = (XReg *)xj;                                  \
    XReg *Xk = (XReg *)xk;                                  \
    typedef __typeof(Xd->ES1(0)) TS1;                       \
    typedef __typeof(Xd->EU1(0)) TU1;                       \
                                                            \
    for (i = 0; i < LASX_LEN / BIT; i++) {                  \
        Xd->ES1(i) += DO_OP((TU1)Xj->EU2(2 * i + 1),        \
                            (TS1)Xk->ES2(2 * i + 1));       \
    }                                                       \
}

XVMADDWOD_U_S(xvmaddwod_h_bu_b, 16, XH, UXH, XB, UXB, DO_MUL)
XVMADDWOD_U_S(xvmaddwod_w_hu_h, 32, XW, UXW, XH, UXH, DO_MUL)
XVMADDWOD_U_S(xvmaddwod_d_wu_w, 64, XD, UXD, XW, UXW, DO_MUL)

#define XVDIV(NAME, BIT, E, DO_OP)                          \
void HELPER(NAME)(CPULoongArchState *env,                   \
                  uint32_t xd, uint32_t xj, uint32_t xk)    \
{                                                           \
    int i;                                                  \
    XReg *Xd = &(env->fpr[xd].xreg);                        \
    XReg *Xj = &(env->fpr[xj].xreg);                        \
    XReg *Xk = &(env->fpr[xk].xreg);                        \
    for (i = 0; i < LASX_LEN / BIT; i++) {                  \
        Xd->E(i) = DO_OP(Xj->E(i), Xk->E(i));               \
    }                                                       \
}

XVDIV(xvdiv_b, 8, XB, DO_DIV)
XVDIV(xvdiv_h, 16, XH, DO_DIV)
XVDIV(xvdiv_w, 32, XW, DO_DIV)
XVDIV(xvdiv_d, 64, XD, DO_DIV)
XVDIV(xvdiv_bu, 8, UXB, DO_DIVU)
XVDIV(xvdiv_hu, 16, UXH, DO_DIVU)
XVDIV(xvdiv_wu, 32, UXW, DO_DIVU)
XVDIV(xvdiv_du, 64, UXD, DO_DIVU)
XVDIV(xvmod_b, 8, XB, DO_REM)
XVDIV(xvmod_h, 16, XH, DO_REM)
XVDIV(xvmod_w, 32, XW, DO_REM)
XVDIV(xvmod_d, 64, XD, DO_REM)
XVDIV(xvmod_bu, 8, UXB, DO_REMU)
XVDIV(xvmod_hu, 16, UXH, DO_REMU)
XVDIV(xvmod_wu, 32, UXW, DO_REMU)
XVDIV(xvmod_du, 64, UXD, DO_REMU)

#define XVSAT_S(NAME, BIT, E)                                   \
void HELPER(NAME)(void *xd, void *xj, uint64_t max, uint32_t v) \
{                                                               \
    int i;                                                      \
    XReg *Xd = (XReg *)xd;                                      \
    XReg *Xj = (XReg *)xj;                                      \
    typedef __typeof(Xd->E(0)) TD;                              \
                                                                \
    for (i = 0; i < LASX_LEN / BIT; i++) {                      \
        Xd->E(i) = Xj->E(i) > (TD)max ? (TD)max :               \
                   Xj->E(i) < (TD)~max ? (TD)~max : Xj->E(i);   \
    }                                                           \
}

XVSAT_S(xvsat_b, 8, XB)
XVSAT_S(xvsat_h, 16, XH)
XVSAT_S(xvsat_w, 32, XW)
XVSAT_S(xvsat_d, 64, XD)

#define XVSAT_U(NAME, BIT, E)                                   \
void HELPER(NAME)(void *xd, void *xj, uint64_t max, uint32_t v) \
{                                                               \
    int i;                                                      \
    XReg *Xd = (XReg *)xd;                                      \
    XReg *Xj = (XReg *)xj;                                      \
    typedef __typeof(Xd->E(0)) TD;                              \
                                                                \
    for (i = 0; i < LASX_LEN / BIT; i++) {                      \
        Xd->E(i) = Xj->E(i) > (TD)max ? (TD)max : Xj->E(i);     \
    }                                                           \
}

XVSAT_U(xvsat_bu, 8, UXB)
XVSAT_U(xvsat_hu, 16, UXH)
XVSAT_U(xvsat_wu, 32, UXW)
XVSAT_U(xvsat_du, 64, UXD)

#define XVEXTH(NAME, BIT, E1, E2)                                   \
void HELPER(NAME)(CPULoongArchState *env, uint32_t xd, uint32_t xj) \
{                                                                   \
    int i, max;                                                     \
    XReg *Xd = &(env->fpr[xd].xreg);                                \
    XReg *Xj = &(env->fpr[xj].xreg);                                \
                                                                    \
    max = LASX_LEN / (BIT * 2);                                     \
    for (i = 0; i < max; i++) {                                     \
        Xd->E1(i) = Xj->E2(i + max);                                \
        Xd->E1(i + max) = Xj->E2(i + max * 3);                      \
    }                                                               \
}

void HELPER(xvexth_q_d)(CPULoongArchState *env, uint32_t xd, uint32_t xj)
{
    XReg *Xd = &(env->fpr[xd].xreg);
    XReg *Xj = &(env->fpr[xj].xreg);

    Xd->XQ(0) = int128_makes64(Xj->XD(1));
    Xd->XQ(1) = int128_makes64(Xj->XD(3));
}

void HELPER(xvexth_qu_du)(CPULoongArchState *env, uint32_t xd, uint32_t xj)
{
    XReg *Xd = &(env->fpr[xd].xreg);
    XReg *Xj = &(env->fpr[xj].xreg);

    Xd->XQ(0) = int128_make64(Xj->UXD(1));
    Xd->XQ(1) = int128_make64(Xj->UXD(3));
}

XVEXTH(xvexth_h_b, 16, XH, XB)
XVEXTH(xvexth_w_h, 32, XW, XH)
XVEXTH(xvexth_d_w, 64, XD, XW)
XVEXTH(xvexth_hu_bu, 16, UXH, UXB)
XVEXTH(xvexth_wu_hu, 32, UXW, UXH)
XVEXTH(xvexth_du_wu, 64, UXD, UXW)

#define VEXT2XV(NAME, BIT, E1, E2)                                  \
void HELPER(NAME)(CPULoongArchState *env, uint32_t xd, uint32_t xj) \
{                                                                   \
    int i;                                                          \
    XReg *Xd = &(env->fpr[xd].xreg);                                \
    XReg *Xj = &(env->fpr[xj].xreg);                                \
    XReg temp;                                                      \
                                                                    \
    for (i = 0; i < LASX_LEN / BIT; i++) {                          \
        temp.E1(i) = Xj->E2(i);                                     \
    }                                                               \
    *Xd = temp;                                                     \
}

VEXT2XV(vext2xv_h_b, 16, XH, XB)
VEXT2XV(vext2xv_w_b, 32, XW, XB)
VEXT2XV(vext2xv_d_b, 64, XD, XB)
VEXT2XV(vext2xv_w_h, 32, XW, XH)
VEXT2XV(vext2xv_d_h, 64, XD, XH)
VEXT2XV(vext2xv_d_w, 64, XD, XW)
VEXT2XV(vext2xv_hu_bu, 16, UXH, UXB)
VEXT2XV(vext2xv_wu_bu, 32, UXW, UXB)
VEXT2XV(vext2xv_du_bu, 64, UXD, UXB)
VEXT2XV(vext2xv_wu_hu, 32, UXW, UXH)
VEXT2XV(vext2xv_du_hu, 64, UXD, UXH)
VEXT2XV(vext2xv_du_wu, 64, UXD, UXW)

XDO_3OP(xvsigncov_b, 8, XB, DO_SIGNCOV)
XDO_3OP(xvsigncov_h, 16, XH, DO_SIGNCOV)
XDO_3OP(xvsigncov_w, 32, XW, DO_SIGNCOV)
XDO_3OP(xvsigncov_d, 64, XD, DO_SIGNCOV)

void HELPER(xvmskltz_b)(CPULoongArchState *env, uint32_t xd, uint32_t xj)
{
    uint16_t temp;
    int i;
    XReg *Xd = &(env->fpr[xd].xreg);
    XReg *Xj = &(env->fpr[xj].xreg);

    for (i = 0; i < 2; i++) {
        temp = 0;
        temp = do_vmskltz_b(Xj->XD(2 * i));
        temp |= (do_vmskltz_b(Xj->XD(2 * i + 1)) << 8);
        Xd->XD(2 * i) = temp;
        Xd->XD(2 * i + 1) = 0;
    }
}

void HELPER(xvmskltz_h)(CPULoongArchState *env, uint32_t xd, uint32_t xj)
{
    uint16_t temp;
    int i;
    XReg *Xd = &(env->fpr[xd].xreg);
    XReg *Xj = &(env->fpr[xj].xreg);

    for (i = 0; i < 2; i++) {
        temp = 0;
        temp = do_vmskltz_h(Xj->XD(2 * i));
        temp |= (do_vmskltz_h(Xj->XD(2 * i + 1)) << 4);
        Xd->XD(2 * i) = temp;
        Xd->XD(2 * i + 1) = 0;
    }
}

void HELPER(xvmskltz_w)(CPULoongArchState *env, uint32_t xd, uint32_t xj)
{
    uint16_t temp;
    int i;
    XReg *Xd = &(env->fpr[xd].xreg);
    XReg *Xj = &(env->fpr[xj].xreg);

    for (i = 0; i < 2; i++) {
        temp = do_vmskltz_w(Xj->XD(2 * i));
        temp |= (do_vmskltz_w(Xj->XD(2 * i + 1)) << 2);
        Xd->XD(2 * i) = temp;
        Xd->XD(2 * i + 1) = 0;
    }
}

void HELPER(xvmskltz_d)(CPULoongArchState *env, uint32_t xd, uint32_t xj)
{
    uint16_t temp;
    int i;
    XReg *Xd = &(env->fpr[xd].xreg);
    XReg *Xj = &(env->fpr[xj].xreg);

    for (i = 0; i < 2; i++) {
        temp = 0;
        temp = do_vmskltz_d(Xj->XD(2 * i));
        temp |= (do_vmskltz_d(Xj->XD(2 * i + 1)) << 1);
        Xd->XD(2 * i) = temp;
        Xd->XD(2 * i + 1) = 0;
    }
}

void HELPER(xvmskgez_b)(CPULoongArchState *env, uint32_t xd, uint32_t xj)
{
    uint16_t temp;
    int i;
    XReg *Xd = &(env->fpr[xd].xreg);
    XReg *Xj = &(env->fpr[xj].xreg);

    for (i = 0; i < 2; i++) {
        temp = 0;
        temp =  do_vmskltz_b(Xj->XD(2 * i));
        temp |= (do_vmskltz_b(Xj->XD(2 * i + 1)) << 8);
        Xd->XD(2 * i) = (uint16_t)(~temp);
        Xd->XD(2 * i + 1) = 0;
    }
}

void HELPER(xvmsknz_b)(CPULoongArchState *env, uint32_t xd, uint32_t xj)
{
    uint16_t temp;
    int i;
    XReg *Xd = &(env->fpr[xd].xreg);
    XReg *Xj = &(env->fpr[xj].xreg);

    for (i = 0; i < 2; i++) {
        temp = 0;
        temp = do_vmskez_b(Xj->XD(2 * i));
        temp |= (do_vmskez_b(Xj->XD(2 * i + 1)) << 8);
        Xd->XD(2 * i) = (uint16_t)(~temp);
        Xd->XD(2 * i + 1) = 0;
    }
}

void HELPER(xvnori_b)(void *xd, void *xj, uint64_t imm, uint32_t v)
{
    int i;
    XReg *Xd = (XReg *)xd;
    XReg *Xj = (XReg *)xj;

    for (i = 0; i < LASX_LEN / 8; i++) {
        Xd->XB(i) = ~(Xj->XB(i) | (uint8_t)imm);
    }
}

#define XVSLLWIL(NAME, BIT, E1, E2)                                \
void HELPER(NAME)(CPULoongArchState *env,                          \
                  uint32_t xd, uint32_t xj, uint32_t imm)          \
{                                                                  \
    int i, max;                                                    \
    XReg temp;                                                     \
    XReg *Xd = &(env->fpr[xd].xreg);                               \
    XReg *Xj = &(env->fpr[xj].xreg);                               \
    typedef __typeof(temp.E1(0)) TD;                               \
                                                                   \
    temp.XQ(0) = int128_zero();                                    \
    temp.XQ(1) = int128_zero();                                    \
    max = LASX_LEN / (BIT * 2);                                    \
    for (i = 0; i < max; i++) {                                    \
        temp.E1(i) = (TD)Xj->E2(i) << (imm % BIT);                 \
        temp.E1(i + max) = (TD)Xj->E2(i + max * 2) << (imm % BIT); \
    }                                                              \
    *Xd = temp;                                                    \
}

void HELPER(xvextl_q_d)(CPULoongArchState *env, uint32_t xd, uint32_t xj)
{
    XReg *Xd = &(env->fpr[xd].xreg);
    XReg *Xj = &(env->fpr[xj].xreg);

    Xd->XQ(0) = int128_makes64(Xj->XD(0));
    Xd->XQ(1) = int128_makes64(Xj->XD(2));
}

void HELPER(xvextl_qu_du)(CPULoongArchState *env, uint32_t xd, uint32_t xj)
{
    XReg *Xd = &(env->fpr[xd].xreg);
    XReg *Xj = &(env->fpr[xj].xreg);

    Xd->XQ(0) = int128_make64(Xj->UXD(0));
    Xd->XQ(1) = int128_make64(Xj->UXD(2));
}

XVSLLWIL(xvsllwil_h_b, 16, XH, XB)
XVSLLWIL(xvsllwil_w_h, 32, XW, XH)
XVSLLWIL(xvsllwil_d_w, 64, XD, XW)
XVSLLWIL(xvsllwil_hu_bu, 16, UXH, UXB)
XVSLLWIL(xvsllwil_wu_hu, 32, UXW, UXH)
XVSLLWIL(xvsllwil_du_wu, 64, UXD, UXW)
