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
 * This uses the undocumented oidfmt interface to find the kind of a requested
 * sysctl, see /sys/kern/kern_sysctl.c:sysctl_sysctl_oidfmt() (compare to
 * src/sbin/sysctl/sysctl.c)
 */
static int oidfmt(int *oid, int len, char *fmt, uint32_t *kind)
{
    int qoid[CTL_MAXNAME + 2];
    uint8_t buf[BUFSIZ];
    int i;
    size_t j;

    qoid[0] = 0;
    qoid[1] = 4;
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

/*
 * try and convert sysctl return data for the target.
 * Note: doesn't handle CTLTYPE_OPAQUE and CTLTYPE_STRUCT.
 */
static int sysctl_oldcvt(void *holdp, size_t *holdlen, uint32_t kind)
{
    switch (kind & CTLTYPE) {
    case CTLTYPE_INT:
    case CTLTYPE_UINT:
        *(uint32_t *)holdp = tswap32(*(uint32_t *)holdp);
        break;

#ifdef TARGET_ABI32
    case CTLTYPE_LONG:
    case CTLTYPE_ULONG:
        /*
         * If the sysctl has a type of long/ulong but seems to be bigger than
         * these data types, its probably an array.  Double check that its
         * evenly divisible by the size of long and convert holdp to a series of
         * 32bit elements instead, adjusting holdlen to the new size.
         */
        if ((*holdlen > sizeof(abi_ulong)) &&
            ((*holdlen % sizeof(abi_ulong)) == 0)) {
            int array_size = *holdlen / sizeof(long);
            int i;
            if (holdp) {
                for (i = 0; i < array_size; i++) {
                    ((uint32_t *)holdp)[i] = tswap32(((long *)holdp)[i]);
                }
                *holdlen = array_size * sizeof(abi_ulong);
            } else {
                *holdlen = sizeof(abi_ulong);
            }
        } else {
            *(uint32_t *)holdp = tswap32(*(long *)holdp);
            *holdlen = sizeof(uint32_t);
        }
        break;
#else
    case CTLTYPE_LONG:
        *(uint64_t *)holdp = tswap64(*(long *)holdp);
        break;
    case CTLTYPE_ULONG:
        *(uint64_t *)holdp = tswap64(*(unsigned long *)holdp);
        break;
#endif
    case CTLTYPE_U64:
    case CTLTYPE_S64:
        *(uint64_t *)holdp = tswap64(*(uint64_t *)holdp);
        break;

    case CTLTYPE_STRING:
        break;

    default:
        return -1;
    }
    return 0;
}

/*
 * Convert the undocmented name2oid sysctl data for the target.
 */
static inline void sysctl_name2oid(uint32_t *holdp, size_t holdlen)
{
    size_t i, num = holdlen / sizeof(uint32_t);

    for (i = 0; i < num; i++) {
        holdp[i] = tswap32(holdp[i]);
    }
}

static inline void sysctl_oidfmt(uint32_t *holdp)
{
    /* byte swap the kind */
    holdp[0] = tswap32(holdp[0]);
}

#define bsd_get_ncpu() 1 /* Placeholder */

static abi_long do_freebsd_sysctl_oid(CPUArchState *env, int32_t *snamep,
        int32_t namelen, void *holdp, size_t *holdlenp, void *hnewp,
        size_t newlen)
{
    uint32_t kind = 0;
#if TARGET_ABI_BITS != HOST_LONG_BITS
    const abi_ulong maxmem = -0x100c000;
#endif
    abi_long ret;
    size_t holdlen, oldlen;

    holdlen = oldlen = *holdlenp;
    oidfmt(snamep, namelen, NULL, &kind);

    /* Handle some arch/emulator dependent sysctl()'s here. */
    switch (snamep[0]) {
#if defined(TARGET_PPC) || defined(TARGET_PPC64)
    case CTL_MACHDEP:
        switch (snamep[1]) {
        case 1:    /* CPU_CACHELINE */
            holdlen = sizeof(uint32_t);
            (*(uint32_t *)holdp) = tswap32(env->dcache_line_size);
            ret = 0;
            goto out;
        }
        break;
#endif
    case CTL_KERN:
        switch (snamep[1]) {
        case KERN_USRSTACK:
            if (oldlen) {
                (*(abi_ulong *)holdp) = tswapal(TARGET_USRSTACK);
            }
            holdlen = sizeof(abi_ulong);
            ret = 0;
            goto out;

        case KERN_PS_STRINGS:
            if (oldlen) {
                (*(abi_ulong *)holdp) = tswapal(TARGET_PS_STRINGS);
            }
            holdlen = sizeof(abi_ulong);
            ret = 0;
            goto out;

        default:
            break;
        }
        break;

    case CTL_HW:
        switch (snamep[1]) {
        case HW_MACHINE:
            holdlen = sizeof(TARGET_HW_MACHINE);
            if (holdp) {
                strlcpy(holdp, TARGET_HW_MACHINE, oldlen);
            }
            ret = 0;
            goto out;

        case HW_MACHINE_ARCH:
        {
            holdlen = sizeof(TARGET_HW_MACHINE_ARCH);
            if (holdp) {
                strlcpy(holdp, TARGET_HW_MACHINE_ARCH, oldlen);
            }
            ret = 0;
            goto out;
        }
        case HW_NCPU:
            if (oldlen) {
                (*(int32_t *)holdp) = tswap32(bsd_get_ncpu());
            }
            holdlen = sizeof(int32_t);
            ret = 0;
            goto out;
#if defined(TARGET_ARM)
        case HW_FLOATINGPT:
            if (oldlen) {
#ifdef ARM_FEATURE_VFP /* XXX FIXME XXX */
                if (env->features & ((1ULL << ARM_FEATURE_VFP)|
                                     (1ULL << ARM_FEATURE_VFP3)|
                                     (1ULL << ARM_FEATURE_VFP4)))
                    *(int32_t *)holdp = 1;
                else
                    *(int32_t *)holdp = 0;
#else
                *(int32_t *)holdp = 1;
#endif
            }
            holdlen = sizeof(int32_t);
            ret = 0;
            goto out;
#endif


#if TARGET_ABI_BITS != HOST_LONG_BITS
        case HW_PHYSMEM:
        case HW_USERMEM:
        case HW_REALMEM:
            holdlen = sizeof(abi_ulong);
            ret = 0;

            if (oldlen) {
                int mib[2] = {snamep[0], snamep[1]};
                unsigned long lvalue;
                size_t len = sizeof(lvalue);

                if (sysctl(mib, 2, &lvalue, &len, NULL, 0) == -1) {
                    ret = -1;
                } else {
                    if (((unsigned long)maxmem) < lvalue) {
                        lvalue = maxmem;
                    }
                    (*(abi_ulong *)holdp) = tswapal((abi_ulong)lvalue);
                }
            }
            goto out;
#endif

        default:
        {
            static int oid_hw_availpages;
            static int oid_hw_pagesizes;

            if (!oid_hw_availpages) {
                int real_oid[CTL_MAXNAME + 2];
                size_t len = sizeof(real_oid) / sizeof(int);

                if (sysctlnametomib("hw.availpages", real_oid, &len) >= 0) {
                    oid_hw_availpages = real_oid[1];
                }
            }
            if (!oid_hw_pagesizes) {
                int real_oid[CTL_MAXNAME + 2];
                size_t len = sizeof(real_oid) / sizeof(int);

                if (sysctlnametomib("hw.pagesizes", real_oid, &len) >= 0) {
                    oid_hw_pagesizes = real_oid[1];
                }
            }

            if (oid_hw_availpages && snamep[1] == oid_hw_availpages) {
                long lvalue;
                size_t len = sizeof(lvalue);

                if (sysctlbyname("hw.availpages", &lvalue, &len, NULL, 0) == -1) {
                    ret = -1;
                } else {
                    if (oldlen) {
#if TARGET_ABI_BITS != HOST_LONG_BITS
                        abi_ulong maxpages = maxmem / (abi_ulong)getpagesize();
                        if (((unsigned long)maxpages) < lvalue) {
                            lvalue = maxpages;
                        }
#endif
                        (*(abi_ulong *)holdp) = tswapal((abi_ulong)lvalue);
                    }
                    holdlen = sizeof(abi_ulong);
                    ret = 0;
                }
                goto out;
            }

            if (oid_hw_pagesizes && snamep[1] == oid_hw_pagesizes) {
                if (oldlen) {
                    (*(abi_ulong *)holdp) = tswapal((abi_ulong)getpagesize());
                    ((abi_ulong *)holdp)[1] = 0;
                }
                holdlen = sizeof(abi_ulong) * 2;
                ret = 0;
                goto out;
            }
            break;
        }
        }
        break;

    default:
        break;
    }

    ret = get_errno(sysctl(snamep, namelen, holdp, &holdlen, hnewp, newlen));
    if (!ret && (holdp != 0)) {

        if (0 == snamep[0] &&
            (2 == snamep[1] || 3 == snamep[1] || 4 == snamep[1])) {
            switch (snamep[1]) {
            case 2:
            case 3:
                /* Handle the undocumented name2oid special case. */
                sysctl_name2oid(holdp, holdlen);
                break;

            case 4:
            default:
                /* Handle oidfmt */
                sysctl_oidfmt(holdp);
                break;
            }
        } else {
            sysctl_oldcvt(holdp, &holdlen, kind);
        }
    }

out:
    *holdlenp = holdlen;
    return ret;
}

/*
 * This syscall was created to make sysctlbyname(3) more efficient.
 * Unfortunately, because we have to fake some sysctls, we can't do that.
 */
abi_long do_freebsd_sysctlbyname(CPUArchState *env, abi_ulong namep,
        int32_t namelen, abi_ulong oldp, abi_ulong oldlenp, abi_ulong newp,
        abi_ulong newlen)
{
    abi_long ret;
    void *holdp = NULL, *hnewp = NULL;
    char *snamep;
    int oid[CTL_MAXNAME + 2];
    size_t holdlen, oidplen;
    abi_ulong oldlen = 0;

    if (oldlenp) {
        if (get_user_ual(oldlen, oldlenp)) {
            return -TARGET_EFAULT;
        }
    }
    snamep = lock_user_string(namep);
    if (snamep == NULL) {
        return -TARGET_EFAULT;
    }
    if (newp) {
        hnewp = lock_user(VERIFY_READ, newp, newlen, 1);
        if (hnewp == NULL) {
            return -TARGET_EFAULT;
        }
    }
    if (oldp) {
        holdp = lock_user(VERIFY_WRITE, oldp, oldlen, 0);
        if (holdp == NULL) {
            return -TARGET_EFAULT;
        }
    }
    holdlen = oldlen;

    oidplen = sizeof(oid) / sizeof(int);
    if (sysctlnametomib(snamep, oid, &oidplen) != 0) {
        return -TARGET_EINVAL;
    }

    ret = do_freebsd_sysctl_oid(env, oid, oidplen, holdp, &holdlen, hnewp,
        newlen);

    if (oldlenp) {
        put_user_ual(holdlen, oldlenp);
    }
    unlock_user(snamep, namep, 0);
    unlock_user(holdp, oldp, holdlen);
    if (hnewp) {
        unlock_user(hnewp, newp, 0);
    }

    return ret;
}

abi_long do_freebsd_sysctl(CPUArchState *env, abi_ulong namep, int32_t namelen,
        abi_ulong oldp, abi_ulong oldlenp, abi_ulong newp, abi_ulong newlen)
{
    abi_long ret;
    void *hnamep, *holdp = NULL, *hnewp = NULL;
    size_t holdlen;
    abi_ulong oldlen = 0;
    int32_t *snamep = g_malloc(sizeof(int32_t) * namelen), *p, *q, i;

    if (oldlenp) {
        if (get_user_ual(oldlen, oldlenp)) {
            return -TARGET_EFAULT;
        }
    }
    hnamep = lock_user(VERIFY_READ, namep, namelen, 1);
    if (hnamep == NULL) {
        return -TARGET_EFAULT;
    }
    if (newp) {
        hnewp = lock_user(VERIFY_READ, newp, newlen, 1);
        if (hnewp == NULL) {
            return -TARGET_EFAULT;
        }
    }
    if (oldp) {
        holdp = lock_user(VERIFY_WRITE, oldp, oldlen, 0);
        if (holdp == NULL) {
            return -TARGET_EFAULT;
        }
    }
    holdlen = oldlen;
    for (p = hnamep, q = snamep, i = 0; i < namelen; p++, i++) {
        *q++ = tswap32(*p);
    }

    ret = do_freebsd_sysctl_oid(env, snamep, namelen, holdp, &holdlen, hnewp,
        newlen);

    if (oldlenp) {
        put_user_ual(holdlen, oldlenp);
    }
    unlock_user(hnamep, namep, 0);
    unlock_user(holdp, oldp, holdlen);
    if (hnewp) {
        unlock_user(hnewp, newp, 0);
    }
    g_free(snamep);
    return ret;
}

/* sysarch() is architecture dependent. */
abi_long do_freebsd_sysarch(void *cpu_env, abi_long arg1, abi_long arg2)
{
    return do_freebsd_arch_sysarch(cpu_env, arg1, arg2);
}
