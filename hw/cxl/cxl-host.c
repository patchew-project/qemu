/*
 * CXL host parameter parsing routines
 *
 * Copyright (c) 2022 Huawei
 * Modeled loosely on the NUMA options handling in hw/core/numa.c
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/bitmap.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "system/qtest.h"
#include "hw/boards.h"
#include "qemu/log.h"

#include "qapi/qapi-visit-machine.h"
#include "hw/cxl/cxl.h"
#include "hw/cxl/cxl_host.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pci_host.h"
#include "hw/pci/pcie_port.h"
#include "hw/pci-bridge/pci_expander_bridge.h"

static void cxl_fixed_memory_window_config(CXLFixedMemoryWindowOptions *object,
                                           int index, Error **errp)
{
    ERRP_GUARD();
    DeviceState *dev = qdev_new(TYPE_CXL_FMW);
    CXLFixedWindow *fw = CXL_FMW(dev);
    strList *target;
    int i;

    fw->index = index;

    for (target = object->targets; target; target = target->next) {
        fw->num_targets++;
    }

    fw->enc_int_ways = cxl_interleave_ways_enc(fw->num_targets, errp);
    if (*errp) {
        return;
    }

    if (object->size % (256 * MiB)) {
        error_setg(errp,
                   "Size of a CXL fixed memory window must be a multiple of 256MiB");
        return;
    }
    fw->size = object->size;

    if (object->has_interleave_granularity) {
        fw->enc_int_gran =
            cxl_interleave_granularity_enc(object->interleave_granularity,
                                           errp);
        if (*errp) {
            return;
        }
    } else {
        /* Default to 256 byte interleave */
        fw->enc_int_gran = 0;
    }

    fw->targets = g_malloc0_n(fw->num_targets, sizeof(*fw->targets));
    for (i = 0, target = object->targets; target; i++, target = target->next) {
        /* This link cannot be resolved yet, so stash the name for now */
        fw->targets[i] = g_strdup(target->value);
    }

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), errp);
}

static int cxl_fmws_link(Object *obj, void *opaque)
{
    Error **errp = opaque;
    struct CXLFixedWindow *fw;
    int i;

    if (!object_dynamic_cast(obj, TYPE_CXL_FMW)) {
        return 0;
    }
    fw = CXL_FMW(obj);

    for (i = 0; i < fw->num_targets; i++) {
        Object *o;
        bool ambig;

        o = object_resolve_path_type(fw->targets[i], TYPE_PXB_CXL_DEV,
                                     &ambig);
        if (!o) {
            error_setg(errp, "Could not resolve CXLFM target %s",
                       fw->targets[i]);
            return -1;
        }
        fw->target_hbs[i] = PXB_CXL_DEV(o);
    }
    return 0;
}

void cxl_fmws_link_targets(Error **errp)
{
    /* Order doesn't matter for this, so no need to build list */
    object_child_foreach_recursive(object_get_root(), cxl_fmws_link, errp);
}

static bool cxl_hdm_find_target(uint32_t *cache_mem, hwaddr addr,
                                uint8_t *target, bool *interleaved)
{
    int hdm_inc = R_CXL_HDM_DECODER1_BASE_LO - R_CXL_HDM_DECODER0_BASE_LO;
    unsigned int hdm_count;
    bool found = false;
    int i;
    uint32_t cap;

    cap = ldl_le_p(cache_mem + R_CXL_HDM_DECODER_CAPABILITY);
    hdm_count = cxl_decoder_count_dec(FIELD_EX32(cap,
                                                 CXL_HDM_DECODER_CAPABILITY,
                                                 DECODER_COUNT));
    for (i = 0; i < hdm_count; i++) {
        uint32_t ctrl, ig_enc, iw_enc, target_idx;
        uint32_t low, high;
        uint64_t base, size;

        low = ldl_le_p(cache_mem + R_CXL_HDM_DECODER0_BASE_LO + i * hdm_inc);
        high = ldl_le_p(cache_mem + R_CXL_HDM_DECODER0_BASE_HI + i * hdm_inc);
        base = (low & 0xf0000000) | ((uint64_t)high << 32);
        low = ldl_le_p(cache_mem + R_CXL_HDM_DECODER0_SIZE_LO + i * hdm_inc);
        high = ldl_le_p(cache_mem + R_CXL_HDM_DECODER0_SIZE_HI + i * hdm_inc);
        size = (low & 0xf0000000) | ((uint64_t)high << 32);
        if (addr < base || addr >= base + size) {
            continue;
        }

        ctrl = ldl_le_p(cache_mem + R_CXL_HDM_DECODER0_CTRL + i * hdm_inc);
        if (!FIELD_EX32(ctrl, CXL_HDM_DECODER0_CTRL, COMMITTED)) {
            return false;
        }
        found = true;
        ig_enc = FIELD_EX32(ctrl, CXL_HDM_DECODER0_CTRL, IG);
        iw_enc = FIELD_EX32(ctrl, CXL_HDM_DECODER0_CTRL, IW);

        if (interleaved) {
            *interleaved = iw_enc != 0;
        }

        target_idx = (addr / cxl_decode_ig(ig_enc)) % (1 << iw_enc);

        if (target_idx < 4) {
            uint32_t val = ldl_le_p(cache_mem +
                                    R_CXL_HDM_DECODER0_TARGET_LIST_LO +
                                    i * hdm_inc);
            *target = extract32(val, target_idx * 8, 8);
        } else {
            uint32_t val = ldl_le_p(cache_mem +
                                    R_CXL_HDM_DECODER0_TARGET_LIST_HI +
                                    i * hdm_inc);
            *target = extract32(val, (target_idx - 4) * 8, 8);
        }
        break;
    }

    return found;
}

static PCIDevice *cxl_cfmws_find_device(CXLFixedWindow *fw, hwaddr addr)
{
    CXLComponentState *hb_cstate, *usp_cstate;
    PCIHostState *hb;
    CXLUpstreamPort *usp;
    int rb_index;
    uint32_t *cache_mem;
    uint8_t target;
    bool target_found;
    PCIDevice *rp, *d;

    /* Address is relative to memory region. Convert to HPA */
    addr += fw->base;

    rb_index = (addr / cxl_decode_ig(fw->enc_int_gran)) % fw->num_targets;
    hb = PCI_HOST_BRIDGE(fw->target_hbs[rb_index]->cxl_host_bridge);
    if (!hb || !hb->bus || !pci_bus_is_cxl(hb->bus)) {
        return NULL;
    }

    if (cxl_get_hb_passthrough(hb)) {
        rp = pcie_find_port_first(hb->bus);
        if (!rp) {
            return NULL;
        }
    } else {
        hb_cstate = cxl_get_hb_cstate(hb);
        if (!hb_cstate) {
            return NULL;
        }

        cache_mem = hb_cstate->crb.cache_mem_registers;

        target_found = cxl_hdm_find_target(cache_mem, addr, &target, NULL);
        if (!target_found) {
            return NULL;
        }

        rp = pcie_find_port_by_pn(hb->bus, target);
        if (!rp) {
            return NULL;
        }
    }

    d = pci_bridge_get_sec_bus(PCI_BRIDGE(rp))->devices[0];
    if (!d) {
        return NULL;
    }

    if (object_dynamic_cast(OBJECT(d), TYPE_CXL_TYPE3)) {
        return d;
    }

    /*
     * Could also be a switch.  Note only one level of switching currently
     * supported.
     */
    if (!object_dynamic_cast(OBJECT(d), TYPE_CXL_USP)) {
        return NULL;
    }
    usp = CXL_USP(d);

    usp_cstate = cxl_usp_to_cstate(usp);
    if (!usp_cstate) {
        return NULL;
    }

    cache_mem = usp_cstate->crb.cache_mem_registers;

    target_found = cxl_hdm_find_target(cache_mem, addr, &target, NULL);
    if (!target_found) {
        return NULL;
    }

    d = pcie_find_port_by_pn(&PCI_BRIDGE(d)->sec_bus, target);
    if (!d) {
        return NULL;
    }

    d = pci_bridge_get_sec_bus(PCI_BRIDGE(d))->devices[0];
    if (!d) {
        return NULL;
    }

    if (!object_dynamic_cast(OBJECT(d), TYPE_CXL_TYPE3)) {
        return NULL;
    }

    return d;
}

static bool cfmws_is_not_interleaved(CXLFixedWindow *fw, hwaddr addr)
{
    PCIDevice *rp, *d;
    PCIHostState *hb;
    CXLComponentState *hb_cstate, *usp_cstate;
    CXLUpstreamPort *usp;
    uint32_t *cache_mem;
    bool target_found, interleaved;
    uint8_t target;

    addr = fw->base;
    if (fw->num_targets > 1) {
        return false;
    }

    hb = PCI_HOST_BRIDGE(fw->target_hbs[0]->cxl_host_bridge);
    if (!hb || !hb->bus || !pci_bus_is_cxl(hb->bus)) {
        return false;
    }
    if (cxl_get_hb_passthrough(hb)) {
        rp = pcie_find_port_first(hb->bus);
        if (!rp) {
            return false;
        }
    } else {
        hb_cstate = cxl_get_hb_cstate(hb);
        if (!hb_cstate) {
            return false;
        }
        cache_mem = hb_cstate->crb.cache_mem_registers;

        target_found = cxl_hdm_find_target(cache_mem, addr, &target,
                                           &interleaved);
        if (!target_found) {
            return false;
        }
        if (interleaved) {
            return false;
        }

        rp = pcie_find_port_by_pn(hb->bus, target);
        if (!rp) {
            return false;
        }
    }
    d = pci_bridge_get_sec_bus(PCI_BRIDGE(rp))->devices[0];
    if (!d) {
        return false;
    }

    if (object_dynamic_cast(OBJECT(d), TYPE_CXL_TYPE3)) {
        return true;
    }
    /*
     * Could also be a switch.  Note only one level of switching currently
     * supported.
     */
    if (!object_dynamic_cast(OBJECT(d), TYPE_CXL_USP)) {
        return false;
    }
    usp = CXL_USP(d);

    usp_cstate = cxl_usp_to_cstate(usp);
    if (!usp_cstate) {
        return false;
    }

    cache_mem = usp_cstate->crb.cache_mem_registers;

    target_found = cxl_hdm_find_target(cache_mem, addr, &target,
                                       &interleaved);
    if (!target_found) {
        return false;
    }
    if (interleaved) {
        return false;
    }

    d = pcie_find_port_by_pn(&PCI_BRIDGE(d)->sec_bus, target);
    if (!d) {
        return false;
    }

    d = pci_bridge_get_sec_bus(PCI_BRIDGE(d))->devices[0];
    if (!d) {
        return false;
    }

    if (!object_dynamic_cast(OBJECT(d), TYPE_CXL_TYPE3)) {
        return false;
    }

    return true;
}

static int cxl_fmws_direct_passthrough(Object *obj, void *opaque)
{
    struct cxl_direct_pt_state *state = opaque;
    struct CXLFixedWindow *fw;
    CXLType3Dev *ct3d = state->ct3d;

    if (!object_dynamic_cast(obj, TYPE_CXL_FMW)) {
        return 0;
    }

    fw = CXL_FMW(obj);

    if (!cfmws_is_not_interleaved(fw, state->decoder_base)) {
        return 0;
    }

    if (state->commit) {
        MemoryRegion *mr = NULL;
        uint64_t vmr_size = 0, pmr_size = 0;
        uint64_t offset = 0;

        if (ct3d->hostvmem) {
            MemoryRegion *vmr = host_memory_backend_get_memory(ct3d->hostvmem);
            vmr_size = memory_region_size(vmr);
            if (state->dpa_base < vmr_size) {
                mr = vmr;
                offset = state->dpa_base;
            }
        }
        if (!mr && ct3d->hostpmem) {
            MemoryRegion *pmr = host_memory_backend_get_memory(ct3d->hostpmem);
            pmr_size = memory_region_size(pmr);
            if (state->dpa_base - vmr_size < pmr_size) {
                mr = pmr;
                offset = state->dpa_base - vmr_size;
            }
        }

        if (!mr) {
            return 0;
        }

        if (memory_region_is_mapped(&ct3d->direct_mr[state->hdm_decoder_idx])) {
            return 0;
        }

        memory_region_init_alias(&ct3d->direct_mr[state->hdm_decoder_idx],
                                 OBJECT(ct3d), "direct-mapping", mr, offset,
                                 state->decoder_size);
        memory_region_add_subregion(&fw->mr,
                                    state->decoder_base - fw->base,
                                    &ct3d->direct_mr[state->hdm_decoder_idx]);
    } else {
        if (memory_region_is_mapped(&ct3d->direct_mr[state->hdm_decoder_idx])) {
            memory_region_del_subregion(&fw->mr,
                &ct3d->direct_mr[state->hdm_decoder_idx]);
        }
    }

    return 0;
}

static int update_non_interleaved(Object *obj, void *opaque)
{
    CXLType3Dev *ct3d;
    uint32_t *cache_mem;
    unsigned int hdm_count, i;
    uint32_t cap;
    int hdm_inc = R_CXL_HDM_DECODER1_BASE_LO - R_CXL_HDM_DECODER0_BASE_LO;
    uint64_t dpa_base = 0;
    bool commit = *(bool *) opaque;

    if (!object_dynamic_cast(obj, TYPE_CXL_TYPE3)) {
        return 0;
    }

    ct3d = CXL_TYPE3(obj);
    cache_mem = ct3d->cxl_cstate.crb.cache_mem_registers;
    /*
     * Walk the decoders and find any committed with iw set to 0
     * (non interleaved).
     */
    cap = ldl_le_p(cache_mem + R_CXL_HDM_DECODER_CAPABILITY);
    hdm_count = cxl_decoder_count_dec(FIELD_EX32(cap,
                                                 CXL_HDM_DECODER_CAPABILITY,
                                                 DECODER_COUNT));

    /* Now for each committed HDM decoder */
    for (i = 0; i < hdm_count; i++) {
        uint64_t decoder_base, decoder_size, skip;
        uint32_t hdm_ctrl, low, high;
        int iw, committed;

        hdm_ctrl = ldl_le_p(cache_mem + R_CXL_HDM_DECODER0_CTRL + i * hdm_inc);
        committed = FIELD_EX32(hdm_ctrl, CXL_HDM_DECODER0_CTRL, COMMITTED);
        if (commit) {
            if (!committed) {
                return 0;
            }
        } else {
            if (committed) {
                return 0;
            }
        }

        /*
         * Even if this decoder is interleaved need to keep track of DPA as the
         * next HDM decoder may not be interleaved.
         */
        low = ldl_le_p(cache_mem + R_CXL_HDM_DECODER0_DPA_SKIP_LO +
                       i * hdm_inc);
        high = ldl_le_p(cache_mem + R_CXL_HDM_DECODER0_DPA_SKIP_HI +
                        i * hdm_inc);
        skip = ((uint64_t)high << 32) | (low & 0xf0000000);
        dpa_base += skip;

        low = ldl_le_p(cache_mem + R_CXL_HDM_DECODER0_SIZE_LO + i * hdm_inc);
        high = ldl_le_p(cache_mem + R_CXL_HDM_DECODER0_SIZE_HI + i * hdm_inc);
        decoder_size = ((uint64_t)high << 32) | (low & 0xf0000000);
        iw = FIELD_EX32(hdm_ctrl, CXL_HDM_DECODER0_CTRL, IW);
        /* Get the HPA of the base */
        low = ldl_le_p(cache_mem + R_CXL_HDM_DECODER0_BASE_LO + i * hdm_inc);
        high = ldl_le_p(cache_mem + R_CXL_HDM_DECODER0_BASE_HI + i * hdm_inc);
        decoder_base = ((uint64_t)high << 32) | (low & 0xf0000000);

#ifdef DEBUG
        qemu_log("non interleaved decoder %lx %lx %lx %d\n", decoder_base,
                 decoder_size, dpa_base, commit);
#endif

        /* Is it non interleaved? - need to check full path later */
        if (iw == 0) {
            struct cxl_direct_pt_state state = {
                .ct3d = ct3d,
                .decoder_base = decoder_base,
                .decoder_size = decoder_size,
                .dpa_base = dpa_base,
                .hdm_decoder_idx = i,
                .commit = commit,
            };
            object_child_foreach_recursive(object_get_root(),
                                           cxl_fmws_direct_passthrough, &state);
        }
        dpa_base += decoder_size / cxl_interleave_ways_dec(iw, &error_fatal);

    }
    return 0;
}

bool cfmws_update_non_interleaved(bool commit)
{
    /*
     * Walk endpoints to find committed decoders then check if they are not
     * interleaved (but path full is set up).
     */
    object_child_foreach_recursive(object_get_root(),
                                   update_non_interleaved, &commit);

    return false;
}

static MemTxResult cxl_read_cfmws(void *opaque, hwaddr addr, uint64_t *data,
                                  unsigned size, MemTxAttrs attrs)
{
    CXLFixedWindow *fw = opaque;
    PCIDevice *d;

    d = cxl_cfmws_find_device(fw, addr);
    if (d == NULL) {
        *data = 0;
        /* Reads to invalid address return poison */
        return MEMTX_ERROR;
    }

    return cxl_type3_read(d, addr + fw->base, data, size, attrs);
}

static MemTxResult cxl_write_cfmws(void *opaque, hwaddr addr,
                                   uint64_t data, unsigned size,
                                   MemTxAttrs attrs)
{
    CXLFixedWindow *fw = opaque;
    PCIDevice *d;

    d = cxl_cfmws_find_device(fw, addr);
    if (d == NULL) {
        /* Writes to invalid address are silent */
        return MEMTX_OK;
    }

    return cxl_type3_write(d, addr + fw->base, data, size, attrs);
}

const MemoryRegionOps cfmws_ops = {
    .read_with_attrs = cxl_read_cfmws,
    .write_with_attrs = cxl_write_cfmws,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = true,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = true,
    },
};

static void machine_get_cxl(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    CXLState *cxl_state = opaque;
    bool value = cxl_state->is_enabled;

    visit_type_bool(v, name, &value, errp);
}

static void machine_set_cxl(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    CXLState *cxl_state = opaque;
    bool value;

    if (!visit_type_bool(v, name, &value, errp)) {
        return;
    }
    cxl_state->is_enabled = value;
}

static void machine_get_cfmw(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    CXLState *state = opaque;
    CXLFixedMemoryWindowOptionsList **list = &state->cfmw_list;

    visit_type_CXLFixedMemoryWindowOptionsList(v, name, list, errp);
}

static void machine_set_cfmw(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    CXLState *state = opaque;
    CXLFixedMemoryWindowOptionsList *cfmw_list = NULL;
    CXLFixedMemoryWindowOptionsList *it;
    int index;

    visit_type_CXLFixedMemoryWindowOptionsList(v, name, &cfmw_list, errp);
    if (!cfmw_list) {
        return;
    }

    for (it = cfmw_list, index = 0; it; it = it->next, index++) {
        cxl_fixed_memory_window_config(it->value, index, errp);
    }
    state->cfmw_list = cfmw_list;
}

void cxl_machine_init(Object *obj, CXLState *state)
{
    object_property_add(obj, "cxl", "bool", machine_get_cxl,
                        machine_set_cxl, NULL, state);
    object_property_set_description(obj, "cxl",
                                    "Set on/off to enable/disable "
                                    "CXL instantiation");

    object_property_add(obj, "cxl-fmw", "CXLFixedMemoryWindow",
                        machine_get_cfmw, machine_set_cfmw,
                        NULL, state);
    object_property_set_description(obj, "cxl-fmw",
                                    "CXL Fixed Memory Windows (array)");
}

void cxl_hook_up_pxb_registers(PCIBus *bus, CXLState *state, Error **errp)
{
    /* Walk the pci busses looking for pxb busses to hook up */
    if (bus) {
        QLIST_FOREACH(bus, &bus->child, sibling) {
            if (!pci_bus_is_root(bus)) {
                continue;
            }
            if (pci_bus_is_cxl(bus)) {
                if (!state->is_enabled) {
                    error_setg(errp, "CXL host bridges present, but cxl=off");
                    return;
                }
                pxb_cxl_hook_up_registers(state, bus, errp);
            }
        }
    }
}

static int cxl_fmws_find(Object *obj, void *opaque)
{
    GSList **list = opaque;

    if (!object_dynamic_cast(obj, TYPE_CXL_FMW)) {
        return 0;
    }
    *list = g_slist_prepend(*list, obj);

    return 0;
}

static GSList *cxl_fmws_get_all(void)
{
    GSList *list = NULL;

    object_child_foreach_recursive(object_get_root(), cxl_fmws_find, &list);

    return list;
}

static gint cfmws_cmp(gconstpointer a, gconstpointer b, gpointer d)
{
    const struct CXLFixedWindow *ap = a;
    const struct CXLFixedWindow *bp = b;

    return ap->index > bp->index;
}

GSList *cxl_fmws_get_all_sorted(void)
{
    return g_slist_sort_with_data(cxl_fmws_get_all(), cfmws_cmp, NULL);
}

static int cxl_fmws_mmio_map(Object *obj, void *opaque)
{
    struct CXLFixedWindow *fw;

    if (!object_dynamic_cast(obj, TYPE_CXL_FMW)) {
        return 0;
    }
    fw = CXL_FMW(obj);
    sysbus_mmio_map(SYS_BUS_DEVICE(fw), 0, fw->base);

    return 0;
}

void cxl_fmws_update_mmio(void)
{
    /* Ordering is not required for this */
    object_child_foreach_recursive(object_get_root(), cxl_fmws_mmio_map, NULL);
}

hwaddr cxl_fmws_set_memmap(hwaddr base, hwaddr max_addr)
{
    GSList *cfmws_list, *iter;
    CXLFixedWindow *fw;

    cfmws_list = cxl_fmws_get_all_sorted();
    for (iter = cfmws_list; iter; iter = iter->next) {
        fw = CXL_FMW(iter->data);
        if (base + fw->size <= max_addr) {
            fw->base = base;
            base += fw->size;
        }
    }
    g_slist_free(cfmws_list);

    return base;
}

static void cxl_fmw_realize(DeviceState *dev, Error **errp)
{
    CXLFixedWindow *fw = CXL_FMW(dev);

    memory_region_init_io(&fw->mr, OBJECT(dev), &cfmws_ops, fw,
                          "cxl-fixed-memory-region", fw->size);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &fw->mr);
}

/*
 * Note: Fixed memory windows represent fixed address decoders on the host and
 * as such have no dynamic state to reset or migrate
 */
static void cxl_fmw_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "CXL Fixed Memory Window";
    dc->realize = cxl_fmw_realize;
    /* Reason - created by machines as tightly coupled to machine memory map */
    dc->user_creatable = false;
}

static const TypeInfo cxl_fmw_info = {
    .name = TYPE_CXL_FMW,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(CXLFixedWindow),
    .class_init = cxl_fmw_class_init,
};

static void cxl_host_register_types(void)
{
    type_register_static(&cxl_fmw_info);
}
type_init(cxl_host_register_types)
