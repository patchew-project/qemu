/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * vscsibig.S -- guest-free reproducer for defect 7:
 *   spapr_vscsi silently drops data on any transfer larger than 128 KiB.
 *
 * struct vscsi_req::cur_desc_offset is uint16_t.  scsi-disk delivers a
 * command in SCSI_DMA_BUF_SIZE (128 KiB) chunks, so vscsi_srp_direct_data()
 * does
 *     req->cur_desc_offset += 0x20000;      /  0x20000 & 0xFFFF == 0  /
 * and wraps back to zero: every chunk after the first is DMA'd over the
 * START of the guest buffer.  The command still completes with status 0 and
 * no sense data.
 *
 * This payload speaks CRQ + SRP directly -- no guest OS, no SLOF client
 * interface. QEMU's vscsi_process_srp_iu() dispatches SRP_CMD without
 * requiring a prior LOGIN, and SPAPR_VIO_LIOBN(reg) == reg, so the whole
 * thing is: free any CRQ SLOF left behind, register our own, identity-map
 * the pages we touch with H_PUT_TCE, clear the power-on UNIT ATTENTION with
 * a TEST UNIT READY, then send one READ_10 of 512 blocks (= 256 KiB)
 * described by a single DIRECT descriptor.
 *
 * The backing disk is written by mkvscsidisk.py so that sector s starts with
 * the big-endian word 0xA5000000|s.  Therefore:
 *
 *   +0x30  data[0]        RED 0xa5000100 (the 2nd chunk landed at offset 0)
 *                       GREEN 0xa5000000 (sector 0, where it belongs)
 *   +0x38  data[0x20000]  RED 0xeeeeeeee (poison -- never written at all)
 *                       GREEN 0xa5000100 (sector 256)
 *
 * Poison rather than zero matters: it separates "the DMA never happened"
 * from "the disk really holds zeros", which is exactly the distinction that
 * cost two measurement rounds when this was first chased through AIX.
 *
 * Results at physical 0x01000000; read with the monitor: x /10xg 0x1000000
 *   +0x00  H_REG_CRQ return code (0 = registered)
 *   +0x08  the VIO unit address that answered (0x71000000 + n)
 *   +0x10  return code of the last H_PUT_TCE
 *   +0x18  H_SEND_CRQ return code for the READ
 *   +0x20  poll iterations spent waiting for the READ response
 *          (0x04000000 = the cap: it timed out, all below is void)
 *   +0x28  SRP RSP status byte of the READ  -- must be 0; 2 = CHECK CONDITION
 *   +0x30  data[0]
 *   +0x38  data[0x20000]
 *   +0x40  SRP RSP status byte of the TEST UNIT READY (2 is normal: it eats the
 *          power-on UNIT ATTENTION, which is exactly why it is sent first)
 *   +0x48  poll iterations spent waiting for the TEST UNIT READY response
 *
 * Physical map: payload 0x00400000 (relocated by spapr, harmless -- every
 * address here is absolute), results 0x01000000, CRQ queue 0x02000000, TUR
 * IU 0x02002000, READ IU 0x02001000, data buffer 0x02100000 + 256 KiB.  Run
 * at -m 512 or more.
 */
    .section .text
    .globl  _start
_start:
    /* r11 = result buffer  0x01000000      */
    lis     11, 0x0100
    /* r20 = CRQ queue      0x02000000      */
    lis     20, 0x0200
    /* r21 = READ IU        0x02001000      */
    addi    21, 20, 0x1000
    /* r19 = TUR IU         0x02002000      */
    addi    19, 20, 0x2000
    /* r22 = data buffer    0x02100000      */
    lis     22, 0x0210

    /* ---- clear the result buffer (80 bytes) ---- */
    li      0, 0
    li      9, 0
1:  stdx    0, 11, 9
    addi    9, 9, 8
    cmpwi   9, 80
    blt     1b

    /* ---- clear the CRQ queue page: we poll it, so it must start empty ---- */
    li      0, 0
    li      9, 0
2:  stdx    0, 20, 9
    addi    9, 9, 8
    cmpwi   9, 0x1000
    blt     2b

    /* ---- poison the whole 256 KiB data buffer ---- */
    lis     8, 0xEEEE
    ori     8, 8, 0xEEEE
    li      9, 0
    /* 0x00040000 bytes                     */
    lis     10, 0x0004
3:  stwx    8, 22, 9
    addi    9, 9, 4
    cmpw    9, 10
    blt     3b

    /* ---- find the vSCSI unit: H_FREE_CRQ, then H_REG_CRQ ----
     * H_REG_CRQ gives H_PARAMETER for a unit that does not exist,
     * H_NOT_FOUND for one without a CRQ (a vty), so the first success
     * is the vSCSI adapter.  The unit that answered is recorded, so a
     * wrong guess is visible rather than silent. */
    /* candidate unit 0x71000000            */
    lis     23, 0x7100
    li      24, 0
/* H_FREE_CRQ                           */
4:  li      3, 0x100
    mr      4, 23
    sc      1
    /* H_REG_CRQ                            */
    li      3, 0xFC
    mr      4, 23
    /* queue address (IOVA)                 */
    mr      5, 20
    li      6, 0
    /* queue length 4096                    */
    ori     6, 6, 0x1000
    sc      1
    cmpdi   3, 0
    beq     5f
    addi    23, 23, 1
    addi    24, 24, 1
    cmpwi   24, 8
    blt     4b
    /* no unit answered -- record and stop  */
    std     3, 0(11)
    b       99f
/* +0x00 H_REG_CRQ rc                   */
5:  std     3, 0(11)
    /* +0x08 unit that answered             */
    std     23, 8(11)

    /* ---- identity-map 0x02000000..0x02200000 into the TCE window ----
     * SPAPR_VIO_LIOBN(reg) == reg, page shift 12, window base 0,
     * so ioba == phys. */
    lis     25, 0x0200
    li      26, 0
/* H_PUT_TCE                            */
6:  li      3, 0x20
    /* liobn == unit                        */
    mr      4, 23
    /* ioba                                 */
    mr      5, 25
    /* tce = page | read | write            */
    ori     6, 25, 3
    sc      1
    addi    25, 25, 0x1000
    addi    26, 26, 1
    cmpwi   26, 512
    blt     6b
    /* +0x10 last H_PUT_TCE rc              */
    std     3, 16(11)

    /* ================= 1. TEST UNIT READY, to eat the UNIT ATTENTION =========
     * A freshly reset SCSI device answers its first command with
     * sense key 0x06 / ASC 0x29 (power on, reset or bus device reset).  Without
     * it the READ below fails with CHECK CONDITION and transfers
     * nothing. */
    li      0, 0
    li      9, 0
7:  stdx    0, 19, 9
    addi    9, 9, 8
    cmpwi   9, 48
    blt     7b
    li      0, 0x02
    /* opcode = SRP_CMD                     */
    stb     0, 0(19)
                                    /* buf_fmt 0 = no data          */
    lis     0, 0x8000
    /* lun = 0x8000000000000000             */
    stw     0, 20(19)
                                    /* cdb[0] +32 = TEST UNIT READY */
    /* H_SEND_CRQ (0x104 is H_VIO_SIGNAL!)  */
    li      3, 0x108
    mr      4, 23
    lis     5, 0x8001
    sldi    5, 5, 32
    /* 80 01 00 00 0000 0030 (IU_length 48) */
    ori     5, 5, 0x0030
    mr      6, 19
    sc      1

    li      27, 0
    /* poll cap ~67M                        */
    lis     28, 0x0400
/* response lands in queue slot 0       */
8:  lbz     0, 0(20)
    cmpwi   0, 0
    bne     9f
    addi    27, 27, 1
    cmpd    27, 28
    blt     8b
/* +0x48 TUR poll iterations            */
9:  std     27, 72(11)
    /* srp_rsp.status                       */
    lbz     0, 19(19)
    /* +0x40 TUR status (2 = the UA)        */
    std     0, 64(11)

    /* ============ 2. the READ_10 that reproduces the defect ======== */
    li      0, 0
    li      9, 0
10: stdx    0, 21, 9
    addi    9, 9, 8
    cmpwi   9, 64
    blt     10b

    li      0, 0x02
    /* opcode = SRP_CMD                     */
    stb     0, 0(21)
    li      0, 0x01
    /* buf_fmt: data-in = DIRECT            */
    stb     0, 5(21)
    li      0, 0
    /* data_out_desc_cnt = 0                */
    stb     0, 6(21)
    li      0, 1
    /* data_in_desc_cnt  = 1                */
    stb     0, 7(21)
    lis     0, 0x1122
    ori     0, 0, 0x3344
    /* tag (any)                            */
    stw     0, 8(21)
    lis     0, 0x5566
    ori     0, 0, 0x7788
    stw     0, 12(21)
    lis     0, 0x8000
    /* lun = 0x8000000000000000 (SAM-5 LU)  */
    stw     0, 20(21)
    li      0, 0
    stw     0, 24(21)
                                    /* add_cdb_len at +31 stays 0   */
    li      0, 0x28
    /* cdb[0] = READ(10)                    */
    stb     0, 32(21)
    li      0, 0x02
    /* cdb[7..8] = 0x0200 = 512 blocks      */
    stb     0, 39(21)

    /* srp_direct_buf { va, key, len } at add_data (+48) */
    li      0, 0
    stw     0, 48(21)
    /* va  = 0x02100000                     */
    stw     22, 52(21)
    /* key = 0                              */
    stw     0, 56(21)
    lis     0, 0x0004
    /* len = 0x00040000 = 256 KiB           */
    stw     0, 60(21)

    /* H_SEND_CRQ                           */
    li      3, 0x108
    mr      4, 23
    lis     5, 0x8001
    sldi    5, 5, 32
    /* 80 01 00 00 0000 0040 (IU_length 64) */
    ori     5, 5, 0x0040
    mr      6, 21
    sc      1
    /* +0x18 H_SEND_CRQ rc                  */
    std     3, 24(11)

    /* the TUR response took slot 0, so this one lands in slot 1 (offset 16) */
    addi    29, 20, 16
    li      27, 0
    lis     28, 0x0400
11: lbz     0, 0(29)
    cmpwi   0, 0
    bne     12f
    addi    27, 27, 1
    cmpd    27, 28
    blt     11b
/* +0x20 READ poll iterations           */
12: std     27, 32(11)
    /* srp_rsp.status -- must be 0          */
    lbz     0, 19(21)
    /* +0x28                                */
    std     0, 40(11)

    /* ---- the two witnesses ---- */
    lwz     0, 0(22)
    /* +0x30 data[0]                        */
    std     0, 48(11)
    lis     8, 0x0002
    lwzx    0, 22, 8
    /* +0x38 data[0x20000]                  */
    std     0, 56(11)

99: b       99b
