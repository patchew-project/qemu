/*
 *  Copyright(c) 2019-2020 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;


static inline void S4_storerhnew_rr(void *p, int index, uint16_t v)
{
  asm volatile("{\n\t"
               "    r0 = %0\n\n"
               "    memh(%1+%2<<#2) = r0.new\n\t"
               "}\n"
               :: "r"(v), "r"(p), "r"(index)
               : "r0", "memory");
}

static uint32_t data;
static inline void *S4_storerbnew_ap(uint8_t v)
{
  void *ret;
  asm volatile("{\n\t"
               "    r0 = %1\n\n"
               "    memb(%0 = ##data) = r0.new\n\t"
               "}\n"
               : "=r"(ret)
               : "r"(v)
               : "r0", "memory");
  return ret;
}

static inline void *S4_storerhnew_ap(uint16_t v)
{
  void *ret;
  asm volatile("{\n\t"
               "    r0 = %1\n\n"
               "    memh(%0 = ##data) = r0.new\n\t"
               "}\n"
               : "=r"(ret)
               : "r"(v)
               : "r0", "memory");
  return ret;
}

static inline void *S4_storerinew_ap(uint32_t v)
{
  void *ret;
  asm volatile("{\n\t"
               "    r0 = %1\n\n"
               "    memw(%0 = ##data) = r0.new\n\t"
               "}\n"
               : "=r"(ret)
               : "r"(v)
               : "r0", "memory");
  return ret;
}

static inline void S4_storeirbt_io(void *p, int pred)
{
  asm volatile("p0 = cmp.eq(%0, #1)\n\t"
               "if (p0) memb(%1+#4)=#27\n\t"
               :: "r"(pred), "r"(p)
               : "p0", "memory");
}

static inline void S4_storeirbf_io(void *p, int pred)
{
  asm volatile("p0 = cmp.eq(%0, #1)\n\t"
               "if (!p0) memb(%1+#4)=#27\n\t"
               :: "r"(pred), "r"(p)
               : "p0", "memory");
}

static inline void S4_storeirbtnew_io(void *p, int pred)
{
  asm volatile("{\n\t"
               "    p0 = cmp.eq(%0, #1)\n\t"
               "    if (p0.new) memb(%1+#4)=#27\n\t"
               "}\n\t"
               :: "r"(pred), "r"(p)
               : "p0", "memory");
}

static inline void S4_storeirbfnew_io(void *p, int pred)
{
  asm volatile("{\n\t"
               "    p0 = cmp.eq(%0, #1)\n\t"
               "    if (!p0.new) memb(%1+#4)=#27\n\t"
               "}\n\t"
               :: "r"(pred), "r"(p)
               : "p0", "memory");
}

static inline void S4_storeirht_io(void *p, int pred)
{
  asm volatile("p0 = cmp.eq(%0, #1)\n\t"
               "if (p0) memh(%1+#4)=#27\n\t"
               :: "r"(pred), "r"(p)
               : "p0", "memory");
}

static inline void S4_storeirhf_io(void *p, int pred)
{
  asm volatile("p0 = cmp.eq(%0, #1)\n\t"
               "if (!p0) memh(%1+#4)=#27\n\t"
               :: "r"(pred), "r"(p)
               : "p0", "memory");
}

static inline void S4_storeirhtnew_io(void *p, int pred)
{
  asm volatile("{\n\t"
               "    p0 = cmp.eq(%0, #1)\n\t"
               "    if (p0.new) memh(%1+#4)=#27\n\t"
               "}\n\t"
               :: "r"(pred), "r"(p)
               : "p0", "memory");
}

static inline void S4_storeirhfnew_io(void *p, int pred)
{
  asm volatile("{\n\t"
               "    p0 = cmp.eq(%0, #1)\n\t"
               "    if (!p0.new) memh(%1+#4)=#27\n\t"
               "}\n\t"
               :: "r"(pred), "r"(p)
               : "p0", "memory");
}

static inline void S4_storeirit_io(void *p, int pred)
{
  asm volatile("p0 = cmp.eq(%0, #1)\n\t"
               "if (p0) memw(%1+#4)=#27\n\t"
               :: "r"(pred), "r"(p)
               : "p0", "memory");
}

static inline void S4_storeirif_io(void *p, int pred)
{
  asm volatile("p0 = cmp.eq(%0, #1)\n\t"
               "if (!p0) memw(%1+#4)=#27\n\t"
               :: "r"(pred), "r"(p)
               : "p0", "memory");
}

static inline void S4_storeiritnew_io(void *p, int pred)
{
  asm volatile("{\n\t"
               "    p0 = cmp.eq(%0, #1)\n\t"
               "    if (p0.new) memw(%1+#4)=#27\n\t"
               "}\n\t"
               :: "r"(pred), "r"(p)
               : "p0", "memory");
}

static inline void S4_storeirifnew_io(void *p, int pred)
{
  asm volatile("{\n\t"
               "    p0 = cmp.eq(%0, #1)\n\t"
               "    if (!p0.new) memw(%1+#4)=#27\n\t"
               "}\n\t"
               :: "r"(pred), "r"(p)
               : "p0", "memory");
}

int err;

static void check(int val, int expect)
{
    if (val != expect) {
        printf("ERROR: 0x%04x != 0x%04x\n", val, expect);
        err++;
    }
}

uint32_t init[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
uint32_t array[10];

int main()
{

    memcpy(array, init, sizeof(array));
    S4_storerhnew_rr(array, 4, 0xffff);
    check(array[4], 0xffff);

    data = ~0;
    check((uint32_t)S4_storerbnew_ap(0x12), (uint32_t)&data);
    check(data, 0xffffff12);

    data = ~0;
    check((uint32_t)S4_storerhnew_ap(0x1234), (uint32_t)&data);
    check(data, 0xffff1234);

    data = ~0;
    check((uint32_t)S4_storerinew_ap(0x12345678), (uint32_t)&data);
    check(data, 0x12345678);

    /* Byte */
    memcpy(array, init, sizeof(array));
    S4_storeirbt_io(&array[1], 1);
    check(array[2], 27);
    S4_storeirbt_io(&array[2], 0);
    check(array[3], 3);

    memcpy(array, init, sizeof(array));
    S4_storeirbf_io(&array[3], 0);
    check(array[4], 27);
    S4_storeirbf_io(&array[4], 1);
    check(array[5], 5);

    memcpy(array, init, sizeof(array));
    S4_storeirbtnew_io(&array[5], 1);
    check(array[6], 27);
    S4_storeirbtnew_io(&array[6], 0);
    check(array[7], 7);

    memcpy(array, init, sizeof(array));
    S4_storeirbfnew_io(&array[7], 0);
    check(array[8], 27);
    S4_storeirbfnew_io(&array[8], 1);
    check(array[9], 9);

    /* Half word */
    memcpy(array, init, sizeof(array));
    S4_storeirht_io(&array[1], 1);
    check(array[2], 27);
    S4_storeirht_io(&array[2], 0);
    check(array[3], 3);

    memcpy(array, init, sizeof(array));
    S4_storeirhf_io(&array[3], 0);
    check(array[4], 27);
    S4_storeirhf_io(&array[4], 1);
    check(array[5], 5);

    memcpy(array, init, sizeof(array));
    S4_storeirhtnew_io(&array[5], 1);
    check(array[6], 27);
    S4_storeirhtnew_io(&array[6], 0);
    check(array[7], 7);

    memcpy(array, init, sizeof(array));
    S4_storeirhfnew_io(&array[7], 0);
    check(array[8], 27);
    S4_storeirhfnew_io(&array[8], 1);
    check(array[9], 9);

    /* Word */
    memcpy(array, init, sizeof(array));
    S4_storeirit_io(&array[1], 1);
    check(array[2], 27);
    S4_storeirit_io(&array[2], 0);
    check(array[3], 3);

    memcpy(array, init, sizeof(array));
    S4_storeirif_io(&array[3], 0);
    check(array[4], 27);
    S4_storeirif_io(&array[4], 1);
    check(array[5], 5);

    memcpy(array, init, sizeof(array));
    S4_storeiritnew_io(&array[5], 1);
    check(array[6], 27);
    S4_storeiritnew_io(&array[6], 0);
    check(array[7], 7);

    memcpy(array, init, sizeof(array));
    S4_storeirifnew_io(&array[7], 0);
    check(array[8], 27);
    S4_storeirifnew_io(&array[8], 1);
    check(array[9], 9);

    puts(err ? "FAIL" : "PASS");
    return err;
}
