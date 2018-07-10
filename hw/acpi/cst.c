#include "qemu/osdep.h"
#include "exec/address-spaces.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/cst.h"
#include "hw/acpi/acpi.h"
#include "hw/nvram/fw_cfg.h"

#define ACPI_SCRATCH_BUFFER_NAME "etc/scratch"

/* Hack! Incomplete! */
static Aml *build_cst_package(void)
{
    int i;
    Aml *crs;
    Aml *pkg;
    int cs_num = 3;

    pkg = aml_package(cs_num + 1); /* # of ACPI Cx states + state count */
    aml_append(pkg, aml_int(cs_num)); /* # of ACPI Cx states */

    for (i = 0; i < cs_num; i++) {
        Aml *cstate = aml_package(4);

        crs = aml_resource_template();
        aml_append(crs, aml_register(AML_AS_SYSTEM_IO,
            0x8,
            0x0,
            0x100,
            0x1));
        aml_append(cstate, crs);
        aml_append(cstate, aml_int(i + 1)); /* Cx ACPI state */
        aml_append(cstate, aml_int((i + 1) * 10)); /* Latency */
        aml_append(cstate, aml_int(cs_num - i - 1));/* Power */
        aml_append(pkg, cstate);
    }

    return pkg;
}

static GArray *cst_scratch;

/*
 * Add an SSDT with a dynamic method named CCST. The method uses the specified
 * ioport to load a table from QEMU, then returns an object named CSTL from
 * it.
 * Everything is scoped under \\_SB.CPUS.CSTP.
 */
void cst_build_acpi(GArray *table_data, BIOSLinker *linker, uint16_t ioport)
{
    Aml *ssdt, *scope, *field, *method;
    uint32_t cstp_offset;

    /* Put this in a separate SSDT table */
    ssdt = init_aml_allocator();

    /* Reserve space for header */
    acpi_data_push(ssdt->buf, sizeof(AcpiTableHeader));

    cstp_offset = table_data->len +
        build_append_named_dword(ssdt->buf, "\\_SB.CPUS.CSTP");
    scope = aml_scope("\\_SB.CPUS");
    {
        /* buffer in reserved memory to load the table from */
        aml_append(scope, aml_operation_region("CSTB", AML_SYSTEM_MEMORY,
                                               aml_name("\\_SB.CPUS.CSTP"),
                                               4096));
        /* write address here to update the table in memory */
        aml_append(scope, aml_operation_region("CSTR", AML_SYSTEM_IO,
                                               aml_int(ioport),
                                               4));
        field = aml_field("CSTR", AML_DWORD_ACC, AML_LOCK,
                          AML_WRITE_AS_ZEROS);
        {
            aml_append(field, aml_named_field("CSTU", 32));
        }
        aml_append(scope, field);
        method = aml_method("CCST", 0, AML_SERIALIZED);
        {
            Aml *ddbhandle = aml_local(0);
            Aml *cst = aml_local(1);
            /* Write buffer address to update table in memory. */
            aml_append(method, aml_store(aml_name("CSTP"), aml_name("CSTU")));
            aml_append(method, aml_load("CSTB", ddbhandle));
            aml_append(method, aml_store(aml_name("CSTL"), cst));
            aml_append(method, aml_unload(ddbhandle));
            aml_append(method, aml_return(cst));
        }
        aml_append(scope, method);
    }
    aml_append(ssdt, scope);
    g_array_append_vals(table_data, ssdt->buf->data, ssdt->buf->len);

    /* Why page boundary? no special reason right now but seems like
     * a good idea for future extensions.      
    */
    bios_linker_loader_alloc(linker, ACPI_SCRATCH_BUFFER_NAME, cst_scratch,
                             4096, false /* page boundary, high memory */);
    /* Patch address of allocated memory into the AML so OSPM can retrieve
     * and read it.
     */
    bios_linker_loader_add_pointer(linker,
        ACPI_BUILD_TABLE_FILE, cstp_offset, sizeof(uint32_t),
        ACPI_SCRATCH_BUFFER_NAME, 0);

    //table_data->data[cstp_offset] = 0x8; /* hack */

    build_header(linker, table_data,
        (void *)(table_data->data + table_data->len - ssdt->buf->len),
        "SSDT", ssdt->buf->len, 1, NULL, "CSTSSDT");

    free_aml_allocator();
}

static GArray *cst_ssdt;

static void cst_ssdt_setup(void)
{
    AcpiTableHeader *dyn_ssdt_hdr;
    Aml *dyn_ssdt = init_aml_allocator();

    /* Reserve space for header */
    acpi_data_push(dyn_ssdt->buf, sizeof(AcpiTableHeader));
    aml_append(dyn_ssdt, aml_name_decl("\\_SB.CPUS.CSTL", build_cst_package()));

    dyn_ssdt_hdr = (AcpiTableHeader *)dyn_ssdt->buf->data;

    acpi_init_header(dyn_ssdt_hdr, "SSDT", dyn_ssdt->buf->len, 1, NULL, "DYNSSDT");

    dyn_ssdt_hdr->checksum = acpi_checksum((uint8_t *)dyn_ssdt_hdr,
                                           dyn_ssdt->buf->len);

    /* dyn_ssdt->buf will be freed. copy to cst_ssdt */
    cst_ssdt = g_array_new(false, true, 1);
    g_array_append_vals(cst_ssdt, dyn_ssdt->buf->data, dyn_ssdt->buf->len);

    free_aml_allocator();
}

/* Update CST in system memory */
static void cst_ioport_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    assert(cst_ssdt);

    cpu_physical_memory_write(data, cst_ssdt->data, cst_ssdt->len);
}

static const MemoryRegionOps cst_ops = {
    .write = cst_ioport_write,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static MemoryRegion cst_mr;

void cst_register(FWCfgState *s, uint16_t ioport)
{
    cst_ssdt_setup();

    /* Allocate guest scratch memory for the table */
    cst_scratch = g_array_new(false, true, 1);
    acpi_data_push(cst_scratch, 4096);
    fw_cfg_add_file(s, ACPI_SCRATCH_BUFFER_NAME, cst_scratch->data,
                    cst_scratch->len);

    /* setup io to trigger updates */
    memory_region_init_io(&cst_mr, NULL, &cst_ops, NULL, "cst-update-request", 4);
    memory_region_add_subregion(get_system_io(), ioport, &cst_mr);
}

/* TODO: API to notify guest of changes */
