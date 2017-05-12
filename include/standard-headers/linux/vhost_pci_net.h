#ifndef _LINUX_VHOST_PCI_NET_H
#define _LINUX_VHOST_PCI_NET_H

/* This header is BSD licensed so anyone can use the definitions to implement
 * compatible drivers/servers.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of Intel nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Intel OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE. */

#include "standard-headers/linux/virtio_ids.h"

#define VPNET_S_LINK_UP	1	/* Link is up */

struct vhost_pci_net_config {
	/*
	 * Legal values are between 1 and 0x8000
	 */
	uint16_t vq_pairs;
	/* See VPNET_S_* above */
	uint16_t status;
} QEMU_PACKED;

struct ctrlq_msg_remote_mem {
	uint64_t gpa;
	uint64_t size;
};

struct ctrlq_msg_remoteq {
	uint16_t last_avail_idx;
        int32_t  vring_enable;
	uint32_t vring_num;
	uint64_t desc_gpa;
	uint64_t avail_gpa;
	uint64_t used_gpa;
};

#define VHOST_PCI_CTRLQ_MSG_REMOTE_MEM	0
#define VHOST_PCI_CTRLQ_MSG_REMOTEQ	1
struct vpnet_ctrlq_msg {
	uint8_t class;
	uint8_t cmd;
	uint16_t size;
        union {
		struct ctrlq_msg_remote_mem msg_remote_mem[0];
		struct ctrlq_msg_remoteq msg_remoteq[0];
	} payload;
} __attribute__((packed));

static struct vpnet_ctrlq_msg vpnet_msg __attribute__ ((unused));
#define VPNET_CTRLQ_MSG_HDR_SIZE (sizeof(vpnet_msg.class) \
			      + sizeof(vpnet_msg.cmd)  \
			      + sizeof(vpnet_msg.size))

#endif
