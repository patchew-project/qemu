/*
 * QEMU TCG support -- s390x vector utilitites
 *
 * Copyright (C) 2019 Red Hat Inc
 *
 * Authors:
 *   David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef S390X_VEC_H
#define S390X_VEC_H

typedef union S390Vector {
    uint64_t doubleword[2];
    uint32_t word[4];
    uint16_t halfword[8];
    uint8_t byte[16];
} S390Vector;

uint8_t s390_vec_read_element8(const S390Vector *v, uint8_t enr);
uint16_t s390_vec_read_element16(const S390Vector *v, uint8_t enr);
uint32_t s390_vec_read_element32(const S390Vector *v, uint8_t enr);
uint64_t s390_vec_read_element64(const S390Vector *v, uint8_t enr);
void s390_vec_write_element8(S390Vector *v, uint8_t enr, uint8_t data);
void s390_vec_write_element16(S390Vector *v, uint8_t enr, uint16_t data);
void s390_vec_write_element32(S390Vector *v, uint8_t enr, uint32_t data);
void s390_vec_write_element64(S390Vector *v, uint8_t enr, uint64_t data);

#endif /* S390X_VEC_H */
