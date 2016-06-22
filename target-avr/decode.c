/*
 * QEMU AVR CPU
 *
 * Copyright (c) 2016 Michael Rolnik
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */


#include <stdint.h>
#include "translate.h"

void avr_decode(uint32_t pc, uint32_t *l, uint32_t c, translate_function_t *t)
{
    uint32_t opc  = extract32(c, 0, 16);
    switch (opc & 0x0000d000) {
        case 0x00000000: {
            switch (opc & 0x00002c00) {
                case 0x00000000: {
                    switch (opc & 0x00000300) {
                        case 0x00000000: {
                            *l = 16;
                            *t = &avr_translate_NOP;
                            break;
                        }
                        case 0x00000100: {
                            *l = 16;
                            *t = &avr_translate_MOVW;
                            break;
                        }
                        case 0x00000200: {
                            *l = 16;
                            *t = &avr_translate_MULS;
                            break;
                        }
                        case 0x00000300: {
                            switch (opc & 0x00000088) {
                                case 0x00000000: {
                                    *l = 16;
                                    *t = &avr_translate_MULSU;
                                    break;
                                }
                                case 0x00000008: {
                                    *l = 16;
                                    *t = &avr_translate_FMUL;
                                    break;
                                }
                                case 0x00000080: {
                                    *l = 16;
                                    *t = &avr_translate_FMULS;
                                    break;
                                }
                                case 0x00000088: {
                                    *l = 16;
                                    *t = &avr_translate_FMULSU;
                                    break;
                                }
                            }
                            break;
                        }
                    }
                    break;
                }
                case 0x00000400: {
                    *l = 16;
                    *t = &avr_translate_CPC;
                    break;
                }
                case 0x00000800: {
                    *l = 16;
                    *t = &avr_translate_SBC;
                    break;
                }
                case 0x00000c00: {
                    *l = 16;
                    *t = &avr_translate_ADD;
                    break;
                }
                case 0x00002000: {
                    *l = 16;
                    *t = &avr_translate_AND;
                    break;
                }
                case 0x00002400: {
                    *l = 16;
                    *t = &avr_translate_EOR;
                    break;
                }
                case 0x00002800: {
                    *l = 16;
                    *t = &avr_translate_OR;
                    break;
                }
                case 0x00002c00: {
                    *l = 16;
                    *t = &avr_translate_MOV;
                    break;
                }
            }
            break;
        }
        case 0x00001000: {
            switch (opc & 0x00002000) {
                case 0x00000000: {
                    switch (opc & 0x00000c00) {
                        case 0x00000000: {
                            *l = 16;
                            *t = &avr_translate_CPSE;
                            break;
                        }
                        case 0x00000400: {
                            *l = 16;
                            *t = &avr_translate_CP;
                            break;
                        }
                        case 0x00000800: {
                            *l = 16;
                            *t = &avr_translate_SUB;
                            break;
                        }
                        case 0x00000c00: {
                            *l = 16;
                            *t = &avr_translate_ADC;
                            break;
                        }
                    }
                    break;
                }
                case 0x00002000: {
                    *l = 16;
                    *t = &avr_translate_CPI;
                    break;
                }
            }
            break;
        }
        case 0x00004000: {
            switch (opc & 0x00002000) {
                case 0x00000000: {
                    *l = 16;
                    *t = &avr_translate_SBCI;
                    break;
                }
                case 0x00002000: {
                    *l = 16;
                    *t = &avr_translate_ORI;
                    break;
                }
            }
            break;
        }
        case 0x00005000: {
            switch (opc & 0x00002000) {
                case 0x00000000: {
                    *l = 16;
                    *t = &avr_translate_SUBI;
                    break;
                }
                case 0x00002000: {
                    *l = 16;
                    *t = &avr_translate_ANDI;
                    break;
                }
            }
            break;
        }
        case 0x00008000: {
            switch (opc & 0x00000208) {
                case 0x00000000: {
                    *l = 16;
                    *t = &avr_translate_LDDZ;
                    break;
                }
                case 0x00000008: {
                    *l = 16;
                    *t = &avr_translate_LDDY;
                    break;
                }
                case 0x00000200: {
                    *l = 16;
                    *t = &avr_translate_STDZ;
                    break;
                }
                case 0x00000208: {
                    *l = 16;
                    *t = &avr_translate_STDY;
                    break;
                }
            }
            break;
        }
        case 0x00009000: {
            switch (opc & 0x00002800) {
                case 0x00000000: {
                    switch (opc & 0x00000600) {
                        case 0x00000000: {
                            switch (opc & 0x0000000f) {
                                case 0x00000000: {
                                    *l = 32;
                                    *t = &avr_translate_LDS;
                                    break;
                                }
                                case 0x00000001: {
                                    *l = 16;
                                    *t = &avr_translate_LDZ2;
                                    break;
                                }
                                case 0x00000002: {
                                    *l = 16;
                                    *t = &avr_translate_LDZ3;
                                    break;
                                }
                                case 0x00000003: {
                                    break;
                                }
                                case 0x00000004: {
                                    *l = 16;
                                    *t = &avr_translate_LPM2;
                                    break;
                                }
                                case 0x00000005: {
                                    *l = 16;
                                    *t = &avr_translate_LPMX;
                                    break;
                                }
                                case 0x00000006: {
                                    *l = 16;
                                    *t = &avr_translate_ELPM2;
                                    break;
                                }
                                case 0x00000007: {
                                    *l = 16;
                                    *t = &avr_translate_ELPMX;
                                    break;
                                }
                                case 0x00000008: {
                                    break;
                                }
                                case 0x00000009: {
                                    *l = 16;
                                    *t = &avr_translate_LDY2;
                                    break;
                                }
                                case 0x0000000a: {
                                    *l = 16;
                                    *t = &avr_translate_LDY3;
                                    break;
                                }
                                case 0x0000000b: {
                                    break;
                                }
                                case 0x0000000c: {
                                    *l = 16;
                                    *t = &avr_translate_LDX1;
                                    break;
                                }
                                case 0x0000000d: {
                                    *l = 16;
                                    *t = &avr_translate_LDX2;
                                    break;
                                }
                                case 0x0000000e: {
                                    *l = 16;
                                    *t = &avr_translate_LDX3;
                                    break;
                                }
                                case 0x0000000f: {
                                    *l = 16;
                                    *t = &avr_translate_POP;
                                    break;
                                }
                            }
                            break;
                        }
                        case 0x00000200: {
                            switch (opc & 0x0000000f) {
                                case 0x00000000: {
                                    *l = 32;
                                    *t = &avr_translate_STS;
                                    break;
                                }
                                case 0x00000001: {
                                    *l = 16;
                                    *t = &avr_translate_STZ2;
                                    break;
                                }
                                case 0x00000002: {
                                    *l = 16;
                                    *t = &avr_translate_STZ3;
                                    break;
                                }
                                case 0x00000003: {
                                    break;
                                }
                                case 0x00000004: {
                                    *l = 16;
                                    *t = &avr_translate_XCH;
                                    break;
                                }
                                case 0x00000005: {
                                    *l = 16;
                                    *t = &avr_translate_LAS;
                                    break;
                                }
                                case 0x00000006: {
                                    *l = 16;
                                    *t = &avr_translate_LAC;
                                    break;
                                }
                                case 0x00000007: {
                                    *l = 16;
                                    *t = &avr_translate_LAT;
                                    break;
                                }
                                case 0x00000008: {
                                    break;
                                }
                                case 0x00000009: {
                                    *l = 16;
                                    *t = &avr_translate_STY2;
                                    break;
                                }
                                case 0x0000000a: {
                                    *l = 16;
                                    *t = &avr_translate_STY3;
                                    break;
                                }
                                case 0x0000000b: {
                                    break;
                                }
                                case 0x0000000c: {
                                    *l = 16;
                                    *t = &avr_translate_STX1;
                                    break;
                                }
                                case 0x0000000d: {
                                    *l = 16;
                                    *t = &avr_translate_STX2;
                                    break;
                                }
                                case 0x0000000e: {
                                    *l = 16;
                                    *t = &avr_translate_STX3;
                                    break;
                                }
                                case 0x0000000f: {
                                    *l = 16;
                                    *t = &avr_translate_PUSH;
                                    break;
                                }
                            }
                            break;
                        }
                        case 0x00000400: {
                            switch (opc & 0x0000000e) {
                                case 0x00000000: {
                                    switch (opc & 0x00000001) {
                                        case 0x00000000: {
                                            *l = 16;
                                            *t = &avr_translate_COM;
                                            break;
                                        }
                                        case 0x00000001: {
                                            *l = 16;
                                            *t = &avr_translate_NEG;
                                            break;
                                        }
                                    }
                                    break;
                                }
                                case 0x00000002: {
                                    switch (opc & 0x00000001) {
                                        case 0x00000000: {
                                            *l = 16;
                                            *t = &avr_translate_SWAP;
                                            break;
                                        }
                                        case 0x00000001: {
                                            *l = 16;
                                            *t = &avr_translate_INC;
                                            break;
                                        }
                                    }
                                    break;
                                }
                                case 0x00000004: {
                                    *l = 16;
                                    *t = &avr_translate_ASR;
                                    break;
                                }
                                case 0x00000006: {
                                    switch (opc & 0x00000001) {
                                        case 0x00000000: {
                                            *l = 16;
                                            *t = &avr_translate_LSR;
                                            break;
                                        }
                                        case 0x00000001: {
                                            *l = 16;
                                            *t = &avr_translate_ROR;
                                            break;
                                        }
                                    }
                                    break;
                                }
                                case 0x00000008: {
                                    switch (opc & 0x00000181) {
                                        case 0x00000000: {
                                            *l = 16;
                                            *t = &avr_translate_BSET;
                                            break;
                                        }
                                        case 0x00000001: {
                                            switch (opc & 0x00000010) {
                                                case 0x00000000: {
                                                    *l = 16;
                                                    *t = &avr_translate_IJMP;
                                                    break;
                                                }
                                                case 0x00000010: {
                                                    *l = 16;
                                                    *t = &avr_translate_EIJMP;
                                                    break;
                                                }
                                            }
                                            break;
                                        }
                                        case 0x00000080: {
                                            *l = 16;
                                            *t = &avr_translate_BCLR;
                                            break;
                                        }
                                        case 0x00000081: {
                                            break;
                                        }
                                        case 0x00000100: {
                                            switch (opc & 0x00000010) {
                                                case 0x00000000: {
                                                    *l = 16;
                                                    *t = &avr_translate_RET;
                                                    break;
                                                }
                                                case 0x00000010: {
                                                    *l = 16;
                                                    *t = &avr_translate_RETI;
                                                    break;
                                                }
                                            }
                                            break;
                                        }
                                        case 0x00000101: {
                                            switch (opc & 0x00000010) {
                                                case 0x00000000: {
                                                    *l = 16;
                                                    *t = &avr_translate_ICALL;
                                                    break;
                                                }
                                                case 0x00000010: {
                                                    *l = 16;
                                                    *t = &avr_translate_EICALL;
                                                    break;
                                                }
                                            }
                                            break;
                                        }
                                        case 0x00000180: {
                                            switch (opc & 0x00000070) {
                                                case 0x00000000: {
                                                    *l = 16;
                                                    *t = &avr_translate_SLEEP;
                                                    break;
                                                }
                                                case 0x00000010: {
                                                    *l = 16;
                                                    *t = &avr_translate_BREAK;
                                                    break;
                                                }
                                                case 0x00000020: {
                                                    *l = 16;
                                                    *t = &avr_translate_WDR;
                                                    break;
                                                }
                                                case 0x00000030: {
                                                    break;
                                                }
                                                case 0x00000040: {
                                                    *l = 16;
                                                    *t = &avr_translate_LPM1;
                                                    break;
                                                }
                                                case 0x00000050: {
                                                    *l = 16;
                                                    *t = &avr_translate_ELPM1;
                                                    break;
                                                }
                                                case 0x00000060: {
                                                    *l = 16;
                                                    *t = &avr_translate_SPM;
                                                    break;
                                                }
                                                case 0x00000070: {
                                                    *l = 16;
                                                    *t = &avr_translate_SPMX;
                                                    break;
                                                }
                                            }
                                            break;
                                        }
                                        case 0x00000181: {
                                            break;
                                        }
                                    }
                                    break;
                                }
                                case 0x0000000a: {
                                    switch (opc & 0x00000001) {
                                        case 0x00000000: {
                                            *l = 16;
                                            *t = &avr_translate_DEC;
                                            break;
                                        }
                                        case 0x00000001: {
                                            *l = 16;
                                            *t = &avr_translate_DES;
                                            break;
                                        }
                                    }
                                    break;
                                }
                                case 0x0000000c: {
                                    *l = 32;
                                    *t = &avr_translate_JMP;
                                    break;
                                }
                                case 0x0000000e: {
                                    *l = 32;
                                    *t = &avr_translate_CALL;
                                    break;
                                }
                            }
                            break;
                        }
                        case 0x00000600: {
                            switch (opc & 0x00000100) {
                                case 0x00000000: {
                                    *l = 16;
                                    *t = &avr_translate_ADIW;
                                    break;
                                }
                                case 0x00000100: {
                                    *l = 16;
                                    *t = &avr_translate_SBIW;
                                    break;
                                }
                            }
                            break;
                        }
                    }
                    break;
                }
                case 0x00000800: {
                    switch (opc & 0x00000400) {
                        case 0x00000000: {
                            switch (opc & 0x00000300) {
                                case 0x00000000: {
                                    *l = 16;
                                    *t = &avr_translate_CBI;
                                    break;
                                }
                                case 0x00000100: {
                                    *l = 16;
                                    *t = &avr_translate_SBIC;
                                    break;
                                }
                                case 0x00000200: {
                                    *l = 16;
                                    *t = &avr_translate_SBI;
                                    break;
                                }
                                case 0x00000300: {
                                    *l = 16;
                                    *t = &avr_translate_SBIS;
                                    break;
                                }
                            }
                            break;
                        }
                        case 0x00000400: {
                            *l = 16;
                            *t = &avr_translate_MUL;
                            break;
                        }
                    }
                    break;
                }
                case 0x00002000: {
                    *l = 16;
                    *t = &avr_translate_IN;
                    break;
                }
                case 0x00002800: {
                    *l = 16;
                    *t = &avr_translate_OUT;
                    break;
                }
            }
            break;
        }
        case 0x0000c000: {
            switch (opc & 0x00002000) {
                case 0x00000000: {
                    *l = 16;
                    *t = &avr_translate_RJMP;
                    break;
                }
                case 0x00002000: {
                    *l = 16;
                    *t = &avr_translate_LDI;
                    break;
                }
            }
            break;
        }
        case 0x0000d000: {
            switch (opc & 0x00002000) {
                case 0x00000000: {
                    *l = 16;
                    *t = &avr_translate_RCALL;
                    break;
                }
                case 0x00002000: {
                    switch (opc & 0x00000c00) {
                        case 0x00000000: {
                            *l = 16;
                            *t = &avr_translate_BRBS;
                            break;
                        }
                        case 0x00000400: {
                            *l = 16;
                            *t = &avr_translate_BRBC;
                            break;
                        }
                        case 0x00000800: {
                            switch (opc & 0x00000200) {
                                case 0x00000000: {
                                    *l = 16;
                                    *t = &avr_translate_BLD;
                                    break;
                                }
                                case 0x00000200: {
                                    *l = 16;
                                    *t = &avr_translate_BST;
                                    break;
                                }
                            }
                            break;
                        }
                        case 0x00000c00: {
                            switch (opc & 0x00000200) {
                                case 0x00000000: {
                                    *l = 16;
                                    *t = &avr_translate_SBRC;
                                    break;
                                }
                                case 0x00000200: {
                                    *l = 16;
                                    *t = &avr_translate_SBRS;
                                    break;
                                }
                            }
                            break;
                        }
                    }
                    break;
                }
            }
            break;
        }
    }

}
