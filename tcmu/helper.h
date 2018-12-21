/*
 * Copyright (c) 2014 Red Hat, Inc.
 *
 * This file is licensed to you under your choice of the GNU Lesser
 * General Public License, version 2.1 or any later version (LGPLv2.1 or
 * later), or the Apache License 2.0.
 */

/*
 * APIs for both libtcmu users and tcmu-runner plugins to use.
 */

#ifndef __TCMU_HELPER_H
#define __TCMU_HELPER_H

#include <stdbool.h>

/* Basic implementations of mandatory SCSI commands */
int tcmu_emulate_inquiry(struct tcmu_device *dev, uint8_t *cdb, struct iovec *iovec, size_t iov_cnt);
int tcmu_emulate_start_stop(struct tcmu_device *dev, uint8_t *cdb);
int tcmu_emulate_test_unit_ready(uint8_t *cdb, struct iovec *iovec, size_t iov_cnt);
int tcmu_emulate_read_capacity_10(uint64_t num_lbas, uint32_t block_size, uint8_t *cdb,
				  struct iovec *iovec, size_t iov_cnt);
int tcmu_emulate_read_capacity_16(uint64_t num_lbas, uint32_t block_size, uint8_t *cdb,
				  struct iovec *iovec, size_t iov_cnt);
int tcmu_emulate_mode_sense(struct tcmu_device *dev, uint8_t *cdb,
			    struct iovec *iovec, size_t iov_cnt);
int tcmu_emulate_mode_select(struct tcmu_device *dev, uint8_t *cdb,
			     struct iovec *iovec, size_t iov_cnt);

#endif
