/*
 * QEMU security policy support
 *
 * Copyright (c) 2016 Advanced Micro Devices
 *
 * Author:
 *      Brijesh Singh <brijesh.singh@amd.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef SECURITY_POLICY_H
#define SECURITY_POLICY_H

#include "qom/object.h"

#define TYPE_SECURITY_POLICY "security-policy"
#define SECURITY_POLICY(obj)                  \
    OBJECT_CHECK(SecurityPolicy, (obj), TYPE_SECURITY_POLICY)

typedef struct SecurityPolicy SecurityPolicy;
typedef struct SecurityPolicyClass SecurityPolicyClass;

/**
 * SecurityPolicy:
 *
 * The SecurityPolicy object provides method to define
 * various security releated policies for guest machine.
 *
 * e.g
 * When launching QEMU, user can create a security policy
 * to disallow memory dump and debug of guest
 *
 *  # $QEMU \
 *      -object security-policy,id=mypolicy,debug=off \
 *      -machine ...,security-policy=mypolicy
 *
 * If hardware supports memory encryption then user can set
 * encryption policy of guest
 *
 * # $QEMU \
 *    -object encrypt-policy,key=xxx,flags=xxxx,id=encrypt \
 *    -object security-policy,debug=off,memory-encryption=encrypt,id=mypolicy \
 *    -machine ...,security-policy=mypolicy
 *
 */

struct SecurityPolicy {
    Object parent_obj;

    bool debug;
    char *memory_encryption;
};


struct SecurityPolicyClass {
    ObjectClass parent_class;
};

bool security_policy_debug_allowed(const char *name);
char *security_policy_get_memory_encryption_id(const char *name);

#endif /* SECURITY_POLICY_H */
