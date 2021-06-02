/*
 * QEMU module parser
 *
 * read modules, find modinfo section, parse & store metadata.
 *
 * Copyright Red Hat, Inc. 2021
 *
 * Authors:
 *     Gerd Hoffmann <kraxel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "elf.h"
#include <glob.h>

#define Elf_Ehdr Elf64_Ehdr
#define Elf_Shdr Elf64_Shdr
#define ELFCLASS ELFCLASS64

static const char *moddir = CONFIG_QEMU_MODDIR;
static const char *dsosuf = CONFIG_HOST_DSOSUF;

static void modinfo(const char *module, char *info, size_t size)
{
    size_t pos = 0, len;

    fprintf(stderr, "%s\n", module);
    do {
        fprintf(stderr, "  -> %s\n", info + pos);
        len = strlen(info + pos) + 1;
        pos += len;
    } while (pos < size);
}

static void elf_read_section_hdr(FILE *fp, Elf_Ehdr *ehdr,
                                 int section, Elf_Shdr *shdr)
{
    size_t pos, len;
    int ret;

    pos = ehdr->e_shoff + section * ehdr->e_shentsize;
    len = MIN(ehdr->e_shentsize, sizeof(*shdr));

    ret = fseek(fp, pos, SEEK_SET);
    if (ret != 0) {
        fprintf(stderr, "seek error\n");
        exit(1);
    }

    memset(shdr, 0, sizeof(*shdr));
    ret = fread(shdr, len, 1, fp);
    if (ret != 1) {
        fprintf(stderr, "read error\n");
        exit(1);
    }
}

static void *elf_read_section(FILE *fp, Elf_Ehdr *ehdr,
                              int section, size_t *size)
{
    Elf_Shdr shdr;
    void *data;
    int ret;

    elf_read_section_hdr(fp, ehdr, section, &shdr);
    if (shdr.sh_offset && shdr.sh_size) {
        ret = fseek(fp, shdr.sh_offset, SEEK_SET);
        if (ret != 0) {
            fprintf(stderr, "seek error\n");
            exit(1);
        }

        data = g_malloc(shdr.sh_size);
        ret = fread(data, shdr.sh_size, 1, fp);
        if (ret != 1) {
            fprintf(stderr, "read error\n");
            exit(1);
        }
        *size = shdr.sh_size;
    } else {
        data = NULL;
        *size = 0;
    }
    return data;
}

static void elf_parse_module(const char *module)
{
    Elf_Ehdr ehdr;
    Elf_Shdr shdr;
    FILE *fp;
    int ret, i;
    char *str;
    size_t str_size;
    char *info;
    size_t info_size;

    fp = fopen(module, "r");
    if (NULL == fp) {
        fprintf(stderr, "open %s: %s\n", module, strerror(errno));
        exit(1);
    }

    ret = fread(&ehdr, sizeof(ehdr), 1, fp);
    if (ret != 1) {
        fprintf(stderr, "read error (%s)\n", module);
        exit(1);
    }

    if (ehdr.e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr.e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr.e_ident[EI_MAG3] != ELFMAG3) {
        fprintf(stderr, "not an elf file (%s)\n", module);
        exit(1);
    }
    if (ehdr.e_ident[EI_CLASS] != ELFCLASS64) {
        fprintf(stderr, "elf class mismatch (%s)\n", module);
        exit(1);
    }
    if (ehdr.e_shoff == 0) {
        fprintf(stderr, "no section header (%s)\n", module);
        exit(1);
    }

    /* read string table */
    if (ehdr.e_shstrndx == 0) {
        fprintf(stderr, "no section strings (%s)\n", module);
        exit(1);
    }
    str = elf_read_section(fp, &ehdr, ehdr.e_shstrndx, &str_size);
    if (NULL == str) {
        fprintf(stderr, "no section strings (%s)\n", module);
        exit(1);
    }

    /* find and read modinfo section */
    info = NULL;
    for (i = 0; i < ehdr.e_shnum; i++) {
        elf_read_section_hdr(fp, &ehdr, i, &shdr);
        if (!shdr.sh_name) {
            continue;
        }
        if (strcmp(str + shdr.sh_name, ".modinfo") == 0) {
            info = elf_read_section(fp, &ehdr, i, &info_size);
        }
    }

    if (info) {
        modinfo(module, info, info_size);
    }

    fclose(fp);
}

int main(int argc, char **argv)
{
    char *pattern;
    glob_t files;
    int ret, i;

    if (argc > 1) {
        moddir = argv[1];
    }

    pattern = g_strdup_printf("%s/*%s", moddir, dsosuf);
    ret = glob(pattern, 0, NULL, &files);
    if (ret != 0) {
        fprintf(stderr, "glob(%s) failed: %d\n", pattern, ret);
        exit(1);
    }

    for (i = 0; i < files.gl_pathc; i++) {
        elf_parse_module(files.gl_pathv[i]);
    }

    globfree(&files);
    g_free(pattern);
    return 0;
}
