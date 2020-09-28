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

static inline int test_clrtnew(int arg1, int old_val)
{
  int ret;
  asm volatile("r5 = %2\n\t"
               "{\n\t"
                   "p0 = cmp.eq(%1, #1)\n\t"
                   "if (p0.new) r5=#0\n\t"
               "}\n\t"
               "%0 = r5\n\t"
               : "=r"(ret)
               : "r"(arg1), "r"(old_val)
               : "p0", "r5");
  return ret;
}

int err;

static void check(int val, int expect)
{
    if (val != expect) {
        printf("ERROR: 0x%d != 0x%d\n", val, expect);
        err++;
    }
}

int main()
{
    int res;

    res = test_clrtnew(1, 7);
    check(res, 0);
    res = test_clrtnew(2, 7);
    check(res, 7);

    puts(err ? "FAIL" : "PASS");
    return err;
}
