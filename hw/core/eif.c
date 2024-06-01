/*
 * EIF (Enclave Image Format) related helpers
 *
 * Copyright (c) 2024 Dorjoy Chowdhury <dorjoychy111@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qapi/error.h"
#include <zlib.h> /* for crc32 */

#include "hw/core/eif.h"

#define MAX_SECTIONS 32

/* members are ordered according to field order in .eif file */
typedef struct EifHeader {
    uint8_t  magic[4]; /* must be .eif in ascii i.e., [46, 101, 105, 102] */
    uint16_t version;
    uint16_t flags;
    uint64_t default_memory;
    uint64_t default_cpus;
    uint16_t reserved;
    uint16_t section_cnt;
    uint64_t section_offsets[MAX_SECTIONS];
    uint64_t section_sizes[MAX_SECTIONS];
    uint32_t unused;
    uint32_t eif_crc32;
} QEMU_PACKED EifHeader;

/* members are ordered according to field order in .eif file */
typedef struct EifSectionHeader {
    /*
     * 0 = invalid, 1 = kernel, 2 = cmdline, 3 = ramdisk, 4 = signature,
     * 5 = metadata
     */
    uint16_t section_type;
    uint16_t flags;
    uint64_t section_size;
} QEMU_PACKED EifSectionHeader;

enum EifSectionTypes {
    EIF_SECTION_INVALID = 0,
    EIF_SECTION_KERNEL = 1,
    EIF_SECTION_CMDLINE = 2,
    EIF_SECTION_RAMDISK = 3,
    EIF_SECTION_SIGNATURE = 4,
    EIF_SECTION_METADATA = 5,
    EIF_SECTION_MAX = 6,
};

static const char *section_type_to_string(uint16_t type)
{
    const char *str;
    switch (type) {
    case EIF_SECTION_INVALID:
        str = "invalid";
        break;
    case EIF_SECTION_KERNEL:
        str = "kernel";
        break;
    case EIF_SECTION_CMDLINE:
        str = "cmdline";
        break;
    case EIF_SECTION_RAMDISK:
        str = "ramdisk";
        break;
    case EIF_SECTION_SIGNATURE:
        str = "signature";
        break;
    case EIF_SECTION_METADATA:
        str = "metadata";
        break;
    default:
        str = "unknown";
        break;
    }

    return str;
}

static bool read_eif_header(FILE *f, EifHeader *header, uint32_t *crc,
                            Error **errp)
{
    size_t got;
    size_t header_size = sizeof(*header);

    got = fread(header, 1, header_size, f);
    if (got != header_size) {
        error_setg(errp, "Failed to read EIF header");
        return false;
    }

    if (memcmp(header->magic, ".eif", 4) != 0) {
        error_setg(errp, "Invalid EIF image. Magic mismatch.");
        return false;
    }

    /* Exclude header->eif_crc32 field from CRC calculation */
    *crc = crc32(*crc, (uint8_t *)header, header_size - 4);

    header->version = be16_to_cpu(header->version);
    header->flags = be16_to_cpu(header->flags);
    header->default_memory = be64_to_cpu(header->default_memory);
    header->default_cpus = be64_to_cpu(header->default_cpus);
    header->reserved = be16_to_cpu(header->reserved);
    header->section_cnt = be16_to_cpu(header->section_cnt);

    for (int i = 0; i < MAX_SECTIONS; ++i) {
        header->section_offsets[i] = be64_to_cpu(header->section_offsets[i]);
    }

    for (int i = 0; i < MAX_SECTIONS; ++i) {
        header->section_sizes[i] = be64_to_cpu(header->section_sizes[i]);
    }

    header->unused = be32_to_cpu(header->unused);
    header->eif_crc32 = be32_to_cpu(header->eif_crc32);
    return true;
}

static bool read_eif_section_header(FILE *f, EifSectionHeader *section_header,
                                    uint32_t *crc, Error **errp)
{
    size_t got;
    size_t section_header_size = sizeof(*section_header);

    got = fread(section_header, 1, section_header_size, f);
    if (got != section_header_size) {
        error_setg(errp, "Failed to read EIF section header");
        return false;
    }

    *crc = crc32(*crc, (uint8_t *)section_header, section_header_size);

    section_header->section_type = be16_to_cpu(section_header->section_type);
    section_header->flags = be16_to_cpu(section_header->flags);
    section_header->section_size = be64_to_cpu(section_header->section_size);
    return true;
}

/*
 * Upon success, the caller is responsible for unlinking and freeing *tmp_path.
 */
static bool get_tmp_file(const char *template, char **tmp_path, Error **errp)
{
    int tmp_fd;

    *tmp_path = NULL;
    tmp_fd = g_file_open_tmp(template, tmp_path, NULL);
    if (tmp_fd < 0 || *tmp_path == NULL) {
        error_setg(errp, "Failed to create temporary file for template %s",
                   template);
        return false;
    }

    close(tmp_fd);
    return true;
}

static void safe_fclose(FILE *f)
{
    if (f) {
        fclose(f);
    }
}

static void safe_unlink(char *f)
{
    if (f) {
        unlink(f);
    }
}

/*
 * Upon success, the caller is reponsible for unlinking and freeing *kernel_path
 */
static bool read_eif_kernel(FILE *f, uint64_t size, char **kernel_path,
                            uint32_t *crc, Error **errp)
{
    size_t got;
    FILE *tmp_file = NULL;
    uint8_t *kernel = NULL;

    *kernel_path = NULL;
    if (!get_tmp_file("eif-kernel-XXXXXX", kernel_path, errp)) {
        goto cleanup;
    }

    tmp_file = fopen(*kernel_path, "wb");
    if (tmp_file == NULL) {
        error_setg_errno(errp, errno, "Failed to open temporary file %s",
                         *kernel_path);
        goto cleanup;
    }

    kernel = g_malloc(size);
    got = fread(kernel, 1, size, f);
    if ((uint64_t) got != size) {
        error_setg(errp, "Failed to read EIF kernel section data");
        goto cleanup;
    }

    got = fwrite(kernel, 1, size, tmp_file);
    if ((uint64_t) got != size) {
        error_setg(errp, "Failed to write EIF kernel section data to temporary"
                   " file");
        goto cleanup;
    }

    *crc = crc32(*crc, kernel, size);
    g_free(kernel);
    fclose(tmp_file);

    return true;

 cleanup:
    safe_fclose(tmp_file);

    safe_unlink(*kernel_path);
    g_free(*kernel_path);
    *kernel_path = NULL;

    g_free(kernel);
    return false;
}

static bool read_eif_cmdline(FILE *f, uint64_t size, char *cmdline,
                             uint32_t *crc, Error **errp)
{
    size_t got = fread(cmdline, 1, size, f);
    if ((uint64_t) got != size) {
        error_setg(errp, "Failed to read EIF cmdline section data");
        return false;
    }

    *crc = crc32(*crc, (uint8_t *)cmdline, size);
    return true;
}

static bool read_eif_ramdisk(FILE *eif, FILE *initrd, uint64_t size,
                             uint32_t *crc, Error **errp)
{
    size_t got;
    uint8_t *ramdisk = g_malloc(size);

    got = fread(ramdisk, 1, size, eif);
    if ((uint64_t) got != size) {
        error_setg(errp, "Failed to read EIF ramdisk section data");
        goto cleanup;
    }

    got = fwrite(ramdisk, 1, size, initrd);
    if ((uint64_t) got != size) {
        error_setg(errp, "Failed to write EIF ramdisk data to temporary file");
        goto cleanup;
    }

    *crc = crc32(*crc, ramdisk, size);
    g_free(ramdisk);
    return true;

 cleanup:
    g_free(ramdisk);
    return false;
}

/*
 * Upon success, the caller is reponsible for unlinking and freeing
 * *kernel_path, *initrd_path and freeing *cmdline.
 */
bool read_eif_file(const char *eif_path, char **kernel_path, char **initrd_path,
                   char **cmdline, Error **errp)
{
    FILE *f = NULL;
    FILE *initrd_f = NULL;
    uint32_t crc = 0;
    EifHeader eif_header;
    bool seen_sections[EIF_SECTION_MAX] = {false};

    *kernel_path = *initrd_path = *cmdline = NULL;

    f = fopen(eif_path, "rb");
    if (f == NULL) {
        error_setg_errno(errp, errno, "Failed to open %s", eif_path);
        goto cleanup;
    }

    if (!read_eif_header(f, &eif_header, &crc, errp)) {
        goto cleanup;
    }

    if (eif_header.version < 4) {
        error_setg(errp, "Expected EIF version 4 or greater");
        goto cleanup;
    }

    if (eif_header.flags != 0) {
        error_setg(errp, "Expected EIF flags to be 0");
        goto cleanup;
    }

    if (eif_header.section_cnt > MAX_SECTIONS) {
        error_setg(errp, "EIF header section count must not be greater than "
                   "%d but found %d", MAX_SECTIONS, eif_header.section_cnt);
        goto cleanup;
    }

    for (int i = 0; i < eif_header.section_cnt; ++i) {
        EifSectionHeader section_header;
        uint16_t section_type;

        if (fseek(f, eif_header.section_offsets[i], SEEK_SET) != 0) {
            error_setg_errno(errp, errno, "Failed to offset to %lu in EIF file",
                             eif_header.section_offsets[i]);
            goto cleanup;
        }

        if (!read_eif_section_header(f, &section_header, &crc, errp)) {
            goto cleanup;
        }

        if (section_header.flags != 0) {
            error_setg(errp, "Expected EIF section header flags to be 0");
            goto cleanup;
        }

        if (eif_header.section_sizes[i] != section_header.section_size) {
            error_setg(errp, "EIF section size mismatch between header and "
                       "section header: header %lu, section header %lu",
                       eif_header.section_sizes[i],
                       section_header.section_size);
            goto cleanup;
        }

        section_type = section_header.section_type;

        switch (section_type) {
        case EIF_SECTION_KERNEL:
            if (seen_sections[EIF_SECTION_KERNEL]) {
                error_setg(errp, "Invalid EIF image. More than 1 kernel "
                           "section");
                goto cleanup;
            }
            if (!read_eif_kernel(f, section_header.section_size, kernel_path,
                                 &crc, errp)) {
                goto cleanup;
            }

            break;
        case EIF_SECTION_CMDLINE:
        {
            uint64_t size;
            if (seen_sections[EIF_SECTION_CMDLINE]) {
                error_setg(errp, "Invalid EIF image. More than 1 cmdline "
                           "section");
                goto cleanup;
            }
            size = section_header.section_size;
            *cmdline = g_malloc(size + 1);
            if (!read_eif_cmdline(f, size, *cmdline, &crc, errp)) {
                goto cleanup;
            }
            (*cmdline)[size] = '\0';

            break;
        }
        case EIF_SECTION_RAMDISK:
            if (!seen_sections[EIF_SECTION_RAMDISK]) {
                /*
                 * If this is the first time we are seeing a ramdisk section,
                 * we need to create the initrd temporary file.
                 */
                if (!get_tmp_file("eif-initrd-XXXXXX", initrd_path, errp)) {
                    goto cleanup;
                }
                initrd_f = fopen(*initrd_path, "wb");
                if (initrd_f == NULL) {
                    error_setg_errno(errp, errno, "Failed to open file %s",
                                     *initrd_path);
                    goto cleanup;
                }
            }

            if (!read_eif_ramdisk(f, initrd_f, section_header.section_size,
                                  &crc, errp)) {
                goto cleanup;
            }

            break;
        default:
            /* other sections including invalid or unknown sections */
        {
            uint8_t *buf;
            size_t got;
            uint64_t size = section_header.section_size;
            buf = g_malloc(size);
            got = fread(buf, 1, size, f);
            if ((uint64_t) got != size) {
                g_free(buf);
                error_setg(errp, "Failed to read EIF %s section data",
                           section_type_to_string(section_type));
                goto cleanup;
            }
            crc = crc32(crc, buf, size);
            g_free(buf);
            break;
        }
        }

        if (section_type < EIF_SECTION_MAX) {
            seen_sections[section_type] = true;
        }
    }

    if (!seen_sections[EIF_SECTION_KERNEL]) {
        error_setg(errp, "Invalid EIF image. No kernel section.");
        goto cleanup;
    }
    if (!seen_sections[EIF_SECTION_CMDLINE]) {
        error_setg(errp, "Invalid EIF image. No cmdline section.");
        goto cleanup;
    }
    if (!seen_sections[EIF_SECTION_RAMDISK]) {
        error_setg(errp, "Invalid EIF image. No ramdisk section.");
        goto cleanup;
    }

    if (eif_header.eif_crc32 != crc) {
        error_setg(errp, "CRC mismatch. Expected %u but header has %u.",
                   crc, eif_header.eif_crc32);
        goto cleanup;
    }

    fclose(f);
    fclose(initrd_f);
    return true;

 cleanup:
    safe_fclose(f);
    safe_fclose(initrd_f);

    safe_unlink(*kernel_path);
    g_free(*kernel_path);
    *kernel_path = NULL;

    safe_unlink(*initrd_path);
    g_free(*initrd_path);
    *initrd_path = NULL;

    g_free(*cmdline);
    *cmdline = NULL;

    return false;
}

bool check_if_eif_file(const char *path, bool *is_eif, Error **errp)
{
    size_t got;
    uint8_t buf[4];
    FILE *f = NULL;

    f = fopen(path, "rb");
    if (f == NULL) {
        error_setg_errno(errp, errno, "Failed to open file %s", path);
        goto cleanup;
    }

    got = fread(buf, 1, 4, f);
    if (got != 4) {
        error_setg(errp, "Failed to read magic value from %s", path);
        goto cleanup;
    }

    fclose(f);
    *is_eif = !memcmp(buf, ".eif", 4);
    return true;

 cleanup:
    safe_fclose(f);
    return false;
}
