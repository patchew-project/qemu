/*
 * LED Matrix Demultiplexer
 *
 * Copyright 2018 Steffen Görtz <contrib@steffen-goertz.de>
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */
#ifndef LED_MATRIX_H
#define LED_MATRIX_H

#include "hw/sysbus.h"
#include "qemu/timer.h"
#define TYPE_LED_MATRIX "led_matrix"
#define LED_MATRIX(obj) OBJECT_CHECK(LEDMatrixState, (obj), TYPE_LED_MATRIX)

typedef struct LEDMatrixState {
    SysBusDevice parent_obj;

    QemuConsole *con;
    bool redraw;

    uint32_t refresh_period; /* refresh period in ms */
    uint8_t nrows;
    uint8_t ncols;
    bool strobe_row;

    QEMUTimer timer;
    int64_t timestamp;

    uint64_t row;
    uint64_t col;
    int64_t *led_working_dc; /* Current LED duty cycle acquisition */
    int64_t *led_frame_dc; /* Last complete LED duty cycle acquisition */
} LEDMatrixState;


#endif
