/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 *  VirtIO SD/MMC driver
 *
 *  Author: Mikhail Krasheninnikov <krashmisha@gmail.com>
 */
#ifndef _LINUX_VIRTIO_MMC_H
#define _LINUX_VIRTIO_MMC_H
#include <linux/types.h>

struct mmc_req {
	__le32 opcode;
	__le32 arg;
};

struct virtio_mmc_request {
	uint8_t flags;

#define VIRTIO_MMC_REQUEST_DATA BIT(1)
#define VIRTIO_MMC_REQUEST_WRITE BIT(2)
#define VIRTIO_MMC_REQUEST_STOP BIT(3)
#define VIRTIO_MMC_REQUEST_SBC BIT(4)

	struct mmc_req request;

	uint8_t buf[4096];
	__le32 buf_len;

	struct mmc_req stop_req;
	struct mmc_req sbc_req;
};

struct virtio_mmc_response {
	__le32 cmd_resp[4];
	int cmd_resp_len;
	uint8_t buf[4096];
};

#endif /* _LINUX_VIRTIO_MMC_H */
