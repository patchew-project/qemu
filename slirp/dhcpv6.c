/*
 * SLIRP stateless DHCPv6
 *
 * We only support stateless DHCPv6, e.g. for network booting.
 * See RFC 3315, RFC 3646, RFC 3736 and RFC 5970 for details.
 *
 * Copyright 2016 Thomas Huth, Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "slirp.h"
#include "dhcpv6.h"

/* DHCPv6 message types */
#define MSGTYPE_REPLY        7
#define MSGTYPE_INFO_REQUEST 11

/* DHCPv6 option types */
#define OPTION_CLIENTID      1
#define OPTION_IAADDR        5
#define OPTION_ORO           6
#define OPTION_DNS_SERVERS   23
#define OPTION_BOOTFILE_URL  59

struct requested_infos {
    uint8_t *client_id;
    int client_id_len;
    bool want_dns;
    bool want_boot_url;
};

/**
 * Analyze the info request message sent by the client to
 * see what data it provided and what it wants to have.
 */
static int dhcpv6_parse_info_request(uint8_t *odata, int olen,
                                     struct requested_infos *ri)
{
    int i;

    while (olen > 0) {
        /* Parse one option */
        int option = odata[0] << 8 | odata[1];
        int len = odata[2] << 8 | odata[3];

        if (len + 4 > olen) {
            qemu_log_mask(LOG_GUEST_ERROR, "Guest sent bad DHCPv6 packet!\n");
            return -E2BIG;
        }

        switch (option) {
        case OPTION_IAADDR:
            /* According to RFC3315, we must discard requests with IA option */
            return -EINVAL;
        case OPTION_CLIENTID:
            if (len > 256) {
                /* Avoid very long IDs which could cause problems later */
                return -E2BIG;
            }
            ri->client_id = odata + 4;
            ri->client_id_len = len;
            break;
        case OPTION_ORO:        /* Option request option */
            if (len & 1) {
                return -EINVAL;
            }
            /* Check which options the client wants to have */
            for (i = 0; i < len; i += 2) {
                switch (odata[4 + i * 2] << 8 | odata[4 + i * 2 + 1]) {
                case OPTION_DNS_SERVERS:
                    ri->want_dns = true;
                    break;
                case OPTION_BOOTFILE_URL:
                    ri->want_boot_url = true;
                    break;
                }
            }
            break;
        default:
            DEBUG_MISC((dfd, "dhcpv6 info req: Unsupported option %d, len=%d\n",
                        option, len));
        }

        odata += len + 4;
        olen -= len + 4;
    }

    return 0;
}


/**
 * Handle information request messages
 */
static void dhcpv6_info_request(Slirp *slirp, struct sockaddr_in6 *srcsas,
                                uint32_t xid, uint8_t *odata, int olen)
{
    struct requested_infos ri = { NULL };
    struct sockaddr_in6 sa6, da6;
    struct mbuf *m;
    uint8_t *resp;

    if (dhcpv6_parse_info_request(odata, olen, &ri) < 0) {
        return;
    }

    m = m_get(slirp);
    if (!m) {
        return;
    }
    memset(m->m_data, 0, m->m_size);
    m->m_data += IF_MAXLINKHDR;
    resp = (uint8_t *)m->m_data + sizeof(struct ip6) + sizeof(struct udphdr);

    /* Fill in response */
    *resp++ = MSGTYPE_REPLY;
    *resp++ = (uint8_t)(xid >> 16);
    *resp++ = (uint8_t)(xid >> 8);
    *resp++ = (uint8_t)xid;

    if (ri.client_id) {
        *resp++ = 0;
        *resp++ = OPTION_CLIENTID;
        *resp++ = ri.client_id_len >> 8;
        *resp++ = ri.client_id_len;
        memcpy(resp, ri.client_id, ri.client_id_len);
        resp += ri.client_id_len;
    }
    if (ri.want_dns) {
        *resp++ = 0;
        *resp++ = OPTION_DNS_SERVERS;
        *resp++ = 0;
        *resp++ = 16;                   /* Length */
        memcpy(resp, &slirp->vnameserver_addr6, 16);
        resp += 16;
    }
    if (ri.want_boot_url) {
        uint8_t *sa = slirp->vhost_addr6.s6_addr;
        int slen;

        *resp++ = 0;
        *resp++ = OPTION_BOOTFILE_URL;
        snprintf((char *)resp + 2, (uint8_t *)m->m_data + IF_MTU - resp - 2,
                 "tftp://[%02x%02x:%02x%02x:%02x%02x:%02x%02x:"
                         "%02x%02x:%02x%02x:%02x%02x:%02x%02x]/%s",
                 sa[0], sa[1], sa[2], sa[3], sa[4], sa[5], sa[6], sa[7],
                 sa[8], sa[9], sa[10], sa[11], sa[12], sa[13], sa[14], sa[15],
                 slirp->bootp_filename);
        slen = strlen((char *)resp + 2);
        *resp++ = slen >> 8;
        *resp++ = slen;
        resp += slen;
    }

    sa6.sin6_addr = slirp->vhost_addr6;
    sa6.sin6_port = DHCPV6_SERVER_PORT;
    da6.sin6_addr = srcsas->sin6_addr;
    da6.sin6_port = srcsas->sin6_port;
    m->m_data += sizeof(struct ip6) + sizeof(struct udphdr);
    m->m_len = resp - (uint8_t *)m->m_data;
    udp6_output(NULL, m, &sa6, &da6);
}

/**
 * Handle DHCPv6 messages sent by the client
 */
void dhcpv6_input(struct sockaddr_in6 *srcsas, struct mbuf *m)
{
    uint8_t *data = (uint8_t *)m->m_data + sizeof(struct udphdr);
    int data_len = m->m_len - sizeof(struct udphdr);
    uint32_t xid;

    xid = data[1] << 16 | data[2] << 8 | data[3];

    switch (data[0]) {
    case MSGTYPE_INFO_REQUEST:
        dhcpv6_info_request(m->slirp, srcsas, xid, &data[4], data_len - 4);
        break;
    default:
        DEBUG_MISC((dfd, "dhcpv6_input: Unsupported message type 0x%x\n",
                    data[0]));
    }
}
