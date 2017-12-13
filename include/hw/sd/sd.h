/*
 * SD Memory Card emulation.  Mostly correct for MMC too.
 *
 * Copyright (c) 2006 Andrzej Zaborowski  <balrog@zabor.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef HW_SD_H
#define HW_SD_H

#include "hw/qdev.h"

typedef struct {
    uint8_t cmd;        /*  6 bits */
    uint32_t arg;       /* 32 bits */
    uint8_t crc;        /*  7 bits */
} SDRequest;     /* total: 48 bits shifted */

typedef struct SDBus SDBus;

#define TYPE_SD_CARD "sd-card"
#define SD_CARD(obj) OBJECT_CHECK(SDState, (obj), TYPE_SD_CARD)

#define TYPE_SD_BUS "sd-bus"
#define SD_BUS(obj) OBJECT_CHECK(SDBus, (obj), TYPE_SD_BUS)

struct SDBus {
    BusState qbus;
};

/* Functions to be used by qdevified callers */
int sdbus_do_command(SDBus *sd, SDRequest *req, uint8_t *response);
void sdbus_write_data(SDBus *sd, uint8_t value);
uint8_t sdbus_read_data(SDBus *sd);
bool sdbus_data_ready(SDBus *sd);
bool sdbus_get_inserted(SDBus *sd);
bool sdbus_get_readonly(SDBus *sd);
/**
 * sdbus_reparent_card: Reparent an SD card from one controller to another
 * @from: controller bus to remove card from
 * @to: controller bus to move card to
 *
 * Reparent an SD card, effectively unplugging it from one controller
 * and inserting it into another. This is useful for SoCs like the
 * bcm2835 which have two SD controllers and connect a single SD card
 * to them, selected by the guest reprogramming GPIO line routing.
 */
void sdbus_reparent_card(SDBus *from, SDBus *to);

/* Functions to be used by SD devices to report back to qdevified controllers */
void sdbus_set_inserted(SDBus *sd, bool inserted);
void sdbus_set_readonly(SDBus *sd, bool readonly);

#endif /* HW_SD_H */
