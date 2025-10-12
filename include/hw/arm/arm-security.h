/*
 * ARM security space helpers
 *
 * Provide ARMSecuritySpace and helpers for code that is not tied to CPU.
 *
 *  Copyright (c) 2003 Fabrice Bellard
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_ARM_ARM_SECURITY_H
#define HW_ARM_ARM_SECURITY_H

#include <stdbool.h>

/*
 * ARM v9 security states.
 * The ordering of the enumeration corresponds to the low 2 bits
 * of the GPI value, and (except for Root) the concat of NSE:NS.
 */

 typedef enum ARMSecuritySpace {
    ARMSS_Secure     = 0,
    ARMSS_NonSecure  = 1,
    ARMSS_Root       = 2,
    ARMSS_Realm      = 3,
} ARMSecuritySpace;

/* Return true if @space is secure, in the pre-v9 sense. */
static inline bool arm_space_is_secure(ARMSecuritySpace space)
{
    return space == ARMSS_Secure || space == ARMSS_Root;
}

/* Return the ARMSecuritySpace for @secure, assuming !RME or EL[0-2]. */
static inline ARMSecuritySpace arm_secure_to_space(bool secure)
{
    return secure ? ARMSS_Secure : ARMSS_NonSecure;
}

#endif /* HW_ARM_ARM_SECURITY_H */


