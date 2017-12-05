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

#define REMOTE_MEM_BAR_ID 2
#define REMOTE_MEM_BAR_SIZE 0x1000000000
#define METADATA_SIZE 4096

#define MAX_REMOTE_REGION 8

/* Set by the device to indicate that the device (e.g. metadata) is ready */
#define VPNET_S_LINK_UP 1
struct vpnet_config {
	uint16_t status;
};

struct vpnet_remote_mem {
	uint64_t gpa;
	uint64_t size;
};

struct vpnet_remote_vq {
	uint16_t last_avail_idx;
	int32_t  vring_enabled;
	uint32_t vring_num;
	uint64_t desc_gpa;
	uint64_t avail_gpa;
	uint64_t used_gpa;
};

struct vpnet_metadata {
	uint32_t nregions;
	uint32_t nvqs;
	struct vpnet_remote_mem mem[MAX_REMOTE_REGION];
	struct vpnet_remote_vq vq[0];
};

#endif
