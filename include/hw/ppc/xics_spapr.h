/*
 * QEMU PowerPC pSeries Logical Partition (aka sPAPR) hardware System Emulator
 *
 * PAPR Virtualized Interrupt System, aka ICS/ICP aka xics
 *
 * Copyright (c) 2010, 2011 David Gibson, IBM Corporation.
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

#ifndef XICS_SPAPR_H
#define XICS_SPAPR_H

#include "hw/ppc/spapr.h"
#include "hw/ppc/xics.h"
#include "qom/object.h"

typedef struct IcsSpaprState {
    /*< private >*/
    ICPState parent_obj;

    /*
     * The ICS needs to know the upper limit to vCPU ids it
     * might be exposed to in order to size the vCPU id range
     * in "ibm,interrupt-server-ranges" and to optimize HW
     * resource allocation when using the XICS-on-XIVE KVM
     * device. It is the purpose of the "nr-servers" property
     * which *must* be set to a non-null value before realizing
     * the ICS.
     */
    uint32_t nr_servers;
} IcsSpaprState;

#define TYPE_ICS_SPAPR "ics-spapr"
DECLARE_INSTANCE_CHECKER(IcsSpaprState, ICS_SPAPR, TYPE_ICS_SPAPR)

int xics_kvm_connect(SpaprInterruptController *intc, uint32_t nr_servers,
                     Error **errp);
void xics_kvm_disconnect(SpaprInterruptController *intc);
bool xics_kvm_has_broken_disconnect(void);

#endif /* XICS_SPAPR_H */
