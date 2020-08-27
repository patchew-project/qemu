/*
 * Renesas Multi-function Timer Uint Object
 *
 * Copyright (c) 2020 Yoshinori Sato
 *
 * This code is licensed under the GPL version 2 or later.
 *
 */

#ifndef HW_RENESAS_MTU_H
#define HW_RENESAS_MTU_H

#include "hw/sysbus.h"
#include "hw/qdev-clock.h"

#define TYPE_RENESAS_MTU2 "renesas-mtu2"
#define RenesasMTU2(obj) \
    OBJECT_CHECK(RenesasMTU2State, (obj), TYPE_RENESAS_MTU2)

#define MTU2Class(klass) \
    OBJECT_CLASS_CHECK(RenesasMTU2Class, klass, TYPE_RENESAS_MTU2)

enum {
    NR_MAX_IRQ = 7,
    MTU_NR_IRQ = 7 + 4 + 4 + 5 + 5 + 3,
};

struct RenesasMTU2State;

typedef struct {
    uint8_t tcr;
    uint8_t tmdr;
    uint8_t tsr;
    uint16_t tior;
    uint16_t tier;
    uint32_t tcnt;
    uint16_t tgr[6];

    int num_gr;
    int64_t base;
    int64_t next;
    int64_t clk;
    bool start;
    bool cntclr;
    bool ier;
    QEMUTimer *timer;
    int ch;
    qemu_irq irq[NR_MAX_IRQ];
    int next_cnt;
    struct RenesasMTU2State *mtu;
} RenesasMTURegs;

typedef struct RenesasMTU2State {
    SysBusDevice parent_obj;
    RenesasMTURegs r[5];
    RenesasMTURegs r5[3];
    uint8_t tbtm;
    uint8_t ticcr;
    uint16_t tadcr;
    uint16_t tadcor[2];
    uint16_t tadcobr[2];
    /* CH A registers */
    uint8_t toer;
    uint8_t tgcr;
    uint8_t tocr[2];
    uint16_t tcdr;
    uint16_t tddr;
    uint16_t tcnts;
    uint16_t tcbr;
    uint8_t titcr;
    uint8_t titcnt;
    uint8_t tbter;
    uint8_t tder;
    uint8_t tolbr;
    uint8_t twcr;
    uint8_t trwer;
    uint8_t tsyr;

    Clock *pck;
    int64_t input_freq;
    MemoryRegion memory[3];
    uint8_t trwer_r;
    uint32_t unit;
} RenesasMTU2State;

typedef struct {
    SysBusDeviceClass parent;
} RenesasMTU2Class;

#endif
