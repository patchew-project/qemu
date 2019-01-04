#ifndef HW_INPUT_GAMEPAD_H
#define HW_INPUT_GAMEPAD_H

/* Gamepad devices that have nowhere better to go.  */

#include "hw/irq.h"

/* stellaris_input.c */
void stellaris_gamepad_init(int n, qemu_irq *irq, const int *keycode);

#endif
