/*
 * Support for generating PCI related ACPI tables and passing them to Guests
 *
 * Copyright (C) 2006 Fabrice Bellard
 * Copyright (C) 2008-2010  Kevin O'Connor <kevin@koconnor.net>
 * Copyright (C) 2013-2019 Red Hat Inc
 * Copyright (C) 2019 Intel Corporation
 *
 * Author: Wei Yang <richardw.yang@linux.intel.com>
 * Author: Michael S. Tsirkin <mst@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/pci.h"
#include "hw/pci/pcie_host.h"
#include "hw/acpi/pcihp.h"

void build_mcfg(GArray *table_data, BIOSLinker *linker, AcpiMcfgInfo *info)
{
    int mcfg_start = table_data->len;

    /*
     * PCI Firmware Specification, Revision 3.0
     * 4.1.2 MCFG Table Description.
     */
    acpi_data_push(table_data, sizeof(AcpiTableHeader));
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 8);

    /*
     * Memory Mapped Enhanced Configuration Space Base Address Allocation
     * Structure
     */
    /* Base address, processor-relative */
    build_append_int_noprefix(table_data, info->base, 8);
    /* PCI segment group number */
    build_append_int_noprefix(table_data, 0, 2);
    /* Starting PCI Bus number */
    build_append_int_noprefix(table_data, 0, 1);
    /* Final PCI Bus number */
    build_append_int_noprefix(table_data, PCIE_MMCFG_BUS(info->size - 1), 1);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 4);

    build_header(linker, table_data, (void *)(table_data->data + mcfg_start),
                 "MCFG", table_data->len - mcfg_start, 1, NULL, NULL);
}

bool vmstate_acpi_pcihp_use_acpi_index(void *opaque, int version_id)
{
     AcpiPciHpState *s = opaque;
     return s->acpi_index;
}

Aml *aml_pci_device_dsm(void)
{
    Aml *method, *UUID, *ifctx, *ifctx1, *ifctx2, *ifctx3, *elsectx;
    Aml *acpi_index = aml_local(0);
    Aml *zero = aml_int(0);
    Aml *bnum = aml_arg(4);
    Aml *sun = aml_arg(5);

    method = aml_method("PDSM", 6, AML_SERIALIZED);

    /*
     * PCI Firmware Specification 3.1
     * 4.6.  _DSM Definitions for PCI
     */
    UUID = aml_touuid("E5C937D0-3553-4D7A-9117-EA4D19C3434D");
    ifctx = aml_if(aml_equal(aml_arg(0), UUID));
    {
        aml_append(ifctx, aml_store(aml_call2("AIDX", bnum, sun), acpi_index));
        ifctx1 = aml_if(aml_equal(aml_arg(2), zero));
        {
            uint8_t byte_list[1];

            ifctx2 = aml_if(aml_equal(aml_arg(1), aml_int(2)));
            {
                /*
                 * advertise function 7 if device has acpi-index
                 */
                ifctx3 = aml_if(aml_lnot(aml_equal(acpi_index, zero)));
                {
                    byte_list[0] =
                        1 /* have supported functions */ |
                        1 << 7 /* support for function 7 */
                    ;
                    aml_append(ifctx3, aml_return(aml_buffer(1, byte_list)));
                }
                aml_append(ifctx2, ifctx3);
             }
             aml_append(ifctx1, ifctx2);

             byte_list[0] = 0; /* nothing supported */
             aml_append(ifctx1, aml_return(aml_buffer(1, byte_list)));
         }
         aml_append(ifctx, ifctx1);
         elsectx = aml_else();
         /*
          * PCI Firmware Specification 3.1
          * 4.6.7. _DSM for Naming a PCI or PCI Express Device Under
          *        Operating Systems
          */
         ifctx1 = aml_if(aml_equal(aml_arg(2), aml_int(7)));
         {
             Aml *pkg = aml_package(2);
             Aml *label = aml_local(2);
             Aml *ret = aml_local(1);

             aml_append(ifctx1, aml_concatenate(aml_string("PCI Device "),
                 aml_to_decimalstring(acpi_index, NULL), label));

             aml_append(pkg, zero);
             aml_append(pkg, aml_string("placeholder"));
             aml_append(ifctx1, aml_store(pkg, ret));
             /*
              * update apci-index to actual value
              */
             aml_append(ifctx1, aml_store(acpi_index, aml_index(ret, zero)));
             /*
              * update device label to actual value
              */
             aml_append(ifctx1, aml_store(label, aml_index(ret, aml_int(1))));
             aml_append(ifctx1, aml_return(ret));
         }
         aml_append(elsectx, ifctx1);
         aml_append(ifctx, elsectx);
    }
    aml_append(method, ifctx);
    return method;
}
