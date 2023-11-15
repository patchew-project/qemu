/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * uefi vars device
 */
#include "qemu/osdep.h"
#include "sysemu/dma.h"
#include "migration/vmstate.h"

#include "hw/uefi/var-service.h"
#include "hw/uefi/var-service-api.h"
#include "hw/uefi/var-service-edk2.h"

#include "trace/trace-hw_uefi.h"

static int uefi_vars_pre_load(void *opaque)
{
    uefi_vars_state *uv = opaque;

    uefi_vars_clear_all(uv);
    uefi_vars_policies_clear(uv);
    g_free(uv->buffer);
    return 0;
}

static int uefi_vars_post_load(void *opaque, int version_id)
{
    uefi_vars_state *uv = opaque;

    uefi_vars_update_storage(uv);
    uv->buffer = g_malloc(uv->buf_size);
    return 0;
}

const VMStateDescription vmstate_uefi_vars = {
    .name = "uefi-vars",
    .pre_load = uefi_vars_pre_load,
    .post_load = uefi_vars_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(sts, uefi_vars_state),
        VMSTATE_UINT32(buf_size, uefi_vars_state),
        VMSTATE_UINT32(buf_addr_lo, uefi_vars_state),
        VMSTATE_UINT32(buf_addr_hi, uefi_vars_state),
        VMSTATE_BOOL(end_of_dxe, uefi_vars_state),
        VMSTATE_BOOL(ready_to_boot, uefi_vars_state),
        VMSTATE_BOOL(exit_boot_service, uefi_vars_state),
        VMSTATE_BOOL(policy_locked, uefi_vars_state),
        VMSTATE_UINT64(used_storage, uefi_vars_state),
        VMSTATE_QTAILQ_V(variables, uefi_vars_state, 0,
                         vmstate_uefi_variable, uefi_variable, next),
        VMSTATE_QTAILQ_V(var_policies, uefi_vars_state, 0,
                         vmstate_uefi_var_policy, uefi_var_policy, next),
        VMSTATE_END_OF_LIST()
    },
};

size_t uefi_strlen(const uint16_t *str, size_t len)
{
    size_t pos = 0;

    for (;;) {
        if (pos == len) {
            return pos;
        }
        if (str[pos] == 0) {
            return pos;
        }
        pos++;
    }
}

gboolean uefi_str_equal(const uint16_t *a, size_t alen,
                        const uint16_t *b, size_t blen)
{
    size_t pos = 0;

    alen = alen / 2;
    blen = blen / 2;
    for (;;) {
        if (pos == alen && pos == blen) {
            return true;
        }
        if (pos == alen && b[pos] == 0) {
            return true;
        }
        if (pos == blen && a[pos] == 0) {
            return true;
        }
        if (pos == alen || pos == blen) {
            return false;
        }
        if (a[pos] == 0 && b[pos] == 0) {
            return true;
        }
        if (a[pos] != b[pos]) {
            return false;
        }
        pos++;
    }
}

char *uefi_ucs2_to_ascii(const uint16_t *ucs2, uint64_t ucs2_size)
{
    char *str = g_malloc0(ucs2_size / 2 + 1);
    int i;

    for (i = 0; i * 2 < ucs2_size; i++) {
        if (ucs2[i] == 0) {
            break;
        }
        if (ucs2[i] < 128) {
            str[i] = ucs2[i];
        } else {
            str[i] = '?';
        }
    }
    str[i] = 0;
    return str;
}

void uefi_trace_variable(const char *action, QemuUUID guid,
                         const uint16_t *name, uint64_t name_size)
{
    QemuUUID be = qemu_uuid_bswap(guid);
    char *str_uuid = qemu_uuid_unparse_strdup(&be);
    char *str_name = uefi_ucs2_to_ascii(name, name_size);

    trace_uefi_variable(action, str_name, name_size, str_uuid);

    g_free(str_name);
    g_free(str_uuid);
}

void uefi_trace_status(const char *action, efi_status status)
{
    switch (status) {
    case EFI_SUCCESS:
        trace_uefi_status(action, "success");
        break;
    case EFI_INVALID_PARAMETER:
        trace_uefi_status(action, "invalid parameter");
        break;
    case EFI_UNSUPPORTED:
        trace_uefi_status(action, "unsupported");
        break;
    case EFI_BAD_BUFFER_SIZE:
        trace_uefi_status(action, "bad buffer size");
        break;
    case EFI_BUFFER_TOO_SMALL:
        trace_uefi_status(action, "buffer too small");
        break;
    case EFI_WRITE_PROTECTED:
        trace_uefi_status(action, "write protected");
        break;
    case EFI_OUT_OF_RESOURCES:
        trace_uefi_status(action, "out of resources");
        break;
    case EFI_NOT_FOUND:
        trace_uefi_status(action, "not found");
        break;
    case EFI_ACCESS_DENIED:
        trace_uefi_status(action, "access denied");
        break;
    case EFI_ALREADY_STARTED:
        trace_uefi_status(action, "already started");
        break;
    default:
        trace_uefi_status(action, "unknown error");
        break;
    }
}

static uint32_t uefi_vars_cmd_mm(uefi_vars_state *uv)
{
    hwaddr    dma;
    mm_header *mhdr;
    uint32_t  size, retval;

    dma = uv->buf_addr_lo | ((hwaddr)uv->buf_addr_hi << 32);
    mhdr = (mm_header *) uv->buffer;

    if (!uv->buffer || uv->buf_size < sizeof(*mhdr)) {
        return UEFI_VARS_STS_ERR_BAD_BUFFER_SIZE;
    }

    /* read header */
    dma_memory_read(&address_space_memory, dma,
                    uv->buffer, sizeof(*mhdr),
                    MEMTXATTRS_UNSPECIFIED);

    size = sizeof(*mhdr) + mhdr->length;
    if (uv->buf_size < size) {
        return UEFI_VARS_STS_ERR_BAD_BUFFER_SIZE;
    }

    /* read buffer (excl header) */
    dma_memory_read(&address_space_memory, dma + sizeof(*mhdr),
                    uv->buffer + sizeof(*mhdr), mhdr->length,
                    MEMTXATTRS_UNSPECIFIED);
    memset(uv->buffer + size, 0, uv->buf_size - size);

    /* dispatch */
    if (qemu_uuid_is_equal(&mhdr->guid, &EfiSmmVariableProtocolGuid)) {
        retval = uefi_vars_mm_vars_proto(uv);

    } else if (qemu_uuid_is_equal(&mhdr->guid, &VarCheckPolicyLibMmiHandlerGuid)) {
        retval = uefi_vars_mm_check_policy_proto(uv);

    } else if (qemu_uuid_is_equal(&mhdr->guid, &EfiEndOfDxeEventGroupGuid)) {
        trace_uefi_event("end-of-dxe");
        uv->end_of_dxe = true;
        retval = UEFI_VARS_STS_SUCCESS;

    } else if (qemu_uuid_is_equal(&mhdr->guid, &EfiEventReadyToBootGuid)) {
        trace_uefi_event("ready-to-boot");
        uv->ready_to_boot = true;
        retval = UEFI_VARS_STS_SUCCESS;

    } else if (qemu_uuid_is_equal(&mhdr->guid, &EfiEventExitBootServicesGuid)) {
        trace_uefi_event("exit-boot-service");
        uv->exit_boot_service = true;
        retval = UEFI_VARS_STS_SUCCESS;

    } else {
        retval = UEFI_VARS_STS_ERR_NOT_SUPPORTED;
    }

    /* write buffer */
    dma_memory_write(&address_space_memory, dma,
                     uv->buffer, sizeof(*mhdr) + mhdr->length,
                     MEMTXATTRS_UNSPECIFIED);

    return retval;
}

static void uefi_vars_soft_reset(uefi_vars_state *uv)
{
    g_free(uv->buffer);
    uv->buffer = NULL;
    uv->buf_size = 0;
    uv->buf_addr_lo = 0;
    uv->buf_addr_hi = 0;
}

void uefi_vars_hard_reset(uefi_vars_state *uv)
{
    trace_uefi_hard_reset();
    uefi_vars_soft_reset(uv);

    uv->end_of_dxe        = false;
    uv->ready_to_boot     = false;
    uv->exit_boot_service = false;
    uv->policy_locked     = false;

    uefi_vars_clear_volatile(uv);
    uefi_vars_policies_clear(uv);
    uefi_vars_auth_init(uv);
}

static uint32_t uefi_vars_cmd(uefi_vars_state *uv, uint32_t cmd)
{
    switch (cmd) {
    case UEFI_VARS_CMD_RESET:
        uefi_vars_soft_reset(uv);
        return UEFI_VARS_STS_SUCCESS;
    case UEFI_VARS_CMD_MM:
        return uefi_vars_cmd_mm(uv);
    default:
        return UEFI_VARS_STS_ERR_NOT_SUPPORTED;
    }
}

static uint64_t uefi_vars_read(void *opaque, hwaddr addr, unsigned size)
{
    uefi_vars_state *uv = opaque;
    uint64_t retval = -1;

    trace_uefi_reg_read(addr, size);

    switch (addr) {
    case UEFI_VARS_REG_MAGIC:
        retval = UEFI_VARS_MAGIC_VALUE;
        break;
    case UEFI_VARS_REG_CMD_STS:
        retval = uv->sts;
        break;
    case UEFI_VARS_REG_BUFFER_SIZE:
        retval = uv->buf_size;
        break;
    case UEFI_VARS_REG_BUFFER_ADDR_LO:
        retval = uv->buf_addr_lo;
        break;
    case UEFI_VARS_REG_BUFFER_ADDR_HI:
        retval = uv->buf_addr_hi;
        break;
    }
    return retval;
}

static void uefi_vars_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    uefi_vars_state *uv = opaque;

    trace_uefi_reg_write(addr, val, size);

    switch (addr) {
    case UEFI_VARS_REG_CMD_STS:
        uv->sts = uefi_vars_cmd(uv, val);
        break;
    case UEFI_VARS_REG_BUFFER_SIZE:
        if (val > MAX_BUFFER_SIZE) {
            val = MAX_BUFFER_SIZE;
        }
        uv->buf_size = val;
        g_free(uv->buffer);
        uv->buffer = g_malloc(uv->buf_size);
        break;
    case UEFI_VARS_REG_BUFFER_ADDR_LO:
        uv->buf_addr_lo = val;
        break;
    case UEFI_VARS_REG_BUFFER_ADDR_HI:
        uv->buf_addr_hi = val;
        break;
    }
}

static const MemoryRegionOps uefi_vars_ops = {
    .read = uefi_vars_read,
    .write = uefi_vars_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 2,
        .max_access_size = 4,
    },
};

void uefi_vars_init(Object *obj, uefi_vars_state *uv)
{
    QTAILQ_INIT(&uv->variables);
    QTAILQ_INIT(&uv->var_policies);
    uv->jsonfd = -1;
    memory_region_init_io(&uv->mr, obj, &uefi_vars_ops, uv,
                          "uefi-vars", UEFI_VARS_REGS_SIZE);
}

void uefi_vars_realize(uefi_vars_state *uv, Error **errp)
{
    uefi_vars_json_init(uv, errp);
    uefi_vars_json_load(uv, errp);
}
