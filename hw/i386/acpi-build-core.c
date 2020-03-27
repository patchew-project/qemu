/* Support for generating ACPI tables and passing them to Guests
 *
 * Copyright (C) 2008-2010  Kevin O'Connor <kevin@koconnor.net>
 * Copyright (C) 2006 Fabrice Bellard
 * Copyright (C) 2013 Red Hat Inc
 *
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
#include "qapi/error.h"

#include "exec/memory.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/bios-linker-loader.h"
#include "hw/acpi/tpm.h"
#include "hw/acpi/utils.h"
#include "hw/acpi/vmgenid.h"
#include "hw/boards.h"
#include "hw/i386/pc.h"
#include "migration/vmstate.h"
#include "sysemu/reset.h"
#include "sysemu/tpm.h"

#include "acpi-build.h"
#include "acpi-build-internal.h"

static void acpi_build(AcpiBuildTables *tables, MachineState *machine)
{
    acpi_build_pc(tables, machine);
}

static void acpi_ram_update(MemoryRegion *mr, GArray *data)
{
    uint32_t size = acpi_data_len(data);

    /* Make sure RAM size is correct - in case it got changed e.g. by migration */
    memory_region_ram_resize(mr, size, &error_abort);

    memcpy(memory_region_get_ram_ptr(mr), data->data, size);
    memory_region_set_dirty(mr, 0, size);
}

static void acpi_build_update(void *build_opaque)
{
    AcpiBuildState *build_state = build_opaque;
    AcpiBuildTables tables;

    /* No state to update or already patched? Nothing to do. */
    if (!build_state || build_state->patched) {
        return;
    }
    build_state->patched = 1;

    acpi_build_tables_init(&tables);

    acpi_build(&tables, MACHINE(qdev_get_machine()));

    acpi_ram_update(build_state->table_mr, tables.table_data);

    if (build_state->rsdp) {
        memcpy(build_state->rsdp, tables.rsdp->data, acpi_data_len(tables.rsdp));
    } else {
        acpi_ram_update(build_state->rsdp_mr, tables.rsdp);
    }

    acpi_ram_update(build_state->linker_mr, tables.linker->cmd_blob);
    acpi_build_tables_cleanup(&tables, true);
}

static void acpi_build_reset(void *build_opaque)
{
    AcpiBuildState *build_state = build_opaque;
    build_state->patched = 0;
}

static const VMStateDescription vmstate_acpi_build = {
    .name = "acpi_build",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(patched, AcpiBuildState),
        VMSTATE_END_OF_LIST()
    },
};

void acpi_setup(void)
{
    PCMachineState *pcms = PC_MACHINE(qdev_get_machine());
    PCMachineClass *pcmc = PC_MACHINE_GET_CLASS(pcms);
    X86MachineState *x86ms = X86_MACHINE(pcms);
    AcpiBuildTables tables;
    AcpiBuildState *build_state;
    Object *vmgenid_dev;
    TPMIf *tpm;
    static FwCfgTPMConfig tpm_config;

    if (!x86ms->fw_cfg) {
        ACPI_BUILD_DPRINTF("No fw cfg. Bailing out.\n");
        return;
    }

    if (!pcms->acpi_build_enabled) {
        ACPI_BUILD_DPRINTF("ACPI build disabled. Bailing out.\n");
        return;
    }

    if (!acpi_enabled) {
        ACPI_BUILD_DPRINTF("ACPI disabled. Bailing out.\n");
        return;
    }

    build_state = g_malloc0(sizeof *build_state);

    acpi_build_tables_init(&tables);
    acpi_build(&tables, MACHINE(pcms));

    /* Now expose it all to Guest */
    build_state->table_mr = acpi_add_rom_blob(acpi_build_update,
                                              build_state, tables.table_data,
                                              ACPI_BUILD_TABLE_FILE,
                                              ACPI_BUILD_TABLE_MAX_SIZE);
    assert(build_state->table_mr != NULL);

    build_state->linker_mr =
        acpi_add_rom_blob(acpi_build_update, build_state,
                          tables.linker->cmd_blob, "etc/table-loader", 0);

    fw_cfg_add_file(x86ms->fw_cfg, ACPI_BUILD_TPMLOG_FILE,
                    tables.tcpalog->data, acpi_data_len(tables.tcpalog));

    tpm = tpm_find();
    if (tpm && object_property_get_bool(OBJECT(tpm), "ppi", &error_abort)) {
        tpm_config = (FwCfgTPMConfig) {
            .tpmppi_address = cpu_to_le32(TPM_PPI_ADDR_BASE),
            .tpm_version = tpm_get_version(tpm),
            .tpmppi_version = TPM_PPI_VERSION_1_30
        };
        fw_cfg_add_file(x86ms->fw_cfg, "etc/tpm/config",
                        &tpm_config, sizeof tpm_config);
    }

    vmgenid_dev = find_vmgenid_dev();
    if (vmgenid_dev) {
        vmgenid_add_fw_cfg(VMGENID(vmgenid_dev), x86ms->fw_cfg,
                           tables.vmgenid);
    }

    if (!pcmc->rsdp_in_ram) {
        /*
         * Keep for compatibility with old machine types.
         * Though RSDP is small, its contents isn't immutable, so
         * we'll update it along with the rest of tables on guest access.
         */
        uint32_t rsdp_size = acpi_data_len(tables.rsdp);

        build_state->rsdp = g_memdup(tables.rsdp->data, rsdp_size);
        fw_cfg_add_file_callback(x86ms->fw_cfg, ACPI_BUILD_RSDP_FILE,
                                 acpi_build_update, NULL, build_state,
                                 build_state->rsdp, rsdp_size, true);
        build_state->rsdp_mr = NULL;
    } else {
        build_state->rsdp = NULL;
        build_state->rsdp_mr = acpi_add_rom_blob(acpi_build_update,
                                                 build_state, tables.rsdp,
                                                 ACPI_BUILD_RSDP_FILE, 0);
    }

    qemu_register_reset(acpi_build_reset, build_state);
    acpi_build_reset(build_state);
    vmstate_register(NULL, 0, &vmstate_acpi_build, build_state);

    /* Cleanup tables but don't free the memory: we track it
     * in build_state.
     */
    acpi_build_tables_cleanup(&tables, false);
}
