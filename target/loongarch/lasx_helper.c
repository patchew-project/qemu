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

#define do_xvsrlr(E, T)                                 \
static T do_xvsrlr_ ##E(T s1, int sh)                   \
{                                                       \
    if (sh == 0) {                                      \
        return s1;                                      \
    } else {                                            \
        return  (s1 >> sh)  + ((s1 >> (sh - 1)) & 0x1); \
    }                                                   \
}

do_xvsrlr(XB, uint8_t)
do_xvsrlr(XH, uint16_t)
do_xvsrlr(XW, uint32_t)
do_xvsrlr(XD, uint64_t)

#define XVSRLR(NAME, BIT, E1, E2)                                   \
void HELPER(NAME)(CPULoongArchState *env,                           \
                  uint32_t xd, uint32_t xj, uint32_t xk)            \
{                                                                   \
    int i;                                                          \
    XReg *Xd = &(env->fpr[xd].xreg);                                \
    XReg *Xj = &(env->fpr[xj].xreg);                                \
    XReg *Xk = &(env->fpr[xk].xreg);                                \
                                                                    \
    for (i = 0; i < LASX_LEN / BIT; i++) {                          \
        Xd->E1(i) = do_xvsrlr_ ## E1(Xj->E1(i), (Xk->E2(i)) % BIT); \
    }                                                               \
}

XVSRLR(xvsrlr_b, 8, XB, UXB)
XVSRLR(xvsrlr_h, 16, XH, UXH)
XVSRLR(xvsrlr_w, 32, XW, UXW)
XVSRLR(xvsrlr_d, 64, XD, UXD)

#define XVSRLRI(NAME, BIT, E)                             \
void HELPER(NAME)(CPULoongArchState *env,                 \
                  uint32_t xd, uint32_t xj, uint32_t imm) \
{                                                         \
    int i;                                                \
    XReg *Xd = &(env->fpr[xd].xreg);                      \
    XReg *Xj = &(env->fpr[xj].xreg);                      \
                                                          \
    for (i = 0; i < LASX_LEN / BIT; i++) {                \
        Xd->E(i) = do_xvsrlr_ ## E(Xj->E(i), imm);        \
    }                                                     \
}

XVSRLRI(xvsrlri_b, 8, XB)
XVSRLRI(xvsrlri_h, 16, XH)
XVSRLRI(xvsrlri_w, 32, XW)
XVSRLRI(xvsrlri_d, 64, XD)

#define do_xvsrar(E, T)                                 \
static T do_xvsrar_ ##E(T s1, int sh)                   \
{                                                       \
    if (sh == 0) {                                      \
        return s1;                                      \
    } else {                                            \
        return  (s1 >> sh)  + ((s1 >> (sh - 1)) & 0x1); \
    }                                                   \
}

do_xvsrar(XB, int8_t)
do_xvsrar(XH, int16_t)
do_xvsrar(XW, int32_t)
do_xvsrar(XD, int64_t)

#define XVSRAR(NAME, BIT, E1, E2)                                   \
void HELPER(NAME)(CPULoongArchState *env,                           \
                  uint32_t xd, uint32_t xj, uint32_t xk)            \
{                                                                   \
    int i;                                                          \
    XReg *Xd = &(env->fpr[xd].xreg);                                \
    XReg *Xj = &(env->fpr[xj].xreg);                                \
    XReg *Xk = &(env->fpr[xk].xreg);                                \
                                                                    \
    for (i = 0; i < LASX_LEN / BIT; i++) {                          \
        Xd->E1(i) = do_xvsrar_ ## E1(Xj->E1(i), (Xk->E2(i)) % BIT); \
    }                                                               \
}

XVSRAR(xvsrar_b, 8, XB, UXB)
XVSRAR(xvsrar_h, 16, XH, UXH)
XVSRAR(xvsrar_w, 32, XW, UXW)
XVSRAR(xvsrar_d, 64, XD, UXD)

#define XVSRARI(NAME, BIT, E)                             \
void HELPER(NAME)(CPULoongArchState *env,                 \
                  uint32_t xd, uint32_t xj, uint32_t imm) \
{                                                         \
    int i;                                                \
    XReg *Xd = &(env->fpr[xd].xreg);                      \
    XReg *Xj = &(env->fpr[xj].xreg);                      \
                                                          \
    for (i = 0; i < LASX_LEN / BIT; i++) {                \
        Xd->E(i) = do_xvsrar_ ## E(Xj->E(i), imm);        \
    }                                                     \
}

XVSRARI(xvsrari_b, 8, XB)
XVSRARI(xvsrari_h, 16, XH)
XVSRARI(xvsrari_w, 32, XW)
XVSRARI(xvsrari_d, 64, XD)

#define XVSRLN(NAME, BIT, E1, E2)                             \
void HELPER(NAME)(CPULoongArchState *env,                     \
                  uint32_t xd, uint32_t xj, uint32_t xk)      \
{                                                             \
    int i, max;                                               \
    XReg *Xd = &(env->fpr[xd].xreg);                          \
    XReg *Xj = &(env->fpr[xj].xreg);                          \
    XReg *Xk = &(env->fpr[xk].xreg);                          \
                                                              \
    max = LASX_LEN / (BIT * 2);                               \
    for (i = 0; i < max; i++) {                               \
        Xd->E1(i) = R_SHIFT(Xj->E2(i), (Xk->E2(i)) % BIT);    \
        Xd->E1(i + max * 2) = R_SHIFT(Xj->E2(i + max),        \
                                      Xk->E2(i + max) % BIT); \
    }                                                         \
    Xd->XD(1) = 0;                                            \
    Xd->XD(3) = 0;                                            \
}

XVSRLN(xvsrln_b_h, 16, XB, UXH)
XVSRLN(xvsrln_h_w, 32, XH, UXW)
XVSRLN(xvsrln_w_d, 64, XW, UXD)

#define XVSRAN(NAME, BIT, E1, E2, E3)                         \
void HELPER(NAME)(CPULoongArchState *env,                     \
                  uint32_t xd, uint32_t xj, uint32_t xk)      \
{                                                             \
    int i, max;                                               \
    XReg *Xd = &(env->fpr[xd].xreg);                          \
    XReg *Xj = &(env->fpr[xj].xreg);                          \
    XReg *Xk = &(env->fpr[xk].xreg);                          \
                                                              \
    max = LASX_LEN / (BIT * 2);                               \
    for (i = 0; i < max; i++) {                               \
        Xd->E1(i) = R_SHIFT(Xj->E2(i), (Xk->E3(i)) % BIT);    \
        Xd->E1(i + max * 2) = R_SHIFT(Xj->E2(i + max),        \
                                      Xk->E3(i + max) % BIT); \
    }                                                         \
    Xd->XD(1) = 0;                                            \
    Xd->XD(3) = 0;                                            \
}

XVSRAN(xvsran_b_h, 16, XB, XH, UXH)
XVSRAN(xvsran_h_w, 32, XH, XW, UXW)
XVSRAN(xvsran_w_d, 64, XW, XD, UXD)

#define XVSRLNI(NAME, BIT, E1, E2)                            \
void HELPER(NAME)(CPULoongArchState *env,                     \
                  uint32_t xd, uint32_t xj, uint32_t imm)     \
{                                                             \
    int i, max;                                               \
    XReg temp;                                                \
    XReg *Xd = &(env->fpr[xd].xreg);                          \
    XReg *Xj = &(env->fpr[xj].xreg);                          \
                                                              \
    temp.XQ(0) = int128_zero();                               \
    temp.XQ(1) = int128_zero();                               \
    max = LASX_LEN / (BIT * 2);                               \
    for (i = 0; i < max; i++) {                               \
        temp.E1(i) = R_SHIFT(Xj->E2(i), imm);                 \
        temp.E1(i + max) = R_SHIFT(Xd->E2(i), imm);           \
        temp.E1(i + max * 2) = R_SHIFT(Xj->E2(i + max), imm); \
        temp.E1(i + max * 3) = R_SHIFT(Xd->E2(i + max), imm); \
    }                                                         \
    *Xd = temp;                                               \
}

void HELPER(xvsrlni_d_q)(CPULoongArchState *env,
                         uint32_t xd, uint32_t xj, uint32_t imm)
{
    XReg temp;
    XReg *Xd = &(env->fpr[xd].xreg);
    XReg *Xj = &(env->fpr[xj].xreg);

    temp.XQ(0) = int128_zero();
    temp.XQ(1) = int128_zero();
    temp.XD(0) = int128_getlo(int128_urshift(Xj->XQ(0), imm % 128));
    temp.XD(1) = int128_getlo(int128_urshift(Xd->XQ(0), imm % 128));
    temp.XD(2) = int128_getlo(int128_urshift(Xj->XQ(1), imm % 128));
    temp.XD(3) = int128_getlo(int128_urshift(Xd->XQ(1), imm % 128));
    *Xd = temp;
}

XVSRLNI(xvsrlni_b_h, 16, XB, UXH)
XVSRLNI(xvsrlni_h_w, 32, XH, UXW)
XVSRLNI(xvsrlni_w_d, 64, XW, UXD)

#define XVSRANI(NAME, BIT, E1, E2)                            \
void HELPER(NAME)(CPULoongArchState *env,                     \
                  uint32_t xd, uint32_t xj, uint32_t imm)     \
{                                                             \
    int i, max;                                               \
    XReg temp;                                                \
    XReg *Xd = &(env->fpr[xd].xreg);                          \
    XReg *Xj = &(env->fpr[xj].xreg);                          \
                                                              \
    temp.XQ(0) = int128_zero();                               \
    temp.XQ(1) = int128_zero();                               \
    max = LASX_LEN / (BIT * 2);                               \
    for (i = 0; i < max; i++) {                               \
        temp.E1(i) = R_SHIFT(Xj->E2(i), imm);                 \
        temp.E1(i + max) = R_SHIFT(Xd->E2(i), imm);           \
        temp.E1(i + max * 2) = R_SHIFT(Xj->E2(i + max), imm); \
        temp.E1(i + max * 3) = R_SHIFT(Xd->E2(i + max), imm); \
    }                                                         \
    *Xd = temp;                                               \
}

void HELPER(xvsrani_d_q)(CPULoongArchState *env,
                         uint32_t xd, uint32_t xj, uint32_t imm)
{
    XReg temp;
    XReg *Xd = &(env->fpr[xd].xreg);
    XReg *Xj = &(env->fpr[xj].xreg);

    temp.XQ(0) = int128_zero();
    temp.XQ(1) = int128_zero();
    temp.XD(0) = int128_getlo(int128_rshift(Xj->XQ(0), imm % 128));
    temp.XD(1) = int128_getlo(int128_rshift(Xd->XQ(0), imm % 128));
    temp.XD(2) = int128_getlo(int128_rshift(Xj->XQ(1), imm % 128));
    temp.XD(3) = int128_getlo(int128_rshift(Xd->XQ(1), imm % 128));
    *Xd = temp;
}

XVSRANI(xvsrani_b_h, 16, XB, XH)
XVSRANI(xvsrani_h_w, 32, XH, XW)
XVSRANI(xvsrani_w_d, 64, XW, XD)

#define XVSRLRN(NAME, BIT, E1, E2, E3)                                \
void HELPER(NAME)(CPULoongArchState *env,                             \
                  uint32_t xd, uint32_t xj, uint32_t xk)              \
{                                                                     \
    int i, max;                                                       \
    XReg *Xd = &(env->fpr[xd].xreg);                                  \
    XReg *Xj = &(env->fpr[xj].xreg);                                  \
    XReg *Xk = &(env->fpr[xk].xreg);                                  \
                                                                      \
    max = LASX_LEN / (BIT * 2);                                       \
    for (i = 0; i < max; i++) {                                       \
        Xd->E1(i) = do_xvsrlr_ ## E2(Xj->E2(i), (Xk->E3(i)) % BIT);   \
        Xd->E1(i + max * 2) = do_xvsrlr_## E2(Xj->E2(i + max),        \
                                              Xk->E3(i + max) % BIT); \
    }                                                                 \
    Xd->XD(1) = 0;                                                    \
    Xd->XD(3) = 0;                                                    \
}

XVSRLRN(xvsrlrn_b_h, 16, XB, XH, UXH)
XVSRLRN(xvsrlrn_h_w, 32, XH, XW, UXW)
XVSRLRN(xvsrlrn_w_d, 64, XW, XD, UXD)

#define XVSRARN(NAME, BIT, E1, E2, E3)                                \
void HELPER(NAME)(CPULoongArchState *env,                             \
                  uint32_t xd, uint32_t xj, uint32_t xk)              \
{                                                                     \
    int i, max;                                                       \
    XReg *Xd = &(env->fpr[xd].xreg);                                  \
    XReg *Xj = &(env->fpr[xj].xreg);                                  \
    XReg *Xk = &(env->fpr[xk].xreg);                                  \
                                                                      \
    max = LASX_LEN / (BIT * 2);                                       \
    for (i = 0; i < max; i++) {                                       \
        Xd->E1(i) = do_xvsrar_ ## E2(Xj->E2(i), (Xk->E3(i)) % BIT);   \
        Xd->E1(i + max * 2) = do_xvsrar_## E2(Xj->E2(i + max),        \
                                              Xk->E3(i + max) % BIT); \
    }                                                                 \
    Xd->XD(1) = 0;                                                    \
    Xd->XD(3) = 0;                                                    \
}

XVSRARN(xvsrarn_b_h, 16, XB, XH, UXH)
XVSRARN(xvsrarn_h_w, 32, XH, XW, UXW)
XVSRARN(xvsrarn_w_d, 64, XW, XD, UXD)

#define XVSRLRNI(NAME, BIT, E1, E2)                                   \
void HELPER(NAME)(CPULoongArchState *env,                             \
                  uint32_t xd, uint32_t xj, uint32_t imm)             \
{                                                                     \
    int i, max;                                                       \
    XReg temp;                                                        \
    XReg *Xd = &(env->fpr[xd].xreg);                                  \
    XReg *Xj = &(env->fpr[xj].xreg);                                  \
                                                                      \
    temp.XQ(0) = int128_zero();                                       \
    temp.XQ(1) = int128_zero();                                       \
    max = LASX_LEN / (BIT * 2);                                       \
    for (i = 0; i < max; i++) {                                       \
        temp.E1(i) = do_xvsrlr_ ## E2(Xj->E2(i), imm);                \
        temp.E1(i + max) = do_xvsrlr_ ## E2(Xd->E2(i), imm);          \
        temp.E1(i + max * 2) = do_xvsrlr_## E2(Xj->E2(i + max), imm); \
        temp.E1(i + max * 3) = do_xvsrlr_## E2(Xd->E2(i + max), imm); \
    }                                                                 \
    *Xd = temp;                                                       \
}

void HELPER(xvsrlrni_d_q)(CPULoongArchState *env,
                          uint32_t xd, uint32_t xj, uint32_t imm)
{
    XReg temp;
    XReg *Xd = &(env->fpr[xd].xreg);
    XReg *Xj = &(env->fpr[xj].xreg);
    Int128 r1, r2, r3, r4;

    if (imm == 0) {
        temp.XD(0) = int128_getlo(Xj->XQ(0));
        temp.XD(1) = int128_getlo(Xd->XQ(0));
        temp.XD(2) = int128_getlo(Xj->XQ(1));
        temp.XD(3) = int128_getlo(Xd->XQ(1));
    } else {
        r1 = int128_and(int128_urshift(Xj->XQ(0), (imm - 1)), int128_one());
        r2 = int128_and(int128_urshift(Xd->XQ(0), (imm - 1)), int128_one());
        r3 = int128_and(int128_urshift(Xj->XQ(1), (imm - 1)), int128_one());
        r4 = int128_and(int128_urshift(Xd->XQ(1), (imm - 1)), int128_one());

       temp.XD(0) = int128_getlo(int128_add(int128_urshift(Xj->XQ(0), imm), r1));
       temp.XD(1) = int128_getlo(int128_add(int128_urshift(Xd->XQ(0), imm), r2));
       temp.XD(2) = int128_getlo(int128_add(int128_urshift(Xj->XQ(1), imm), r3));
       temp.XD(3) = int128_getlo(int128_add(int128_urshift(Xd->XQ(1), imm), r4));
    }
    *Xd = temp;
}

XVSRLRNI(xvsrlrni_b_h, 16, XB, XH)
XVSRLRNI(xvsrlrni_h_w, 32, XH, XW)
XVSRLRNI(xvsrlrni_w_d, 64, XW, XD)

#define XVSRARNI(NAME, BIT, E1, E2)                                   \
void HELPER(NAME)(CPULoongArchState *env,                             \
                  uint32_t xd, uint32_t xj, uint32_t imm)             \
{                                                                     \
    int i, max;                                                       \
    XReg temp;                                                        \
    XReg *Xd = &(env->fpr[xd].xreg);                                  \
    XReg *Xj = &(env->fpr[xj].xreg);                                  \
                                                                      \
    temp.XQ(0) = int128_zero();                                       \
    temp.XQ(1) = int128_zero();                                       \
    max = LASX_LEN / (BIT * 2);                                       \
    for (i = 0; i < max; i++) {                                       \
        temp.E1(i) = do_xvsrar_ ## E2(Xj->E2(i), imm);                \
        temp.E1(i + max) = do_xvsrar_ ## E2(Xd->E2(i), imm);          \
        temp.E1(i + max * 2) = do_xvsrar_## E2(Xj->E2(i + max), imm); \
        temp.E1(i + max * 3) = do_xvsrar_## E2(Xd->E2(i + max), imm); \
    }                                                                 \
    *Xd = temp;                                                       \
}

void HELPER(xvsrarni_d_q)(CPULoongArchState *env,
                          uint32_t xd, uint32_t xj, uint32_t imm)
{
    XReg temp;
    XReg *Xd = &(env->fpr[xd].xreg);
    XReg *Xj = &(env->fpr[xj].xreg);
    Int128 r1, r2, r3, r4;

    if (imm == 0) {
        temp.XD(0) = int128_getlo(Xj->XQ(0));
        temp.XD(1) = int128_getlo(Xd->XQ(0));
        temp.XD(2) = int128_getlo(Xj->XQ(1));
        temp.XD(3) = int128_getlo(Xd->XQ(1));
    } else {
        r1 = int128_and(int128_rshift(Xj->XQ(0), (imm - 1)), int128_one());
        r2 = int128_and(int128_rshift(Xd->XQ(0), (imm - 1)), int128_one());
        r3 = int128_and(int128_rshift(Xj->XQ(1), (imm - 1)), int128_one());
        r4 = int128_and(int128_rshift(Xd->XQ(1), (imm - 1)), int128_one());

       temp.XD(0) = int128_getlo(int128_add(int128_rshift(Xj->XQ(0), imm), r1));
       temp.XD(1) = int128_getlo(int128_add(int128_rshift(Xd->XQ(0), imm), r2));
       temp.XD(2) = int128_getlo(int128_add(int128_rshift(Xj->XQ(1), imm), r3));
       temp.XD(3) = int128_getlo(int128_add(int128_rshift(Xd->XQ(1), imm), r4));
    }
    *Xd = temp;
}

XVSRARNI(xvsrarni_b_h, 16, XB, XH)
XVSRARNI(xvsrarni_h_w, 32, XH, XW)
XVSRARNI(xvsrarni_w_d, 64, XW, XD)

#define XSSRLNS(NAME, T1, T2, T3)                    \
static T1 do_xssrlns_ ## NAME(T2 e2, int sa, int sh) \
{                                                    \
        T1 shft_res;                                 \
        if (sa == 0) {                               \
            shft_res = e2;                           \
        } else {                                     \
            shft_res = (((T1)e2) >> sa);             \
        }                                            \
        T3 mask;                                     \
        mask = (1ull << sh) - 1;                     \
        if (shft_res > mask) {                       \
            return mask;                             \
        } else {                                     \
            return  shft_res;                        \
        }                                            \
}

XSSRLNS(XB, uint16_t, int16_t, uint8_t)
XSSRLNS(XH, uint32_t, int32_t, uint16_t)
XSSRLNS(XW, uint64_t, int64_t, uint32_t)

#define XVSSRLN(NAME, BIT, E1, E2, E3)                                 \
void HELPER(NAME)(CPULoongArchState *env,                              \
                  uint32_t xd, uint32_t xj, uint32_t xk)               \
{                                                                      \
    int i, max;                                                        \
    XReg *Xd = &(env->fpr[xd].xreg);                                   \
    XReg *Xj = &(env->fpr[xj].xreg);                                   \
    XReg *Xk = &(env->fpr[xk].xreg);                                   \
                                                                       \
    max = LASX_LEN / (BIT * 2);                                        \
    for (i = 0; i < max; i++) {                                        \
        Xd->E1(i) = do_xssrlns_ ## E1(Xj->E2(i),                       \
                                      Xk->E3(i) % BIT, (BIT / 2) - 1); \
        Xd->E1(i + max * 2) = do_xssrlns_## E1(Xj->E2(i + max),        \
                                               Xk->E3(i + max) % BIT,  \
                                               (BIT / 2) - 1);         \
    }                                                                  \
    Xd->XD(1) = 0;                                                     \
    Xd->XD(3) = 0;                                                     \
}

XVSSRLN(xvssrln_b_h, 16, XB, XH, UXH)
XVSSRLN(xvssrln_h_w, 32, XH, XW, UXW)
XVSSRLN(xvssrln_w_d, 64, XW, XD, UXD)

#define XSSRANS(E, T1, T2)                        \
static T1 do_xssrans_ ## E(T1 e2, int sa, int sh) \
{                                                 \
        T1 shft_res;                              \
        if (sa == 0) {                            \
            shft_res = e2;                        \
        } else {                                  \
            shft_res = e2 >> sa;                  \
        }                                         \
        T2 mask;                                  \
        mask = (1ll << sh) - 1;                   \
        if (shft_res > mask) {                    \
            return  mask;                         \
        } else if (shft_res < -(mask + 1)) {      \
            return  ~mask;                        \
        } else {                                  \
            return shft_res;                      \
        }                                         \
}

XSSRANS(XB, int16_t, int8_t)
XSSRANS(XH, int32_t, int16_t)
XSSRANS(XW, int64_t, int32_t)

#define XVSSRAN(NAME, BIT, E1, E2, E3)                                 \
void HELPER(NAME)(CPULoongArchState *env,                              \
                  uint32_t xd, uint32_t xj, uint32_t xk)               \
{                                                                      \
    int i, max;                                                        \
    XReg *Xd = &(env->fpr[xd].xreg);                                   \
    XReg *Xj = &(env->fpr[xj].xreg);                                   \
    XReg *Xk = &(env->fpr[xk].xreg);                                   \
                                                                       \
    max = LASX_LEN / (BIT * 2);                                        \
    for (i = 0; i < max; i++) {                                        \
        Xd->E1(i) = do_xssrans_ ## E1(Xj->E2(i),                       \
                                      Xk->E3(i) % BIT, (BIT / 2) - 1); \
        Xd->E1(i + max * 2) = do_xssrans_## E1(Xj->E2(i + max),        \
                                               Xk->E3(i + max) % BIT,  \
                                               (BIT / 2) - 1);         \
    }                                                                  \
    Xd->XD(1) = 0;                                                     \
    Xd->XD(3) = 0;                                                     \
}

XVSSRAN(xvssran_b_h, 16, XB, XH, UXH)
XVSSRAN(xvssran_h_w, 32, XH, XW, UXW)
XVSSRAN(xvssran_w_d, 64, XW, XD, UXD)

#define XSSRLNU(E, T1, T2, T3)                    \
static T1 do_xssrlnu_ ## E(T3 e2, int sa, int sh) \
{                                                 \
        T1 shft_res;                              \
        if (sa == 0) {                            \
            shft_res = e2;                        \
        } else {                                  \
            shft_res = (((T1)e2) >> sa);          \
        }                                         \
        T2 mask;                                  \
        mask = (1ull << sh) - 1;                  \
        if (shft_res > mask) {                    \
            return mask;                          \
        } else {                                  \
            return shft_res;                      \
        }                                         \
}

XSSRLNU(XB, uint16_t, uint8_t,  int16_t)
XSSRLNU(XH, uint32_t, uint16_t, int32_t)
XSSRLNU(XW, uint64_t, uint32_t, int64_t)

#define XVSSRLNU(NAME, BIT, E1, E2, E3)                                     \
void HELPER(NAME)(CPULoongArchState *env,                                   \
                  uint32_t xd, uint32_t xj, uint32_t xk)                    \
{                                                                           \
    int i, max;                                                             \
    XReg *Xd = &(env->fpr[xd].xreg);                                        \
    XReg *Xj = &(env->fpr[xj].xreg);                                        \
    XReg *Xk = &(env->fpr[xk].xreg);                                        \
                                                                            \
    max = LASX_LEN / (BIT * 2);                                             \
    for (i = 0; i < max; i++) {                                             \
        Xd->E1(i) = do_xssrlnu_ ## E1(Xj->E2(i), Xk->E3(i) % BIT, BIT / 2); \
        Xd->E1(i + max * 2) = do_xssrlnu_## E1(Xj->E2(i + max),             \
                                               Xk->E3(i + max) % BIT,       \
                                               BIT / 2);                    \
    }                                                                       \
    Xd->XD(1) = 0;                                                          \
    Xd->XD(3) = 0;                                                          \
}

XVSSRLNU(xvssrln_bu_h, 16, XB, XH, UXH)
XVSSRLNU(xvssrln_hu_w, 32, XH, XW, UXW)
XVSSRLNU(xvssrln_wu_d, 64, XW, XD, UXD)

#define XSSRANU(E, T1, T2, T3)                    \
static T1 do_xssranu_ ## E(T3 e2, int sa, int sh) \
{                                                 \
        T1 shft_res;                              \
        if (sa == 0) {                            \
            shft_res = e2;                        \
        } else {                                  \
            shft_res = e2 >> sa;                  \
        }                                         \
        if (e2 < 0) {                             \
            shft_res = 0;                         \
        }                                         \
        T2 mask;                                  \
        mask = (1ull << sh) - 1;                  \
        if (shft_res > mask) {                    \
            return mask;                          \
        } else {                                  \
            return shft_res;                      \
        }                                         \
}

XSSRANU(XB, uint16_t, uint8_t,  int16_t)
XSSRANU(XH, uint32_t, uint16_t, int32_t)
XSSRANU(XW, uint64_t, uint32_t, int64_t)

#define XVSSRANU(NAME, BIT, E1, E2, E3)                                     \
void HELPER(NAME)(CPULoongArchState *env,                                   \
                  uint32_t xd, uint32_t xj, uint32_t xk)                    \
{                                                                           \
    int i, max;                                                             \
    XReg *Xd = &(env->fpr[xd].xreg);                                        \
    XReg *Xj = &(env->fpr[xj].xreg);                                        \
    XReg *Xk = &(env->fpr[xk].xreg);                                        \
                                                                            \
    max = LASX_LEN / (BIT * 2);                                             \
    for (i = 0; i < max; i++) {                                             \
        Xd->E1(i) = do_xssranu_ ## E1(Xj->E2(i), Xk->E3(i) % BIT, BIT / 2); \
        Xd->E1(i + max * 2) = do_xssranu_## E1(Xj->E2(i + max),             \
                                               Xk->E3(i + max) % BIT,       \
                                               BIT / 2);                    \
    }                                                                       \
    Xd->XD(1) = 0;                                                          \
    Xd->XD(3) = 0;                                                          \
}

XVSSRANU(xvssran_bu_h, 16, XB, XH, UXH)
XVSSRANU(xvssran_hu_w, 32, XH, XW, UXW)
XVSSRANU(xvssran_wu_d, 64, XW, XD, UXD)

#define XVSSRLNI(NAME, BIT, E1, E2)                                          \
void HELPER(NAME)(CPULoongArchState *env,                                    \
                  uint32_t xd, uint32_t xj, uint32_t imm)                    \
{                                                                            \
    int i, max;                                                              \
    XReg temp;                                                               \
    XReg *Xd = &(env->fpr[xd].xreg);                                         \
    XReg *Xj = &(env->fpr[xj].xreg);                                         \
                                                                             \
    max = LASX_LEN / (BIT * 2);                                              \
    for (i = 0; i < max; i++) {                                              \
        temp.E1(i) = do_xssrlns_ ## E1(Xj->E2(i), imm, (BIT / 2) - 1);       \
        temp.E1(i + max) = do_xssrlns_ ## E1(Xd->E2(i), imm, (BIT / 2) - 1); \
        temp.E1(i + max * 2) = do_xssrlns_## E1(Xj->E2(i + max),             \
                                                imm, (BIT / 2) - 1);         \
        temp.E1(i + max * 3) = do_xssrlns_## E1(Xd->E2(i + max),             \
                                                imm, (BIT / 2) - 1);         \
    }                                                                        \
    *Xd = temp;                                                              \
}

void HELPER(xvssrlni_d_q)(CPULoongArchState *env,
                          uint32_t xd, uint32_t xj, uint32_t imm)
{
    int i;
    Int128 shft_res[4], mask;
    XReg *Xd = &(env->fpr[xd].xreg);
    XReg *Xj = &(env->fpr[xj].xreg);

    if (imm == 0) {
        shft_res[0] = Xj->XQ(0);
        shft_res[1] = Xd->XQ(0);
        shft_res[2] = Xj->XQ(1);
        shft_res[3] = Xd->XQ(1);
    } else {
        shft_res[0] = int128_urshift(Xj->XQ(0), imm);
        shft_res[1] = int128_urshift(Xd->XQ(0), imm);
        shft_res[2] = int128_urshift(Xj->XQ(1), imm);
        shft_res[3] = int128_urshift(Xd->XQ(1), imm);
    }
    mask = int128_sub(int128_lshift(int128_one(), 63), int128_one());

    for (i = 0; i < 4; i++) {
        if (int128_ult(mask, shft_res[i])) {
            Xd->XD(i) = int128_getlo(mask);
        } else {
            Xd->XD(i) = int128_getlo(shft_res[i]);
        }
    }
}

XVSSRLNI(xvssrlni_b_h, 16, XB, XH)
XVSSRLNI(xvssrlni_h_w, 32, XH, XW)
XVSSRLNI(xvssrlni_w_d, 64, XW, XD)

#define XVSSRANI(NAME, BIT, E1, E2)                                          \
void HELPER(NAME)(CPULoongArchState *env,                                    \
                  uint32_t xd, uint32_t xj, uint32_t imm)                    \
{                                                                            \
    int i, max;                                                              \
    XReg temp;                                                               \
    XReg *Xd = &(env->fpr[xd].xreg);                                         \
    XReg *Xj = &(env->fpr[xj].xreg);                                         \
                                                                             \
    max = LASX_LEN / (BIT * 2);                                              \
    for (i = 0; i < max; i++) {                                              \
        temp.E1(i) = do_xssrans_ ## E1(Xj->E2(i), imm, (BIT / 2) - 1);       \
        temp.E1(i + max) = do_xssrans_ ## E1(Xd->E2(i), imm, (BIT / 2) - 1); \
        temp.E1(i + max * 2) = do_xssrans_## E1(Xj->E2(i + max),             \
                                                imm, (BIT / 2) - 1);         \
        temp.E1(i + max * 3) = do_xssrans_## E1(Xd->E2(i + max),             \
                                                imm, (BIT / 2) - 1);         \
    }                                                                        \
    *Xd = temp;                                                              \
}

void HELPER(xvssrani_d_q)(CPULoongArchState *env,
                          uint32_t xd, uint32_t xj, uint32_t imm)
{
    int i;
    Int128 shft_res[4], mask, min;
    XReg *Xd = &(env->fpr[xd].xreg);
    XReg *Xj = &(env->fpr[xj].xreg);

    if (imm == 0) {
        shft_res[0] = Xj->XQ(0);
        shft_res[1] = Xd->XQ(0);
        shft_res[2] = Xj->XQ(1);
        shft_res[3] = Xd->XQ(1);
    } else {
        shft_res[0] = int128_rshift(Xj->XQ(0), imm);
        shft_res[1] = int128_rshift(Xd->XQ(0), imm);
        shft_res[2] = int128_rshift(Xj->XQ(1), imm);
        shft_res[3] = int128_rshift(Xd->XQ(1), imm);
    }
    mask = int128_sub(int128_lshift(int128_one(), 63), int128_one());
    min  = int128_lshift(int128_one(), 63);

    for (i = 0; i < 4; i++) {
        if (int128_gt(shft_res[i],  mask)) {
            Xd->XD(i) = int128_getlo(mask);
        } else if (int128_lt(shft_res[i], int128_neg(min))) {
            Xd->XD(i) = int128_getlo(min);
        } else {
            Xd->XD(i) = int128_getlo(shft_res[i]);
        }
    }
}

XVSSRANI(xvssrani_b_h, 16, XB, XH)
XVSSRANI(xvssrani_h_w, 32, XH, XW)
XVSSRANI(xvssrani_w_d, 64, XW, XD)

#define XVSSRLNUI(NAME, BIT, E1, E2)                                   \
void HELPER(NAME)(CPULoongArchState *env,                              \
                  uint32_t xd, uint32_t xj, uint32_t imm)              \
{                                                                      \
    int i, max;                                                        \
    XReg temp;                                                         \
    XReg *Xd = &(env->fpr[xd].xreg);                                   \
    XReg *Xj = &(env->fpr[xj].xreg);                                   \
                                                                       \
    max = LASX_LEN / (BIT * 2);                                        \
    for (i = 0; i < max; i++) {                                        \
        temp.E1(i) = do_xssrlnu_ ## E1(Xj->E2(i), imm, BIT / 2);       \
        temp.E1(i + max) = do_xssrlnu_ ## E1(Xd->E2(i), imm, BIT / 2); \
        temp.E1(i + max * 2) = do_xssrlnu_## E1(Xj->E2(i + max),       \
                                                imm, BIT / 2);         \
        temp.E1(i + max * 3) = do_xssrlnu_## E1(Xd->E2(i + max),       \
                                                       imm, BIT / 2);  \
    }                                                                  \
    *Xd = temp;                                                        \
}

void HELPER(xvssrlni_du_q)(CPULoongArchState *env,
                           uint32_t xd, uint32_t xj, uint32_t imm)
{
    int i;
    Int128 shft_res[4], mask;
    XReg *Xd = &(env->fpr[xd].xreg);
    XReg *Xj = &(env->fpr[xj].xreg);

    if (imm == 0) {
        shft_res[0] = Xj->XQ(0);
        shft_res[1] = Xd->XQ(0);
        shft_res[2] = Xj->XQ(1);
        shft_res[3] = Xd->XQ(1);
    } else {
        shft_res[0] = int128_urshift(Xj->XQ(0), imm);
        shft_res[1] = int128_urshift(Xd->XQ(0), imm);
        shft_res[2] = int128_urshift(Xj->XQ(1), imm);
        shft_res[3] = int128_urshift(Xd->XQ(1), imm);
    }
    mask = int128_sub(int128_lshift(int128_one(), 64), int128_one());

    for (i = 0; i < 4; i++) {
        if (int128_ult(mask, shft_res[i])) {
            Xd->XD(i) = int128_getlo(mask);
        } else {
            Xd->XD(i) = int128_getlo(shft_res[i]);
        }
    }
}

XVSSRLNUI(xvssrlni_bu_h, 16, XB, XH)
XVSSRLNUI(xvssrlni_hu_w, 32, XH, XW)
XVSSRLNUI(xvssrlni_wu_d, 64, XW, XD)

#define XVSSRANUI(NAME, BIT, E1, E2)                                   \
void HELPER(NAME)(CPULoongArchState *env,                              \
                  uint32_t xd, uint32_t xj, uint32_t imm)              \
{                                                                      \
    int i, max;                                                        \
    XReg temp;                                                         \
    XReg *Xd = &(env->fpr[xd].xreg);                                   \
    XReg *Xj = &(env->fpr[xj].xreg);                                   \
                                                                       \
    max = LASX_LEN / (BIT * 2);                                        \
    for (i = 0; i < max; i++) {                                        \
        temp.E1(i) = do_xssranu_ ## E1(Xj->E2(i), imm, BIT / 2);       \
        temp.E1(i + max) = do_xssranu_ ## E1(Xd->E2(i), imm, BIT / 2); \
        temp.E1(i + max * 2) = do_xssranu_## E1(Xj->E2(i + max),       \
                                                imm, BIT / 2);         \
        temp.E1(i + max * 3) = do_xssranu_## E1(Xd->E2(i + max),       \
                                                imm, BIT / 2);         \
    }                                                                  \
    *Xd = temp;                                                        \
}

void HELPER(xvssrani_du_q)(CPULoongArchState *env,
                           uint32_t xd, uint32_t xj, uint32_t imm)
{
    int i;
    Int128 shft_res[4], mask;
    XReg *Xd = &(env->fpr[xd].xreg);
    XReg *Xj = &(env->fpr[xj].xreg);

    if (imm == 0) {
        shft_res[0] = Xj->XQ(0);
        shft_res[1] = Xd->XQ(0);
        shft_res[2] = Xj->XQ(1);
        shft_res[3] = Xd->XQ(1);
    } else {
        shft_res[0] = int128_rshift(Xj->XQ(0), imm);
        shft_res[1] = int128_rshift(Xd->XQ(0), imm);
        shft_res[2] = int128_rshift(Xj->XQ(1), imm);
        shft_res[3] = int128_rshift(Xd->XQ(1), imm);
    }

    if (int128_lt(Xj->XQ(0), int128_zero())) {
        shft_res[0] = int128_zero();
    }
    if (int128_lt(Xd->XQ(0), int128_zero())) {
        shft_res[1] = int128_zero();
    }
    if (int128_lt(Xj->XQ(1), int128_zero())) {
        shft_res[2] = int128_zero();
    }
    if (int128_lt(Xd->XQ(1), int128_zero())) {
        shft_res[3] = int128_zero();
    }

    mask = int128_sub(int128_lshift(int128_one(), 64), int128_one());

    for (i = 0; i < 4; i++) {
        if (int128_ult(mask, shft_res[i])) {
            Xd->XD(i) = int128_getlo(mask);
        } else {
            Xd->XD(i) = int128_getlo(shft_res[i]);
        }
    }
}

XVSSRANUI(xvssrani_bu_h, 16, XB, XH)
XVSSRANUI(xvssrani_hu_w, 32, XH, XW)
XVSSRANUI(xvssrani_wu_d, 64, XW, XD)

#define XSSRLRNS(E1, E2, T1, T2, T3)                \
static T1 do_xssrlrns_ ## E1(T2 e2, int sa, int sh) \
{                                                   \
    T1 shft_res;                                    \
                                                    \
    shft_res = do_xvsrlr_ ## E2(e2, sa);            \
    T1 mask;                                        \
    mask = (1ull << sh) - 1;                        \
    if (shft_res > mask) {                          \
        return mask;                                \
    } else {                                        \
        return  shft_res;                           \
    }                                               \
}

XSSRLRNS(XB, XH, uint16_t, int16_t, uint8_t)
XSSRLRNS(XH, XW, uint32_t, int32_t, uint16_t)
XSSRLRNS(XW, XD, uint64_t, int64_t, uint32_t)

#define XVSSRLRN(NAME, BIT, E1, E2, E3)                                 \
void HELPER(NAME)(CPULoongArchState *env,                               \
                  uint32_t xd, uint32_t xj, uint32_t xk)                \
{                                                                       \
    int i, max;                                                         \
    XReg *Xd = &(env->fpr[xd].xreg);                                    \
    XReg *Xj = &(env->fpr[xj].xreg);                                    \
    XReg *Xk = &(env->fpr[xk].xreg);                                    \
                                                                        \
    max = LASX_LEN / (BIT * 2);                                         \
    for (i = 0; i < max; i++) {                                         \
        Xd->E1(i) = do_xssrlrns_ ## E1(Xj->E2(i),                       \
                                       Xk->E3(i) % BIT, (BIT / 2) - 1); \
        Xd->E1(i + max * 2) = do_xssrlrns_## E1(Xj->E2(i + max),        \
                                                Xk->E3(i + max) % BIT,  \
                                                (BIT / 2) - 1);         \
    }                                                                   \
    Xd->XD(1) = 0;                                                      \
    Xd->XD(3) = 0;                                                      \
}

XVSSRLRN(xvssrlrn_b_h, 16, XB, XH, UXH)
XVSSRLRN(xvssrlrn_h_w, 32, XH, XW, UXW)
XVSSRLRN(xvssrlrn_w_d, 64, XW, XD, UXD)

#define XSSRARNS(E1, E2, T1, T2)                    \
static T1 do_xssrarns_ ## E1(T1 e2, int sa, int sh) \
{                                                   \
    T1 shft_res;                                    \
                                                    \
    shft_res = do_xvsrar_ ## E2(e2, sa);            \
    T2 mask;                                        \
    mask = (1ll << sh) - 1;                         \
    if (shft_res > mask) {                          \
        return  mask;                               \
    } else if (shft_res < -(mask + 1)) {            \
        return  ~mask;                              \
    } else {                                        \
        return shft_res;                            \
    }                                               \
}

XSSRARNS(XB, XH, int16_t, int8_t)
XSSRARNS(XH, XW, int32_t, int16_t)
XSSRARNS(XW, XD, int64_t, int32_t)

#define XVSSRARN(NAME, BIT, E1, E2, E3)                                 \
void HELPER(NAME)(CPULoongArchState *env,                               \
                  uint32_t xd, uint32_t xj, uint32_t xk)                \
{                                                                       \
    int i, max;                                                         \
    XReg *Xd = &(env->fpr[xd].xreg);                                    \
    XReg *Xj = &(env->fpr[xj].xreg);                                    \
    XReg *Xk = &(env->fpr[xk].xreg);                                    \
                                                                        \
    max = LASX_LEN / (BIT * 2);                                         \
    for (i = 0; i < max; i++) {                                         \
        Xd->E1(i) = do_xssrarns_ ## E1(Xj->E2(i),                       \
                                       Xk->E3(i) % BIT, (BIT / 2) - 1); \
        Xd->E1(i + max * 2) = do_xssrarns_## E1(Xj->E2(i + max),        \
                                                Xk->E3(i + max) % BIT,  \
                                                (BIT / 2) - 1);         \
    }                                                                   \
    Xd->XD(1) = 0;                                                      \
    Xd->XD(3) = 0;                                                      \
}

XVSSRARN(xvssrarn_b_h, 16, XB, XH, UXH)
XVSSRARN(xvssrarn_h_w, 32, XH, XW, UXW)
XVSSRARN(xvssrarn_w_d, 64, XW, XD, UXD)

#define XSSRLRNU(E1, E2, T1, T2, T3)                \
static T1 do_xssrlrnu_ ## E1(T3 e2, int sa, int sh) \
{                                                   \
    T1 shft_res;                                    \
                                                    \
    shft_res = do_xvsrlr_ ## E2(e2, sa);            \
                                                    \
    T2 mask;                                        \
    mask = (1ull << sh) - 1;                        \
    if (shft_res > mask) {                          \
        return mask;                                \
    } else {                                        \
        return shft_res;                            \
    }                                               \
}

XSSRLRNU(XB, XH, uint16_t, uint8_t, int16_t)
XSSRLRNU(XH, XW, uint32_t, uint16_t, int32_t)
XSSRLRNU(XW, XD, uint64_t, uint32_t, int64_t)

#define XVSSRLRNU(NAME, BIT, E1, E2, E3)                                     \
void HELPER(NAME)(CPULoongArchState *env,                                    \
                  uint32_t xd, uint32_t xj, uint32_t xk)                     \
{                                                                            \
    int i, max;                                                              \
    XReg *Xd = &(env->fpr[xd].xreg);                                         \
    XReg *Xj = &(env->fpr[xj].xreg);                                         \
    XReg *Xk = &(env->fpr[xk].xreg);                                         \
                                                                             \
    max = LASX_LEN / (BIT * 2);                                              \
    for (i = 0; i < max; i++) {                                              \
        Xd->E1(i) = do_xssrlrnu_ ## E1(Xj->E2(i), Xk->E3(i) % BIT, BIT / 2); \
        Xd->E1(i + max * 2) = do_xssrlrnu_## E1(Xj->E2(i + max),             \
                                                Xk->E3(i + max) % BIT,       \
                                                BIT / 2);                    \
    }                                                                        \
    Xd->XD(1) = 0;                                                           \
    Xd->XD(3) = 0;                                                           \
}

XVSSRLRNU(xvssrlrn_bu_h, 16, XB, XH, UXH)
XVSSRLRNU(xvssrlrn_hu_w, 32, XH, XW, UXW)
XVSSRLRNU(xvssrlrn_wu_d, 64, XW, XD, UXD)

#define XSSRARNU(E1, E2, T1, T2, T3)                \
static T1 do_xssrarnu_ ## E1(T3 e2, int sa, int sh) \
{                                                   \
    T1 shft_res;                                    \
                                                    \
    if (e2 < 0) {                                   \
        shft_res = 0;                               \
    } else {                                        \
        shft_res = do_xvsrar_ ## E2(e2, sa);        \
    }                                               \
    T2 mask;                                        \
    mask = (1ull << sh) - 1;                        \
    if (shft_res > mask) {                          \
        return mask;                                \
    } else {                                        \
        return shft_res;                            \
    }                                               \
}

XSSRARNU(XB, XH, uint16_t, uint8_t, int16_t)
XSSRARNU(XH, XW, uint32_t, uint16_t, int32_t)
XSSRARNU(XW, XD, uint64_t, uint32_t, int64_t)

#define XVSSRARNU(NAME, BIT, E1, E2, E3)                                     \
void HELPER(NAME)(CPULoongArchState *env,                                    \
                  uint32_t xd, uint32_t xj, uint32_t xk)                     \
{                                                                            \
    int i, max;                                                              \
    XReg *Xd = &(env->fpr[xd].xreg);                                         \
    XReg *Xj = &(env->fpr[xj].xreg);                                         \
    XReg *Xk = &(env->fpr[xk].xreg);                                         \
                                                                             \
    max = LASX_LEN / (BIT * 2);                                              \
    for (i = 0; i < max; i++) {                                              \
        Xd->E1(i) = do_xssrarnu_ ## E1(Xj->E2(i), Xk->E3(i) % BIT, BIT / 2); \
        Xd->E1(i + max * 2) = do_xssrarnu_## E1(Xj->E2(i + max),             \
                                                Xk->E3(i + max) % BIT,       \
                                                BIT / 2);                    \
    }                                                                        \
    Xd->XD(1) = 0;                                                           \
    Xd->XD(3) = 0;                                                           \
}

XVSSRARNU(xvssrarn_bu_h, 16, XB, XH, UXH)
XVSSRARNU(xvssrarn_hu_w, 32, XH, XW, UXW)
XVSSRARNU(xvssrarn_wu_d, 64, XW, XD, UXD)

#define XVSSRLRNI(NAME, BIT, E1, E2)                                          \
void HELPER(NAME)(CPULoongArchState *env,                                     \
                  uint32_t xd, uint32_t xj, uint32_t imm)                     \
{                                                                             \
    int i, max;                                                               \
    XReg temp;                                                                \
    XReg *Xd = &(env->fpr[xd].xreg);                                          \
    XReg *Xj = &(env->fpr[xj].xreg);                                          \
                                                                              \
    max = LASX_LEN / (BIT * 2);                                               \
    for (i = 0; i < max; i++) {                                               \
        temp.E1(i) = do_xssrlrns_ ## E1(Xj->E2(i), imm, (BIT / 2) - 1);       \
        temp.E1(i + max) = do_xssrlrns_ ## E1(Xd->E2(i), imm, (BIT / 2) - 1); \
        temp.E1(i + max * 2) = do_xssrlrns_## E1(Xj->E2(i + max),             \
                                                 imm, (BIT / 2) - 1);         \
        temp.E1(i + max * 3) = do_xssrlrns_## E1(Xd->E2(i + max),             \
                                                 imm, (BIT / 2) - 1);         \
    }                                                                         \
    *Xd = temp;                                                               \
}

#define XVSSRLRNI_Q(NAME, sh)                                                  \
void HELPER(NAME)(CPULoongArchState *env,                                      \
                          uint32_t xd, uint32_t xj, uint32_t imm)              \
{                                                                              \
    int i;                                                                     \
    Int128 shft_res[4], r[4], mask;                                            \
    XReg *Xd = &(env->fpr[xd].xreg);                                           \
    XReg *Xj = &(env->fpr[xj].xreg);                                           \
                                                                               \
    if (imm == 0) {                                                            \
        shft_res[0] = Xj->XQ(0);                                               \
        shft_res[1] = Xd->XQ(0);                                               \
        shft_res[2] = Xj->XQ(1);                                               \
        shft_res[3] = Xd->XQ(1);                                               \
    } else {                                                                   \
        r[0] = int128_and(int128_urshift(Xj->XQ(0), (imm - 1)), int128_one()); \
        r[1] = int128_and(int128_urshift(Xd->XQ(0), (imm - 1)), int128_one()); \
        r[2] = int128_and(int128_urshift(Xj->XQ(1), (imm - 1)), int128_one()); \
        r[3] = int128_and(int128_urshift(Xd->XQ(1), (imm - 1)), int128_one()); \
                                                                               \
        shft_res[0] = (int128_add(int128_urshift(Xj->XQ(0), imm), r[0]));      \
        shft_res[1] = (int128_add(int128_urshift(Xd->XQ(0), imm), r[1]));      \
        shft_res[2] = (int128_add(int128_urshift(Xj->XQ(1), imm), r[2]));      \
        shft_res[3] = (int128_add(int128_urshift(Xd->XQ(1), imm), r[3]));      \
    }                                                                          \
                                                                               \
    mask = int128_sub(int128_lshift(int128_one(), sh), int128_one());          \
                                                                               \
    for (i = 0; i < 4; i++) {                                                  \
        if (int128_ult(mask, shft_res[i])) {                                   \
            Xd->XD(i) = int128_getlo(mask);                                    \
        } else {                                                               \
            Xd->XD(i) = int128_getlo(shft_res[i]);                             \
        }                                                                      \
    }                                                                          \
}

XVSSRLRNI(xvssrlrni_b_h, 16, XB, XH)
XVSSRLRNI(xvssrlrni_h_w, 32, XH, XW)
XVSSRLRNI(xvssrlrni_w_d, 64, XW, XD)
XVSSRLRNI_Q(xvssrlrni_d_q, 63)

#define XVSSRARNI(NAME, BIT, E1, E2)                                          \
void HELPER(NAME)(CPULoongArchState *env,                                     \
                  uint32_t xd, uint32_t xj, uint32_t imm)                     \
{                                                                             \
    int i, max;                                                               \
    XReg temp;                                                                \
    XReg *Xd = &(env->fpr[xd].xreg);                                          \
    XReg *Xj = &(env->fpr[xj].xreg);                                          \
                                                                              \
    max = LASX_LEN / (BIT * 2);                                               \
    for (i = 0; i < max; i++) {                                               \
        temp.E1(i) = do_xssrarns_ ## E1(Xj->E2(i), imm, (BIT / 2) -  1);      \
        temp.E1(i + max) = do_xssrarns_ ## E1(Xd->E2(i), imm, (BIT / 2) - 1); \
        temp.E1(i + max * 2) = do_xssrarns_## E1(Xj->E2(i + max),             \
                                                 imm, (BIT / 2) - 1);         \
        temp.E1(i + max * 3) = do_xssrarns_## E1(Xd->E2(i + max),             \
                                                 imm, (BIT / 2) - 1);         \
    }                                                                         \
    *Xd = temp;                                                               \
}

void HELPER(xvssrarni_d_q)(CPULoongArchState *env,
                           uint32_t xd, uint32_t xj, uint32_t imm)
{
    int i;
    Int128 shft_res[4], r[4], mask1, mask2;
    XReg *Xd = &(env->fpr[xd].xreg);
    XReg *Xj = &(env->fpr[xj].xreg);

    if (imm == 0) {
        shft_res[0] = Xj->XQ(0);
        shft_res[1] = Xd->XQ(0);
        shft_res[2] = Xj->XQ(1);
        shft_res[3] = Xd->XQ(1);
    } else {
        r[0] = int128_and(int128_rshift(Xj->XQ(0), (imm - 1)), int128_one());
        r[1] = int128_and(int128_rshift(Xd->XQ(0), (imm - 1)), int128_one());
        r[2] = int128_and(int128_rshift(Xj->XQ(1), (imm - 1)), int128_one());
        r[3] = int128_and(int128_rshift(Xd->XQ(1), (imm - 1)), int128_one());

        shft_res[0] = int128_add(int128_rshift(Xj->XQ(0), imm), r[0]);
        shft_res[1] = int128_add(int128_rshift(Xd->XQ(0), imm), r[1]);
        shft_res[2] = int128_add(int128_rshift(Xj->XQ(1), imm), r[2]);
        shft_res[3] = int128_add(int128_rshift(Xd->XQ(1), imm), r[3]);
    }

    mask1 = int128_sub(int128_lshift(int128_one(), 63), int128_one());
    mask2  = int128_lshift(int128_one(), 63);

    for (i = 0; i < 4; i++) {
        if (int128_gt(shft_res[i],  mask1)) {
            Xd->XD(i) = int128_getlo(mask1);
        } else if (int128_lt(shft_res[i], int128_neg(mask2))) {
            Xd->XD(i) = int128_getlo(mask2);
        } else {
            Xd->XD(i) = int128_getlo(shft_res[i]);
        }
    }
}

XVSSRARNI(xvssrarni_b_h, 16, XB, XH)
XVSSRARNI(xvssrarni_h_w, 32, XH, XW)
XVSSRARNI(xvssrarni_w_d, 64, XW, XD)

#define XVSSRLRNUI(NAME, BIT, E1, E2)                                   \
void HELPER(NAME)(CPULoongArchState *env,                               \
                  uint32_t xd, uint32_t xj, uint32_t imm)               \
{                                                                       \
    int i, max;                                                         \
    XReg temp;                                                          \
    XReg *Xd = &(env->fpr[xd].xreg);                                    \
    XReg *Xj = &(env->fpr[xj].xreg);                                    \
                                                                        \
    max = LASX_LEN / (BIT * 2);                                         \
    for (i = 0; i < max; i++) {                                         \
        temp.E1(i) = do_xssrlrnu_ ## E1(Xj->E2(i), imm, BIT / 2);       \
        temp.E1(i + max) = do_xssrlrnu_ ## E1(Xd->E2(i), imm, BIT / 2); \
        temp.E1(i + max * 2) = do_xssrlrnu_## E1(Xj->E2(i + max),       \
                                                 imm, BIT / 2);         \
        temp.E1(i + max * 3) = do_xssrlrnu_## E1(Xd->E2(i + max),       \
                                                 imm, BIT / 2);         \
    }                                                                   \
    *Xd = temp;                                                         \
}

XVSSRLRNUI(xvssrlrni_bu_h, 16, XB, XH)
XVSSRLRNUI(xvssrlrni_hu_w, 32, XH, XW)
XVSSRLRNUI(xvssrlrni_wu_d, 64, XW, XD)
XVSSRLRNI_Q(xvssrlrni_du_q, 64)

#define XVSSRARNUI(NAME, BIT, E1, E2)                                   \
void HELPER(NAME)(CPULoongArchState *env,                               \
                  uint32_t xd, uint32_t xj, uint32_t imm)               \
{                                                                       \
    int i, max;                                                         \
    XReg temp;                                                          \
    XReg *Xd = &(env->fpr[xd].xreg);                                    \
    XReg *Xj = &(env->fpr[xj].xreg);                                    \
                                                                        \
    max = LASX_LEN / (BIT * 2);                                         \
    for (i = 0; i < max; i++) {                                         \
        temp.E1(i) = do_xssrarnu_ ## E1(Xj->E2(i), imm, BIT / 2);       \
        temp.E1(i + max) = do_xssrarnu_ ## E1(Xd->E2(i), imm, BIT / 2); \
        temp.E1(i + max * 2) = do_xssrarnu_## E1(Xj->E2(i + max),       \
                                                 imm, BIT / 2);         \
        temp.E1(i + max * 3) = do_xssrarnu_## E1(Xd->E2(i + max),       \
                                                 imm, BIT / 2);         \
    }                                                                   \
    *Xd = temp;                                                         \
}

void HELPER(xvssrarni_du_q)(CPULoongArchState *env,
                            uint32_t xd, uint32_t xj, uint32_t imm)
{
    int i;
    Int128 shft_res[4], r[4], mask1, mask2;
    XReg *Xd = &(env->fpr[xd].xreg);
    XReg *Xj = &(env->fpr[xj].xreg);

    if (imm == 0) {
        shft_res[0] = Xj->XQ(0);
        shft_res[1] = Xd->XQ(0);
        shft_res[2] = Xj->XQ(1);
        shft_res[3] = Xd->XQ(1);
    } else {
        r[0] = int128_and(int128_rshift(Xj->XQ(0), (imm - 1)), int128_one());
        r[1] = int128_and(int128_rshift(Xd->XQ(0), (imm - 1)), int128_one());
        r[2] = int128_and(int128_rshift(Xj->XQ(1), (imm - 1)), int128_one());
        r[3] = int128_and(int128_rshift(Xd->XQ(1), (imm - 1)), int128_one());

        shft_res[0] = int128_add(int128_rshift(Xj->XQ(0), imm), r[0]);
        shft_res[1] = int128_add(int128_rshift(Xd->XQ(0), imm), r[1]);
        shft_res[2] = int128_add(int128_rshift(Xj->XQ(1), imm), r[2]);
        shft_res[3] = int128_add(int128_rshift(Xd->XQ(1), imm), r[3]);
    }

    if (int128_lt(Xj->XQ(0), int128_zero())) {
        shft_res[0] = int128_zero();
    }
    if (int128_lt(Xd->XQ(0), int128_zero())) {
        shft_res[1] = int128_zero();
    }
    if (int128_lt(Xj->XQ(1), int128_zero())) {
        shft_res[2] = int128_zero();
    }
    if (int128_lt(Xd->XQ(1), int128_zero())) {
        shft_res[3] = int128_zero();
    }

    mask1 = int128_sub(int128_lshift(int128_one(), 64), int128_one());
    mask2  = int128_lshift(int128_one(), 64);

    for (i = 0; i < 4; i++) {
        if (int128_gt(shft_res[i],  mask1)) {
            Xd->XD(i) = int128_getlo(mask1);
        } else if (int128_lt(shft_res[i], int128_neg(mask2))) {
            Xd->XD(i) = int128_getlo(mask2);
        } else {
            Xd->XD(i) = int128_getlo(shft_res[i]);
        }
    }
}

XVSSRARNUI(xvssrarni_bu_h, 16, XB, XH)
XVSSRARNUI(xvssrarni_hu_w, 32, XH, XW)
XVSSRARNUI(xvssrarni_wu_d, 64, XW, XD)

#define XDO_2OP(NAME, BIT, E, DO_OP)                                \
void HELPER(NAME)(CPULoongArchState *env, uint32_t xd, uint32_t xj) \
{                                                                   \
    int i;                                                          \
    XReg *Xd = &(env->fpr[xd].xreg);                                \
    XReg *Xj = &(env->fpr[xj].xreg);                                \
                                                                    \
    for (i = 0; i < LASX_LEN / BIT; i++) {                          \
        Xd->E(i) = DO_OP(Xj->E(i));                                 \
    }                                                               \
}

XDO_2OP(xvclo_b, 8, UXB, DO_CLO_B)
XDO_2OP(xvclo_h, 16, UXH, DO_CLO_H)
XDO_2OP(xvclo_w, 32, UXW, DO_CLO_W)
XDO_2OP(xvclo_d, 64, UXD, DO_CLO_D)
XDO_2OP(xvclz_b, 8, UXB, DO_CLZ_B)
XDO_2OP(xvclz_h, 16, UXH, DO_CLZ_H)
XDO_2OP(xvclz_w, 32, UXW, DO_CLZ_W)
XDO_2OP(xvclz_d, 64, UXD, DO_CLZ_D)

#define XVPCNT(NAME, BIT, E, FN)                                    \
void HELPER(NAME)(CPULoongArchState *env, uint32_t xd, uint32_t xj) \
{                                                                   \
    int i;                                                          \
    XReg *Xd = &(env->fpr[xd].xreg);                                \
    XReg *Xj = &(env->fpr[xj].xreg);                                \
                                                                    \
    for (i = 0; i < LASX_LEN / BIT; i++) {                          \
        Xd->E(i) = FN(Xj->E(i));                                    \
    }                                                               \
}

XVPCNT(xvpcnt_b, 8, UXB, ctpop8)
XVPCNT(xvpcnt_h, 16, UXH, ctpop16)
XVPCNT(xvpcnt_w, 32, UXW, ctpop32)
XVPCNT(xvpcnt_d, 64, UXD, ctpop64)
