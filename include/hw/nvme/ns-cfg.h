/*
 * QEMU NVM Express Virtual Dynamic Namespace Management
 * Common configuration handling for qemu-img tool and qemu-system-xx
 *
 *
 * Copyright (c) 2022 Solidigm
 *
 * Authors:
 *  Michael Kropaczek      <michael.kropaczek@solidigm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See the
 * COPYING file in the top-level directory.
 *
 */

#ifdef NS_CFG_DEF
NS_CFG_DEF(int, "params.nsid", (int64_t)ns->params.nsid, nsid)
NS_CFG_DEF(obj, "attached_ctrls", QOBJECT(ctrl_qlist), QOBJECT(ctrl_qlist))
NS_CFG_DEF(int, "params.pi", (int64_t)ns->params.pi, 0)
NS_CFG_DEF(int, "lbasz", (int64_t)ns->lbasz, 0)
NS_CFG_DEF(int, "id_ns.nsze", le64_to_cpu(ns->id_ns.nsze), 0)
NS_CFG_DEF(int, "id_ns.ncap", le64_to_cpu(ns->id_ns.ncap), 0)
NS_CFG_DEF(int, "id_ns.nuse", le64_to_cpu(ns->id_ns.nuse), 0)
NS_CFG_DEF(int, "id_ns.nsfeat", (int64_t)ns->id_ns.nsfeat, 0)
NS_CFG_DEF(int, "id_ns.flbas", (int64_t)ns->id_ns.flbas, 0)
NS_CFG_DEF(int, "id_ns.nmic", (int64_t)ns->id_ns.nmic, 0)
NS_CFG_DEF(int, "ns_size", ns->size, 0)
#endif
