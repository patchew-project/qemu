/*
 *  FreeBSD sysctl() and sysarch() system call emulation
 *
 *  Copyright (c) 2013-15 Stacey D. Son
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

#include "qemu/osdep.h"
#include "qemu.h"
#include "target_arch_sysarch.h"

#include <sys/sysctl.h>

/*
 * Length for the fixed length types.
 * 0 means variable length for strings and structures
 * Compare with sys/kern_sysctl.c ctl_size
 * Note: Not all types appear to be used in-tree.
 */
static const int G_GNUC_UNUSED target_ctl_size[CTLTYPE+1] = {
	[CTLTYPE_INT] = sizeof(abi_int),
	[CTLTYPE_UINT] = sizeof(abi_uint),
	[CTLTYPE_LONG] = sizeof(abi_long),
	[CTLTYPE_ULONG] = sizeof(abi_ulong),
	[CTLTYPE_S8] = sizeof(int8_t),
	[CTLTYPE_S16] = sizeof(int16_t),
	[CTLTYPE_S32] = sizeof(int32_t),
	[CTLTYPE_S64] = sizeof(int64_t),
	[CTLTYPE_U8] = sizeof(uint8_t),
	[CTLTYPE_U16] = sizeof(uint16_t),
	[CTLTYPE_U32] = sizeof(uint32_t),
	[CTLTYPE_U64] = sizeof(uint64_t),
};

static const int G_GNUC_UNUSED host_ctl_size[CTLTYPE+1] = {
	[CTLTYPE_INT] = sizeof(int),
	[CTLTYPE_UINT] = sizeof(u_int),
	[CTLTYPE_LONG] = sizeof(long),
	[CTLTYPE_ULONG] = sizeof(u_long),
	[CTLTYPE_S8] = sizeof(int8_t),
	[CTLTYPE_S16] = sizeof(int16_t),
	[CTLTYPE_S32] = sizeof(int32_t),
	[CTLTYPE_S64] = sizeof(int64_t),
	[CTLTYPE_U8] = sizeof(uint8_t),
	[CTLTYPE_U16] = sizeof(uint16_t),
	[CTLTYPE_U32] = sizeof(uint32_t),
	[CTLTYPE_U64] = sizeof(uint64_t),
};

#ifdef TARGET_ABI32
/*
 * Limit the amount of available memory to be most of the 32-bit address
 * space. 0x100c000 was arrived at through trial and error as a good
 * definition of 'most'.
 */
static const abi_ulong target_max_mem = UINT32_MAX - 0x100c000 + 1;

static abi_ulong G_GNUC_UNUSED cap_memory(uint64_t mem)
{
    if (((unsigned long)target_max_mem) < mem) {
        mem = target_max_mem;
    }

    return mem;
}
#endif

static unsigned long host_page_size;

static abi_ulong G_GNUC_UNUSED scale_to_target_pages(uint64_t pages)
{
    if (host_page_size == 0) {
        host_page_size = getpagesize();
    }

    pages = muldiv64(pages, host_page_size, TARGET_PAGE_SIZE);
#ifdef TARGET_ABI32
    abi_ulong maxpages = target_max_mem / (abi_ulong)TARGET_PAGE_SIZE;

    if (((unsigned long)maxpages) < pages) {
        pages = maxpages;
    }
#endif
    return pages;
}

#ifdef TARGET_ABI32
static abi_long G_GNUC_UNUSED h2t_long_sat(long l)
{
    if (l > INT32_MAX) {
        l = INT32_MAX;
    } else if (l < INT32_MIN) {
        l = INT32_MIN;
    }
    return l;
}

static abi_ulong G_GNUC_UNUSED h2t_ulong_sat(u_long ul)
{
    if (ul > UINT32_MAX) {
        ul = UINT32_MAX;
    }
    return ul;
}
#endif

/*
 * placeholder until bsd-user downstream upstreams this with its thread support
 */
#define bsd_get_ncpu() 1

/*
 * This uses the undocumented oidfmt interface to find the kind of a requested
 * sysctl, see /sys/kern/kern_sysctl.c:sysctl_sysctl_oidfmt() (compare to
 * src/sbin/sysctl/sysctl.c)
 */
static int G_GNUC_UNUSED oidfmt(int *oid, int len, char *fmt, uint32_t *kind)
{
    int qoid[CTL_MAXNAME + 2];
    uint8_t buf[BUFSIZ];
    int i;
    size_t j;

    qoid[0] = CTL_SYSCTL;
    qoid[1] = CTL_SYSCTL_OIDFMT;
    memcpy(qoid + 2, oid, len * sizeof(int));

    j = sizeof(buf);
    i = sysctl(qoid, len + 2, buf, &j, 0, 0);
    if (i) {
        return i;
    }

    if (kind) {
        *kind = *(uint32_t *)buf;
    }

    if (fmt) {
        strcpy(fmt, (char *)(buf + sizeof(uint32_t)));
    }
    return 0;
}

/* sysarch() is architecture dependent. */
abi_long do_freebsd_sysarch(void *cpu_env, abi_long arg1, abi_long arg2)
{
    return do_freebsd_arch_sysarch(cpu_env, arg1, arg2);
}
