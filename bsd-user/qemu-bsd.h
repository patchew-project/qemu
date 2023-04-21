/*
 *  BSD conversion extern declarations
 *
 *  Copyright (c) 2013 Stacey D. Son
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

#ifndef QEMU_BSD_H
#define QEMU_BSD_H

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>

/* bsd-socket.c */
abi_long target_to_host_sockaddr(struct sockaddr *addr, abi_ulong target_addr,
        socklen_t len);
abi_long host_to_target_sockaddr(abi_ulong target_addr, struct sockaddr *addr,
        socklen_t len);
abi_long target_to_host_ip_mreq(struct ip_mreqn *mreqn, abi_ulong target_addr,
        socklen_t len);

#endif /* QEMU_BSD_H */
