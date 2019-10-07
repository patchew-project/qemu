/*
 * libqos fw_cfg support
 *
 * Copyright IBM, Corp. 2012-2013
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef LIBQOS_FW_CFG_H
#define LIBQOS_FW_CFG_H

#include "libqtest.h"

typedef struct QFWCFG QFWCFG;

struct QFWCFG
{
    uint64_t base;
    void (*select)(QTestState *qts, QFWCFG *fw_cfg, uint16_t key);
    void (*read)(QTestState *qts, QFWCFG *fw_cfg, void *data, size_t len);
};

void qfw_cfg_select(QTestState *qts, QFWCFG *fw_cfg, uint16_t key);
void qfw_cfg_read_data(QTestState *qts, QFWCFG *fw_cfg, void *data, size_t len);
void qfw_cfg_get(QTestState *qts, QFWCFG *fw_cfg, uint16_t key,
                 void *data, size_t len);
uint16_t qfw_cfg_get_u16(QTestState *qts, QFWCFG *fw_cfg, uint16_t key);
uint32_t qfw_cfg_get_u32(QTestState *qts, QFWCFG *fw_cfg, uint16_t key);
uint64_t qfw_cfg_get_u64(QTestState *qts, QFWCFG *fw_cfg, uint16_t key);
size_t qfw_cfg_get_file(QTestState *qts, QFWCFG *fw_cfg, const char *filename,
                        void *data, size_t buflen);

/**
 * mm_fw_cfg_init():
 * @base: The MMIO base address of the fw_cfg device in the guest.
 *
 * Returns a newly allocated QFWCFG object which must be released
 * with a call to g_free() when no longer required.
 */
QFWCFG *mm_fw_cfg_init(uint64_t base);

/**
 * io_fw_cfg_init():
 * @base: The I/O address of the fw_cfg device in the guest.
 *
 * Returns a newly allocated QFWCFG object which must be released
 * with a call to g_free() when no longer required.
 */
QFWCFG *io_fw_cfg_init(uint16_t base);

/**
 * pc_fw_cfg_init():
 *
 * This function is specific to the PC machine (X86).
 *
 * Returns a newly allocated QFWCFG object which must be released
 * with a call to g_free() when no longer required.
 */
static inline QFWCFG *pc_fw_cfg_init(void)
{
    return io_fw_cfg_init(0x510);
}

#endif
