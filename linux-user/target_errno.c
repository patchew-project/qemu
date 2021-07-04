/*
 *  Linux syscalls
 *
 *  Copyright (c) 2003 Fabrice Bellard
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
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "target_errno.h"
#include "errno_defs.h"

/*
 * target_to_host_errno_table[] is initialized from
 * host_to_target_errno_table[] in target_to_host_errno_table_init().
 */
static uint16_t target_to_host_errno_table[ERRNO_TABLE_SIZE] = {
};

/*
 * This list is the union of errno values overridden in asm-<arch>/errno.h
 * minus the errnos that are not actually generic to all archs.
 */
static uint16_t host_to_target_errno_table[ERRNO_TABLE_SIZE] = {
    [EAGAIN]            = TARGET_EAGAIN,
    [EIDRM]             = TARGET_EIDRM,
    [ECHRNG]            = TARGET_ECHRNG,
    [EL2NSYNC]          = TARGET_EL2NSYNC,
    [EL3HLT]            = TARGET_EL3HLT,
    [EL3RST]            = TARGET_EL3RST,
    [ELNRNG]            = TARGET_ELNRNG,
    [EUNATCH]           = TARGET_EUNATCH,
    [ENOCSI]            = TARGET_ENOCSI,
    [EL2HLT]            = TARGET_EL2HLT,
    [EDEADLK]           = TARGET_EDEADLK,
    [ENOLCK]            = TARGET_ENOLCK,
    [EBADE]             = TARGET_EBADE,
    [EBADR]             = TARGET_EBADR,
    [EXFULL]            = TARGET_EXFULL,
    [ENOANO]            = TARGET_ENOANO,
    [EBADRQC]           = TARGET_EBADRQC,
    [EBADSLT]           = TARGET_EBADSLT,
    [EBFONT]            = TARGET_EBFONT,
    [ENOSTR]            = TARGET_ENOSTR,
    [ENODATA]           = TARGET_ENODATA,
    [ETIME]             = TARGET_ETIME,
    [ENOSR]             = TARGET_ENOSR,
    [ENONET]            = TARGET_ENONET,
    [ENOPKG]            = TARGET_ENOPKG,
    [EREMOTE]           = TARGET_EREMOTE,
    [ENOLINK]           = TARGET_ENOLINK,
    [EADV]              = TARGET_EADV,
    [ESRMNT]            = TARGET_ESRMNT,
    [ECOMM]             = TARGET_ECOMM,
    [EPROTO]            = TARGET_EPROTO,
    [EDOTDOT]           = TARGET_EDOTDOT,
    [EMULTIHOP]         = TARGET_EMULTIHOP,
    [EBADMSG]           = TARGET_EBADMSG,
    [ENAMETOOLONG]      = TARGET_ENAMETOOLONG,
    [EOVERFLOW]         = TARGET_EOVERFLOW,
    [ENOTUNIQ]          = TARGET_ENOTUNIQ,
    [EBADFD]            = TARGET_EBADFD,
    [EREMCHG]           = TARGET_EREMCHG,
    [ELIBACC]           = TARGET_ELIBACC,
    [ELIBBAD]           = TARGET_ELIBBAD,
    [ELIBSCN]           = TARGET_ELIBSCN,
    [ELIBMAX]           = TARGET_ELIBMAX,
    [ELIBEXEC]          = TARGET_ELIBEXEC,
    [EILSEQ]            = TARGET_EILSEQ,
    [ENOSYS]            = TARGET_ENOSYS,
    [ELOOP]             = TARGET_ELOOP,
    [ERESTART]          = TARGET_ERESTART,
    [ESTRPIPE]          = TARGET_ESTRPIPE,
    [ENOTEMPTY]         = TARGET_ENOTEMPTY,
    [EUSERS]            = TARGET_EUSERS,
    [ENOTSOCK]          = TARGET_ENOTSOCK,
    [EDESTADDRREQ]      = TARGET_EDESTADDRREQ,
    [EMSGSIZE]          = TARGET_EMSGSIZE,
    [EPROTOTYPE]        = TARGET_EPROTOTYPE,
    [ENOPROTOOPT]       = TARGET_ENOPROTOOPT,
    [EPROTONOSUPPORT]   = TARGET_EPROTONOSUPPORT,
    [ESOCKTNOSUPPORT]   = TARGET_ESOCKTNOSUPPORT,
    [EOPNOTSUPP]        = TARGET_EOPNOTSUPP,
    [EPFNOSUPPORT]      = TARGET_EPFNOSUPPORT,
    [EAFNOSUPPORT]      = TARGET_EAFNOSUPPORT,
    [EADDRINUSE]        = TARGET_EADDRINUSE,
    [EADDRNOTAVAIL]     = TARGET_EADDRNOTAVAIL,
    [ENETDOWN]          = TARGET_ENETDOWN,
    [ENETUNREACH]       = TARGET_ENETUNREACH,
    [ENETRESET]         = TARGET_ENETRESET,
    [ECONNABORTED]      = TARGET_ECONNABORTED,
    [ECONNRESET]        = TARGET_ECONNRESET,
    [ENOBUFS]           = TARGET_ENOBUFS,
    [EISCONN]           = TARGET_EISCONN,
    [ENOTCONN]          = TARGET_ENOTCONN,
    [EUCLEAN]           = TARGET_EUCLEAN,
    [ENOTNAM]           = TARGET_ENOTNAM,
    [ENAVAIL]           = TARGET_ENAVAIL,
    [EISNAM]            = TARGET_EISNAM,
    [EREMOTEIO]         = TARGET_EREMOTEIO,
    [EDQUOT]            = TARGET_EDQUOT,
    [ESHUTDOWN]         = TARGET_ESHUTDOWN,
    [ETOOMANYREFS]      = TARGET_ETOOMANYREFS,
    [ETIMEDOUT]         = TARGET_ETIMEDOUT,
    [ECONNREFUSED]      = TARGET_ECONNREFUSED,
    [EHOSTDOWN]         = TARGET_EHOSTDOWN,
    [EHOSTUNREACH]      = TARGET_EHOSTUNREACH,
    [EALREADY]          = TARGET_EALREADY,
    [EINPROGRESS]       = TARGET_EINPROGRESS,
    [ESTALE]            = TARGET_ESTALE,
    [ECANCELED]         = TARGET_ECANCELED,
    [ENOMEDIUM]         = TARGET_ENOMEDIUM,
    [EMEDIUMTYPE]       = TARGET_EMEDIUMTYPE,
#ifdef ENOKEY
    [ENOKEY]            = TARGET_ENOKEY,
#endif
#ifdef EKEYEXPIRED
    [EKEYEXPIRED]       = TARGET_EKEYEXPIRED,
#endif
#ifdef EKEYREVOKED
    [EKEYREVOKED]       = TARGET_EKEYREVOKED,
#endif
#ifdef EKEYREJECTED
    [EKEYREJECTED]      = TARGET_EKEYREJECTED,
#endif
#ifdef EOWNERDEAD
    [EOWNERDEAD]        = TARGET_EOWNERDEAD,
#endif
#ifdef ENOTRECOVERABLE
    [ENOTRECOVERABLE]   = TARGET_ENOTRECOVERABLE,
#endif
#ifdef ENOMSG
    [ENOMSG]            = TARGET_ENOMSG,
#endif
#ifdef ERKFILL
    [ERFKILL]           = TARGET_ERFKILL,
#endif
#ifdef EHWPOISON
    [EHWPOISON]         = TARGET_EHWPOISON,
#endif
};

void target_to_host_errno_table_init(void)
{
    /*
     * Build target_to_host_errno_table[] table
     * from host_to_target_errno_table[].
     */
    for (int i = 0; i < ERRNO_TABLE_SIZE; i++) {
        target_to_host_errno_table[host_to_target_errno_table[i]] = i;
    }
}

int host_to_target_errno(int err)
{
    if (err >= 0 && err < ERRNO_TABLE_SIZE &&
        host_to_target_errno_table[err]) {
        return host_to_target_errno_table[err];
    }
    return err;
}

int target_to_host_errno(int err)
{
    if (err >= 0 && err < ERRNO_TABLE_SIZE &&
        target_to_host_errno_table[err]) {
        return target_to_host_errno_table[err];
    }
    return err;
}
