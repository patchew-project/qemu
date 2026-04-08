/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * MCTP-over-SMBus/I2C responder: NCSI control message type extension.
 *
 * This is intentionally a thin QOM sub-type that only registers an extra
 * message handler (msg_type=0x02). The detailed NCSI payload parsing and
 * response building is left to be implemented by the user.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include <arpa/inet.h>

#include "hw/misc/mctp-i2c-responder.h"

#define TYPE_MCTP_I2C_RESPONDER_NCSI "mctp-i2c-responder-ncsi"
typedef struct MctpI2cResponderNcsi {
    MctpI2cResponder parent_obj;

    /* Link status */
    bool link_up;
    uint8_t link_speed_duplex; /* bits1-4 in link_speed */

    /* Temperatures */
    uint8_t asic_temp_cur;
    uint8_t asic_temp_max;
    uint8_t xcvr_temp;
    uint8_t xcvr_hi_warn;
    uint8_t xcvr_hi_alarm;

    /* Version/IDs */
    char fw_name[13]; /* max 12 bytes used in response */
    uint32_t fw_version; /* packed, returned as BE */
    uint16_t pci_did;
    uint16_t pci_vid;
    uint16_t pci_ssid;
    uint16_t pci_svid;
    uint32_t manufacturer_id; /* returned as BE */

    /* Capabilities */
    uint8_t channel_count;

    /* Mellanox OEM */
    uint8_t mlx_temp_base;
    uint8_t mlx_temp_step;
    uint8_t mlx_temp_max;
    uint8_t module_identifier;
} MctpI2cResponderNcsi;

#define MCTP_I2C_RESPONDER_NCSI(obj) \
    OBJECT_CHECK(MctpI2cResponderNcsi, (obj), TYPE_MCTP_I2C_RESPONDER_NCSI)

static bool ncsi_prop_get_link_up(Object *obj, Error **errp)
{
    MctpI2cResponderNcsi *n = MCTP_I2C_RESPONDER_NCSI(obj);
    return n->link_up;
}

static void ncsi_prop_set_link_up(Object *obj, bool value, Error **errp)
{
    MctpI2cResponderNcsi *n = MCTP_I2C_RESPONDER_NCSI(obj);
    n->link_up = value;
}

static char *ncsi_prop_get_fw_name(Object *obj, Error **errp)
{
    MctpI2cResponderNcsi *n = MCTP_I2C_RESPONDER_NCSI(obj);
    return g_strdup(n->fw_name);
}

static void ncsi_prop_set_fw_name(Object *obj, const char *value, Error **errp)
{
    MctpI2cResponderNcsi *n = MCTP_I2C_RESPONDER_NCSI(obj);
    if (!value) {
        n->fw_name[0] = '\0';
        return;
    }
    /* Response uses only 12 bytes, truncate here for predictability. */
    g_strlcpy(n->fw_name, value, sizeof(n->fw_name));
    n->fw_name[12] = '\0';
}

#define NCSI_HDR_REV_1 0x01

/* NCSI control packet types (DSP0222 Table 21). */
#define NCSI_CMD_CLEAR_INITIAL_STATE        0x00
#define NCSI_CMD_SELECT_PACKAGE             0x01
#define NCSI_CMD_DESELECT_PACKAGE           0x02
#define NCSI_CMD_ENABLE_CHANNEL             0x03
#define NCSI_CMD_DISABLE_CHANNEL            0x04
#define NCSI_CMD_RESET_CHANNEL              0x05
#define NCSI_CMD_SET_LINK                   0x09
#define NCSI_CMD_GET_LINK_STATUS            0x0a
#define NCSI_CMD_GET_VERSION_ID             0x15
#define NCSI_CMD_GET_CAPABILITIES           0x16
#define NCSI_CMD_GET_MODULE_MGMT_DATA       0x32
#define NCSI_CMD_GET_ASIC_TEMPERATURE       0x48
#define NCSI_CMD_GET_AMBIENT_TEMPERATURE    0x49
#define NCSI_CMD_GET_XCVR_TEMPERATURE       0x4a
#define NCSI_CMD_OEM_COMMAND                0x50
#define NCSI_CMD_PLDM_REQUEST               0x51

/* Mellanox OEM definitions */
#define MELLANOX_MANUFACTURER_ID 0x8119
#define MLX_OEM_CMD_ID 0x13
#define MLX_OEM_PARAM_TEMP 0x02
#define MLX_OEM_PARAM_MODULE_DATA 0x11

typedef struct NcsiCtrlPkgHdr {
    uint8_t mcId;
    uint8_t headerRevision;
    uint8_t reserved1;
    uint8_t IID;
    uint8_t ctrlPkgType;
    uint8_t channelId;
    uint16_t payloadLength;
    uint16_t reserved3;
    uint16_t reserved4;
    uint16_t reserved5;
    uint16_t reserved6;
} QEMU_PACKED NcsiCtrlPkgHdr;

static inline uint8_t ncsi_channel_pkg(uint8_t channel_id)
{
    return channel_id >> 5;
}

static inline uint8_t ncsi_channel_idx(uint8_t channel_id)
{
    return channel_id & 0x1f;
}

static inline uint8_t ncsi_ctrl_rsp_type(uint8_t req_type)
{
    return (uint8_t)(req_type + 0x80);
}

static inline MctpI2cResponderNcsi *ncsi_get_state(MctpI2cResponder *s)
{
    return MCTP_I2C_RESPONDER_NCSI(OBJECT(s));
}

static void ncsi_init_rsp_hdr(NcsiCtrlPkgHdr *rsp, const NcsiCtrlPkgHdr *req,
                              uint8_t rsp_type, uint16_t payload_len)
{
    memset(rsp, 0, sizeof(*rsp));
    rsp->mcId = 0x00;
    rsp->headerRevision = NCSI_HDR_REV_1;
    rsp->IID = req->IID;
    rsp->ctrlPkgType = rsp_type;
    rsp->channelId = req->channelId;
    rsp->payloadLength = htons(payload_len);
}

static bool ncsi_tx_begin_rsp(MctpI2cResponder *s, const NcsiCtrlPkgHdr *req,
                              uint8_t rsp_type, uint16_t payload_len)
{
    if (!mctp_i2c_responder_tx_begin(s, MCTP_MSG_TYPE_NCSI_CONTROL)) {
        return false;
    }

    NcsiCtrlPkgHdr hdr;
    ncsi_init_rsp_hdr(&hdr, req, rsp_type, payload_len);
    if (!mctp_i2c_responder_tx_append(s, &hdr, sizeof(hdr))) {
        return false;
    }

    uint16_t response_code = 0x0000;
    uint16_t reason_code = 0x0000;
    (void)mctp_i2c_responder_tx_append(s, &response_code,
                                       sizeof(response_code));
    (void)mctp_i2c_responder_tx_append(s, &reason_code, sizeof(reason_code));
    return true;
}

static void ncsi_tx_append_u32be(MctpI2cResponder *s, uint32_t v)
{
    v = htonl(v);
    (void)mctp_i2c_responder_tx_append(s, &v, sizeof(v));
}

static void ncsi_tx_append_u16be(MctpI2cResponder *s, uint16_t v)
{
    v = htons(v);
    (void)mctp_i2c_responder_tx_append(s, &v, sizeof(v));
}

static uint8_t mlx_module_byte(MctpI2cResponderNcsi *n,
                               uint8_t port, uint8_t page,
                               uint16_t off)
{
    /* Provide stable, parsable identifiers for module code paths. */
    if (page == 0x00 && off == 0x00) {
        /* Identifier: controlled by property (default QSFP-DD/CMIS=0x18). */
        return n->module_identifier;
    }
    /* Deterministic pattern for debugging. */
    return (uint8_t)(0x5a ^ (port * 17) ^ (page * 3) ^ (off & 0xff));
}

static void ncsi_build_min_rsp(MctpI2cResponder *s, const NcsiCtrlPkgHdr *req)
{
    /* Minimal response used by checkResponse(): NcsiCommandRes<uint32_t> */
    if (!ncsi_tx_begin_rsp(s, req, ncsi_ctrl_rsp_type(req->ctrlPkgType), 8)) {
        return;
    }

    /* data = checksum (ignored) */
    ncsi_tx_append_u32be(s, 0);
}

static void ncsi_handle_get_link_status(MctpI2cResponder *s,
                                        const NcsiCtrlPkgHdr *req)
{
    MctpI2cResponderNcsi *n = ncsi_get_state(s);
    /* NcsiGetLinkStatusRes = hdr + rcode + reason + GetLinkStatusRes */
    if (!ncsi_tx_begin_rsp(s, req,
                           ncsi_ctrl_rsp_type(req->ctrlPkgType), 4 + 16)) {
        return;
    }

    /* GetLinkStatusRes layout (16 bytes). */
    uint8_t extended_speed = 0;
    uint8_t others3 = 0;
    uint8_t others2 = 0;

    /* bit0 linkFlag; bits1-4 speedDuplex; bits5-7 others */
    uint8_t link_speed = (uint8_t)((n->link_up ? 1u : 0u) << 0) |
                         (uint8_t)((n->link_speed_duplex & 0x0f) << 1);

    uint32_t other_indications = 0;
    uint32_t oem_link_status = 0;
    uint32_t checksum = 0;

    (void)mctp_i2c_responder_tx_append(s, &extended_speed, 1);
    (void)mctp_i2c_responder_tx_append(s, &others3, 1);
    (void)mctp_i2c_responder_tx_append(s, &others2, 1);
    (void)mctp_i2c_responder_tx_append(s, &link_speed, 1);

    (void)mctp_i2c_responder_tx_append(s, &other_indications,
                                       sizeof(other_indications));
    (void)mctp_i2c_responder_tx_append(s, &oem_link_status,
                                       sizeof(oem_link_status));
    (void)mctp_i2c_responder_tx_append(s, &checksum, sizeof(checksum));
}

static void ncsi_handle_get_version_id(MctpI2cResponder *s,
                                       const NcsiCtrlPkgHdr *req)
{
    MctpI2cResponderNcsi *n = ncsi_get_state(s);
    /*
     * NcsiGetVersionIdRes = hdr + rcode + reason
     * + GetVersionIdRes (40 bytes)
     */
    if (!ncsi_tx_begin_rsp(s, req,
                           ncsi_ctrl_rsp_type(req->ctrlPkgType), 4 + 40)) {
        return;
    }

    /* ncsiVersion (uint32) */
    ncsi_tx_append_u32be(s, 0x01000000);

    /* reserved1..3 + alpha2 (4 bytes) */
    uint32_t rsvd = 0;
    (void)mctp_i2c_responder_tx_append(s, &rsvd, sizeof(rsvd));

    /* firmwareNameString[12] */
    char fw_name[12] = {0};
    memcpy(fw_name, n->fw_name, MIN((size_t)12, strlen(n->fw_name)));
    (void)mctp_i2c_responder_tx_append(s, fw_name, sizeof(fw_name));

    /* firmwareVersion (uint32, big-endian) */
    ncsi_tx_append_u32be(s, n->fw_version);

    /* pciDid/pciVid/pciSsid/pciSvid (uint16 BE) */
    ncsi_tx_append_u16be(s, n->pci_did);
    ncsi_tx_append_u16be(s, n->pci_vid);
    ncsi_tx_append_u16be(s, n->pci_ssid);
    ncsi_tx_append_u16be(s, n->pci_svid);

    /* manufacturerId (uint32 BE) */
    ncsi_tx_append_u32be(s, n->manufacturer_id);

    /* checksum (uint32, ignored) */
    ncsi_tx_append_u32be(s, 0);
}

static void ncsi_handle_get_capabilities(MctpI2cResponder *s,
                                         const NcsiCtrlPkgHdr *req)
{
    MctpI2cResponderNcsi *n = ncsi_get_state(s);
    /*
     * NcsiGetCapabilitiesRes = hdr + rcode + reason
     * + GetCapabilitiesRes (32 bytes)
     */
    if (!ncsi_tx_begin_rsp(s, req,
                           ncsi_ctrl_rsp_type(req->ctrlPkgType), 4 + 32)) {
        return;
    }

    uint32_t zero32 = 0;
    /* capabilitiesFlags .. aenControlCapabilities */
    for (int i = 0; i < 5; i++) {
        (void)mctp_i2c_responder_tx_append(s, &zero32, sizeof(zero32));
    }

    /* vlanFilterCount/mixed/multicast/unicast (4 bytes) */
    uint8_t counts[4] = {0, 0, 0, 0};
    (void)mctp_i2c_responder_tx_append(s, counts, sizeof(counts));

    /* reserved1 (u16), vlanModeSupport(u8), channelCount(u8) */
    uint16_t reserved1 = 0;
    uint8_t vlan_mode_support = 0;
    uint8_t channel_count = n->channel_count;
    (void)mctp_i2c_responder_tx_append(s, &reserved1, sizeof(reserved1));
    (void)mctp_i2c_responder_tx_append(s, &vlan_mode_support,
                                       sizeof(vlan_mode_support));
    (void)mctp_i2c_responder_tx_append(s, &channel_count,
                                       sizeof(channel_count));

    /* checksum */
    (void)mctp_i2c_responder_tx_append(s, &zero32, sizeof(zero32));
}

static void ncsi_handle_get_asic_temp(MctpI2cResponder *s,
                                      const NcsiCtrlPkgHdr *req)
{
    MctpI2cResponderNcsi *n = ncsi_get_state(s);
    /* NcsiGetAsicTempRes = hdr + rcode + reason + GetAsicTempRes (8 bytes) */
    if (!ncsi_tx_begin_rsp(s, req,
                           ncsi_ctrl_rsp_type(req->ctrlPkgType), 4 + 8)) {
        return;
    }

    uint16_t max_temp = htons(n->asic_temp_max);
    uint16_t cur_temp = htons(n->asic_temp_cur);
    uint32_t checksum = 0;
    (void)mctp_i2c_responder_tx_append(s, &max_temp, sizeof(max_temp));
    (void)mctp_i2c_responder_tx_append(s, &cur_temp, sizeof(cur_temp));
    (void)mctp_i2c_responder_tx_append(s, &checksum, sizeof(checksum));
}

static void ncsi_handle_get_xcvr_temp(MctpI2cResponder *s,
                                      const NcsiCtrlPkgHdr *req)
{
    MctpI2cResponderNcsi *n = ncsi_get_state(s);
    /*
     * NcsiGetTransceiverTempRes = hdr + rcode + reason
     * + GetTransceiverTempRes (12 bytes)
     */
    if (!ncsi_tx_begin_rsp(s, req,
                           ncsi_ctrl_rsp_type(req->ctrlPkgType), 4 + 12)) {
        return;
    }

    uint16_t hi_alarm = htons(n->xcvr_hi_alarm);
    uint16_t hi_warn = htons(n->xcvr_hi_warn);
    uint16_t temp_val = htons(n->xcvr_temp);
    uint16_t reserved = 0;
    uint32_t checksum = 0;
    (void)mctp_i2c_responder_tx_append(s, &hi_alarm, sizeof(hi_alarm));
    (void)mctp_i2c_responder_tx_append(s, &hi_warn, sizeof(hi_warn));
    (void)mctp_i2c_responder_tx_append(s, &temp_val, sizeof(temp_val));
    (void)mctp_i2c_responder_tx_append(s, &reserved, sizeof(reserved));
    (void)mctp_i2c_responder_tx_append(s, &checksum, sizeof(checksum));
}

static void ncsi_handle_mlx_oem_temp(MctpI2cResponder *s,
                                     const NcsiCtrlPkgHdr *req,
                                     const uint8_t *oem, size_t oem_len)
{
    MctpI2cResponderNcsi *n = ncsi_get_state(s);
    /*
     * Mellanox Get Temp Req layout:
     * hdr(16) + manufacturerId(4) + cmdRev(1) + cmdId(1) + parameter(1)
     * + data(1) + checksum(4)
     */
    if (oem_len < 4 + 1 + 1 + 1 + 1) {
        ncsi_build_min_rsp(s, req);
        return;
    }

    uint32_t manufacturer_id_be;
    memcpy(&manufacturer_id_be, oem, sizeof(manufacturer_id_be));
    uint32_t manufacturer_id = ntohl(manufacturer_id_be);
    uint8_t cmd_rev = oem[4];
    uint8_t cmd_id = oem[5];
    uint8_t parameter = oem[6];
    uint8_t temp_req = oem[7];

    uint8_t sp = (temp_req >> 7) & 0x1;
    uint8_t sensor_index = temp_req & 0x7f;

    if (manufacturer_id != MELLANOX_MANUFACTURER_ID ||
        cmd_id != MLX_OEM_CMD_ID ||
        parameter != MLX_OEM_PARAM_TEMP) {
        ncsi_build_min_rsp(s, req);
        return;
    }

    bool is_port = (sp != 0);
    uint8_t rsp_size = is_port ? 44 : 36;
    uint16_t payload_len = (uint16_t)(rsp_size - sizeof(NcsiCtrlPkgHdr));

    if (!mctp_i2c_responder_tx_begin(s, MCTP_MSG_TYPE_NCSI_CONTROL)) {
        return;
    }

    NcsiCtrlPkgHdr hdr;
    ncsi_init_rsp_hdr(&hdr, req,
                      ncsi_ctrl_rsp_type(req->ctrlPkgType), payload_len);
    (void)mctp_i2c_responder_tx_append(s, &hdr, sizeof(hdr));

    uint16_t response_code = 0x0000;
    uint16_t reason_code = 0x0000;
    (void)mctp_i2c_responder_tx_append(s, &response_code,
                                       sizeof(response_code));
    (void)mctp_i2c_responder_tx_append(s, &reason_code, sizeof(reason_code));

    uint32_t mid = htonl(MELLANOX_MANUFACTURER_ID);
    (void)mctp_i2c_responder_tx_append(s, &mid, sizeof(mid));
    (void)mctp_i2c_responder_tx_append(s, &cmd_rev, 1);
    (void)mctp_i2c_responder_tx_append(s, &cmd_id, 1);
    (void)mctp_i2c_responder_tx_append(s, &parameter, 1);

    /* data */
    uint8_t data0 = (uint8_t)((sp << 7) | (sensor_index & 0x7f));
    uint16_t pad = 0;
    uint8_t max_temp = n->mlx_temp_max;
    uint8_t temp_step = MAX((uint8_t)1, n->mlx_temp_step);
    uint8_t cur_temp = (uint8_t)(n->mlx_temp_base +
                                 (uint8_t)((sensor_index % 15) * temp_step));
    cur_temp = MIN(cur_temp, max_temp);

    (void)mctp_i2c_responder_tx_append(s, &data0, 1);
    (void)mctp_i2c_responder_tx_append(s, &pad, sizeof(pad));
    (void)mctp_i2c_responder_tx_append(s, &max_temp, 1);
    (void)mctp_i2c_responder_tx_append(s, &cur_temp, 1);

    if (is_port) {
        char str[8] = {0};
        memcpy(str, "mlxtemp", 6);
        (void)mctp_i2c_responder_tx_append(s, str, sizeof(str));
    }

    /* checksum */
    uint32_t checksum = 0;
    (void)mctp_i2c_responder_tx_append(s, &checksum, sizeof(checksum));
}

static void ncsi_handle_mlx_oem_module_data(MctpI2cResponder *s,
                                            const NcsiCtrlPkgHdr *req,
                                            const uint8_t *oem, size_t oem_len)
{
    MctpI2cResponderNcsi *n = ncsi_get_state(s);
    /*
     * Mellanox Get Module Info Req
     * hdr(16) + manufacturerId(4) + cmdRev(1) + cmdId(1) + parameter(1)
     * + MellanoxGetModuleDataReq(9) + checksum(4)
     */
    if (oem_len < 4 + 1 + 1 + 1 + 9) {
        ncsi_build_min_rsp(s, req);
        return;
    }

    uint32_t manufacturer_id_be;
    memcpy(&manufacturer_id_be, oem, sizeof(manufacturer_id_be));
    uint32_t manufacturer_id = ntohl(manufacturer_id_be);
    uint8_t cmd_rev = oem[4];
    uint8_t cmd_id = oem[5];
    uint8_t parameter = oem[6];

    if (manufacturer_id != MELLANOX_MANUFACTURER_ID ||
        cmd_id != MLX_OEM_CMD_ID ||
        parameter != MLX_OEM_PARAM_MODULE_DATA) {
        ncsi_build_min_rsp(s, req);
        return;
    }

    /* Parse MellanoxGetModuleDataReq */
    const uint8_t *p = &oem[7];
    uint8_t reserved1 = p[0];
    uint8_t i2c_addr = p[1];
    uint8_t page = p[2];
    uint16_t dev_addr_be;
    memcpy(&dev_addr_be, &p[3], sizeof(dev_addr_be));
    uint16_t dev_addr = ntohs(dev_addr_be);
    uint8_t others1 = p[5];
    uint8_t others2 = p[6];
    uint16_t xfer_be;
    memcpy(&xfer_be, &p[7], sizeof(xfer_be));
    uint16_t xfer = ntohs(xfer_be);
    (void)xfer;

    /* Response must be exactly sizeof(MellanoxGetModuleInfoRes)=88. */
    const size_t rsp_size = 88;
    uint16_t payload_len = (uint16_t)(rsp_size - sizeof(NcsiCtrlPkgHdr));

    if (!mctp_i2c_responder_tx_begin(s, MCTP_MSG_TYPE_NCSI_CONTROL)) {
        return;
    }

    NcsiCtrlPkgHdr hdr;
    ncsi_init_rsp_hdr(&hdr, req,
                      ncsi_ctrl_rsp_type(req->ctrlPkgType), payload_len);
    (void)mctp_i2c_responder_tx_append(s, &hdr, sizeof(hdr));

    uint16_t response_code = 0x0000;
    uint16_t reason_code = 0x0000;
    (void)mctp_i2c_responder_tx_append(s, &response_code,
                                       sizeof(response_code));
    (void)mctp_i2c_responder_tx_append(s, &reason_code, sizeof(reason_code));

    uint32_t mid = htonl(MELLANOX_MANUFACTURER_ID);
    (void)mctp_i2c_responder_tx_append(s, &mid, sizeof(mid));
    (void)mctp_i2c_responder_tx_append(s, &cmd_rev, 1);
    (void)mctp_i2c_responder_tx_append(s, &cmd_id, 1);
    (void)mctp_i2c_responder_tx_append(s, &parameter, 1);

    /* MellanoxGetModuleDataRes (57 bytes) */
    (void)mctp_i2c_responder_tx_append(s, &reserved1, 1);
    (void)mctp_i2c_responder_tx_append(s, &i2c_addr, 1);
    (void)mctp_i2c_responder_tx_append(s, &page, 1);
    (void)mctp_i2c_responder_tx_append(s, &dev_addr_be, sizeof(dev_addr_be));
    (void)mctp_i2c_responder_tx_append(s, &others1, 1);
    (void)mctp_i2c_responder_tx_append(s, &others2, 1);
    (void)mctp_i2c_responder_tx_append(s, &xfer_be, sizeof(xfer_be));

    /* Module data decode area (48 bytes). */
    uint8_t port = ncsi_channel_idx(req->channelId);
    uint8_t module_data[48];
    for (int i = 0; i < 48; i++) {
        uint16_t off = (uint16_t)(dev_addr + i);
        module_data[i] = mlx_module_byte(n, port, page, off);
    }
    (void)mctp_i2c_responder_tx_append(s, module_data, sizeof(module_data));

    /* checksum */
    uint32_t checksum = 0;
    (void)mctp_i2c_responder_tx_append(s, &checksum, sizeof(checksum));
}

static void ncsi_handle_oem(MctpI2cResponder *s, const NcsiCtrlPkgHdr *req,
                            const uint8_t *payload, size_t payload_len)
{
    /* OEM payload starts right after NcsiCtrlPkgHdr. */
    if (payload_len < 4 + 1 + 1 + 1) {
        ncsi_build_min_rsp(s, req);
        return;
    }

    uint32_t manufacturer_id_be;
    memcpy(&manufacturer_id_be, payload, sizeof(manufacturer_id_be));
    uint32_t manufacturer_id = ntohl(manufacturer_id_be);

    if (manufacturer_id == MELLANOX_MANUFACTURER_ID) {
        uint8_t cmd_id = payload[5];
        uint8_t param = payload[6];
        if (cmd_id == MLX_OEM_CMD_ID && param == MLX_OEM_PARAM_TEMP) {
            ncsi_handle_mlx_oem_temp(s, req, payload, payload_len);
            return;
        }
        if (cmd_id == MLX_OEM_CMD_ID && param == MLX_OEM_PARAM_MODULE_DATA) {
            ncsi_handle_mlx_oem_module_data(s, req, payload, payload_len);
            return;
        }
    }

    /* Unknown OEM: still return a minimal success response to avoid retries. */
    ncsi_build_min_rsp(s, req);
}

static void (*parent_init_handlers)(MctpI2cResponder *s);

static void mctp_ncsi_control_handler_placeholder(MctpI2cResponder *s)
{
    size_t rx_len = 0;
    const uint8_t *rx = mctp_i2c_responder_get_rx_buf(s, &rx_len);

    if (!rx || rx_len < 1) {
        return;
    }

    /* Keep a short trace line for quick debugging. */
    qemu_log_mask(LOG_GUEST_ERROR,
                  "mctp: NCSI control handler placeholder rx_len=%zu, rx[0]=0x%02x\n",
                  rx_len, rx[0]);

    qemu_log_mask(LOG_GUEST_ERROR,
                  "mctp: NCSI handler rx_len=%zu, msg_type=0x%02x\n",
                  rx_len, rx[0]);

    if (rx[0] != MCTP_MSG_TYPE_NCSI_CONTROL) {
        return;
    }

    const uint8_t *req_buf = rx + 1;
    size_t req_len = rx_len - 1;
    if (req_len < sizeof(NcsiCtrlPkgHdr)) {
        return;
    }

    NcsiCtrlPkgHdr req_hdr;
    memcpy(&req_hdr, req_buf, sizeof(req_hdr));

    /* NCSI payload starts right after header. */
    const uint8_t *payload = req_buf + sizeof(NcsiCtrlPkgHdr);
    size_t payload_len = req_len - sizeof(NcsiCtrlPkgHdr);

    switch (req_hdr.ctrlPkgType) {
    case NCSI_CMD_GET_LINK_STATUS:
        ncsi_handle_get_link_status(s, &req_hdr);
        break;
    case NCSI_CMD_GET_VERSION_ID:
        ncsi_handle_get_version_id(s, &req_hdr);
        break;
    case NCSI_CMD_GET_CAPABILITIES:
        ncsi_handle_get_capabilities(s, &req_hdr);
        break;
    case NCSI_CMD_GET_ASIC_TEMPERATURE:
    case NCSI_CMD_GET_AMBIENT_TEMPERATURE:
        ncsi_handle_get_asic_temp(s, &req_hdr);
        break;
    case NCSI_CMD_GET_XCVR_TEMPERATURE:
        ncsi_handle_get_xcvr_temp(s, &req_hdr);
        break;
    case NCSI_CMD_OEM_COMMAND:
        ncsi_handle_oem(s, &req_hdr, payload, payload_len);
        break;
    case NCSI_CMD_CLEAR_INITIAL_STATE:
    case NCSI_CMD_SELECT_PACKAGE:
    case NCSI_CMD_DESELECT_PACKAGE:
    case NCSI_CMD_ENABLE_CHANNEL:
    case NCSI_CMD_DISABLE_CHANNEL:
    case NCSI_CMD_RESET_CHANNEL:
    case NCSI_CMD_SET_LINK:
    case NCSI_CMD_GET_MODULE_MGMT_DATA:
    case NCSI_CMD_PLDM_REQUEST:
    default:
        /* Provide minimal success response so upper layers don't retry. */
        ncsi_build_min_rsp(s, &req_hdr);
        break;
    }
}

static void mctp_i2c_responder_ncsi_init_handlers(MctpI2cResponder *s)
{
    if (parent_init_handlers) {
        parent_init_handlers(s);
    }

    mctp_i2c_responder_register_msg_handler(
        s, MCTP_MSG_TYPE_NCSI_CONTROL,
        mctp_ncsi_control_handler_placeholder);
}

static void mctp_i2c_responder_ncsi_class_init(ObjectClass *oc, const void *data)
{
    MctpI2cResponderClass *mc = MCTP_I2C_RESPONDER_CLASS(oc);
    MctpI2cResponderClass *parent_mc =
        MCTP_I2C_RESPONDER_CLASS(object_class_get_parent(oc));

    parent_init_handlers = parent_mc->init_handlers;
    mc->init_handlers = mctp_i2c_responder_ncsi_init_handlers;
}

static void mctp_i2c_responder_ncsi_instance_init(Object *obj)
{
    MctpI2cResponderNcsi *n = MCTP_I2C_RESPONDER_NCSI(obj);

    n->link_up = true;
    n->link_speed_duplex = 0x0d; /* 100Gbps */

    n->asic_temp_cur = 45;
    n->asic_temp_max = 90;
    n->xcvr_temp = 50;
    n->xcvr_hi_warn = 85;
    n->xcvr_hi_alarm = 100;

    g_strlcpy(n->fw_name, "QEMU-MLX", sizeof(n->fw_name));
    n->fw_version = 0x162304D2; /* 22.35.1234 */
    n->pci_did = 0x101d;
    n->pci_vid = 0x15b3;
    n->pci_ssid = 0x0107;
    n->pci_svid = 0x15b3;
    n->manufacturer_id = MELLANOX_MANUFACTURER_ID;

    n->channel_count = 8;

    n->mlx_temp_base = 40;
    n->mlx_temp_step = 1;
    n->mlx_temp_max = 95;
    n->module_identifier = 0x18; /* QSFP-DD (CMIS) */

    /* QOM properties: runtime writable via qom-set (QMP/HMP). */
    object_property_add_bool(obj, "link-up",
                             ncsi_prop_get_link_up,
                             ncsi_prop_set_link_up);

    object_property_add_uint8_ptr(obj, "link-speed-duplex",
                                  &n->link_speed_duplex,
                                  OBJ_PROP_FLAG_READWRITE);

    object_property_add_uint8_ptr(obj, "asic-temp-cur",
                                  &n->asic_temp_cur,
                                  OBJ_PROP_FLAG_READWRITE);
    object_property_add_uint8_ptr(obj, "asic-temp-max",
                                  &n->asic_temp_max,
                                  OBJ_PROP_FLAG_READWRITE);

    object_property_add_uint8_ptr(obj, "xcvr-temp",
                                  &n->xcvr_temp,
                                  OBJ_PROP_FLAG_READWRITE);
    object_property_add_uint8_ptr(obj, "xcvr-hi-warn",
                                  &n->xcvr_hi_warn,
                                  OBJ_PROP_FLAG_READWRITE);
    object_property_add_uint8_ptr(obj, "xcvr-hi-alarm",
                                  &n->xcvr_hi_alarm,
                                  OBJ_PROP_FLAG_READWRITE);

    object_property_add_str(obj, "fw-name",
                            ncsi_prop_get_fw_name,
                            ncsi_prop_set_fw_name);
    object_property_add_uint32_ptr(obj, "fw-version",
                                   &n->fw_version,
                                   OBJ_PROP_FLAG_READWRITE);

    object_property_add_uint16_ptr(obj, "pci-did",
                                   &n->pci_did,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_add_uint16_ptr(obj, "pci-vid",
                                   &n->pci_vid,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_add_uint16_ptr(obj, "pci-ssid",
                                   &n->pci_ssid,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_add_uint16_ptr(obj, "pci-svid",
                                   &n->pci_svid,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_add_uint32_ptr(obj, "manufacturer-id",
                                   &n->manufacturer_id,
                                   OBJ_PROP_FLAG_READWRITE);

    object_property_add_uint8_ptr(obj, "channel-count",
                                  &n->channel_count,
                                  OBJ_PROP_FLAG_READWRITE);

    object_property_add_uint8_ptr(obj, "mlx-temp-base",
                                  &n->mlx_temp_base,
                                  OBJ_PROP_FLAG_READWRITE);
    object_property_add_uint8_ptr(obj, "mlx-temp-step",
                                  &n->mlx_temp_step,
                                  OBJ_PROP_FLAG_READWRITE);
    object_property_add_uint8_ptr(obj, "mlx-temp-max",
                                  &n->mlx_temp_max,
                                  OBJ_PROP_FLAG_READWRITE);

    object_property_add_uint8_ptr(obj, "module-identifier",
                                  &n->module_identifier,
                                  OBJ_PROP_FLAG_READWRITE);
}

static const TypeInfo mctp_i2c_responder_ncsi_type_info = {
    .name = TYPE_MCTP_I2C_RESPONDER_NCSI,
    .parent = TYPE_MCTP_I2C_RESPONDER,
    .instance_size = sizeof(MctpI2cResponderNcsi),
    .class_init = mctp_i2c_responder_ncsi_class_init,
    .instance_init = mctp_i2c_responder_ncsi_instance_init,
};

static void mctp_i2c_responder_ncsi_register_types(void)
{
    type_register_static(&mctp_i2c_responder_ncsi_type_info);
}

type_init(mctp_i2c_responder_ncsi_register_types)
