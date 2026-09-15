/*
 * SPDX-License-Identifier: MIT
 *
 * QEMU remote port ATS
 *
 * Copyright (c) 2021 Xilinx Inc
 * Written by Francisco Iglesias <francisco.iglesias@xilinx.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "system/system.h"
#include "system/dma.h"
#include "qemu/log.h"
#include "qapi/qmp/qerror.h"
#include "qapi/error.h"
#include "hw/core/sysbus.h"
#include "migration/vmstate.h"
#include "hw/core/qdev-properties.h"
#include "trace.h"

#include "hw/core/remote-port-proto.h"
#include "hw/core/remote-port-ats.h"

typedef struct ATSIOMMUNotifier {
    IOMMUNotifier n;
    MemoryRegion *mr;
    RemotePortATS *rp_ats;
    int iommu_idx;
} ATSIOMMUNotifier;

IOMMUTLBEntry *rp_ats_cache_lookup_translation(RemotePortATSCache *cache,
                                               hwaddr translated_addr,
                                               hwaddr len)
{
    RemotePortATSCacheClass *c = REMOTE_PORT_ATS_CACHE_GET_CLASS(cache);

    return c->lookup_translation(cache, translated_addr, len);
}

static IOMMUTLBEntry *rp_ats_lookup_translation(RemotePortATSCache *cache,
                                                hwaddr translated_addr,
                                                hwaddr len)
{
    /* TBD */
    return NULL;
}

static bool ats_translate_address(RemotePortATS *s, struct rp_pkt *pkt,
                                  hwaddr *phys_addr, hwaddr *phys_len)
{
    /* TBD */
    return true;
}

static void rp_ats_req(RemotePortDevice *dev, struct rp_pkt *pkt)
{
    RemotePortATS *s = REMOTE_PORT_ATS(dev);
    size_t pktlen = sizeof(struct rp_pkt_ats);
    hwaddr phys_addr = 0;
    hwaddr phys_len = (hwaddr)(-1);
    uint64_t result;
    size_t enclen;
    int64_t delay;
    int64_t clk;

    assert(!(pkt->hdr.flags & RP_PKT_FLAGS_response));

    rp_dpkt_alloc(&s->rsp, pktlen);

    result = ats_translate_address(s, pkt, &phys_addr, &phys_len) ?
        RP_ATS_RESULT_ok : RP_ATS_RESULT_error;

    /*
     * delay here could be set to the annotated cost of doing issuing
     * these accesses. QEMU doesn't support this kind of annotations
     * at the moment. So we just clear the delay.
     */
    delay = 0;
    clk = pkt->ats.timestamp + delay;

    enclen = rp_encode_ats_req(pkt->hdr.id, pkt->hdr.dev,
                             &s->rsp.pkt->ats,
                             clk,
                             pkt->ats.attributes,
                             phys_addr,
                             phys_len,
                             result,
                             pkt->hdr.flags | RP_PKT_FLAGS_response);
    assert(enclen == pktlen);

    rp_write(s->rp, (void *)s->rsp.pkt, enclen);
}

static void rp_ats_realize(DeviceState *dev, Error **errp)
{
    RemotePortATS *s = REMOTE_PORT_ATS(dev);
    RemotePortDevice *rpd = REMOTE_PORT_DEVICE(dev);

    rp_register_dev(s->rp, rpd, s->rp_dev);

    s->peer = rp_get_peer(s->rp);
    address_space_init(&s->as, s->mr ? s->mr : get_system_memory(), "ats-as");

    s->iommu_notifiers = g_array_new(false, true, sizeof(ATSIOMMUNotifier *));
    s->cache = g_array_new(false, true, sizeof(IOMMUTLBEntry *));
}

static void rp_prop_allow_set_link(const Object *obj, const char *name,
                                   Object *val, Error **errp)
{
}

static void rp_ats_init(Object *obj)
{
    RemotePortATS *s = REMOTE_PORT_ATS(obj);

    object_property_add_link(obj, "rp-adaptor0", "remote-port",
                             (Object **)&s->rp,
                             rp_prop_allow_set_link,
                             OBJ_PROP_LINK_STRONG);
    object_property_add_link(obj, "mr", TYPE_MEMORY_REGION,
                             (Object **)&s->mr,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
}

static void rp_ats_unrealize(DeviceState *dev)
{
    RemotePortATS *s = REMOTE_PORT_ATS(dev);
    ATSIOMMUNotifier *notifier;
    int i;

    for (i = 0; i < s->iommu_notifiers->len; i++) {
        notifier = g_array_index(s->iommu_notifiers, ATSIOMMUNotifier *, i);
        memory_region_unregister_iommu_notifier(notifier->mr, &notifier->n);
        g_free(notifier);
    }
    g_array_free(s->iommu_notifiers, true);

    address_space_destroy(&s->as);

    for (i = 0; i < s->cache->len; i++) {
        IOMMUTLBEntry *tmp = g_array_index(s->cache, IOMMUTLBEntry *, i);
        g_free(tmp);
    }
    g_array_free(s->cache, true);
}

static Property rp_properties[] = {
    DEFINE_PROP_UINT32("rp-chan0", RemotePortATS, rp_dev, 0),
};

static void rp_ats_class_init(ObjectClass *oc, const void *data)
{
    RemotePortDeviceClass *rpdc = REMOTE_PORT_DEVICE_CLASS(oc);
    RemotePortATSCacheClass *atscc = REMOTE_PORT_ATS_CACHE_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_props_n(dc, rp_properties, ARRAY_SIZE(rp_properties));

    rpdc->ops[RP_CMD_ats_req] = rp_ats_req;
    dc->realize = rp_ats_realize;
    dc->unrealize = rp_ats_unrealize;
    atscc->lookup_translation = rp_ats_lookup_translation;
}

static const TypeInfo rp_ats_info = {
    .name          = TYPE_REMOTE_PORT_ATS,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(RemotePortATS),
    .instance_init = rp_ats_init,
    .class_init    = rp_ats_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_REMOTE_PORT_DEVICE },
        { TYPE_REMOTE_PORT_ATS_CACHE },
        { },
    },
};

static const TypeInfo rp_ats_cache_info = {
    .name          = TYPE_REMOTE_PORT_ATS_CACHE,
    .parent        = TYPE_INTERFACE,
    .class_size = sizeof(RemotePortATSCacheClass),
};

static void rp_register_types(void)
{
    type_register_static(&rp_ats_cache_info);
    type_register_static(&rp_ats_info);
}

type_init(rp_register_types)
