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
#include <stdint.h>
#include <dirent.h>

#include "qapi/qapi-types-modules.h"
#include "qapi/qapi-visit-modules.h"
#include "qapi/qobject-output-visitor.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qstring.h"

#if INTPTR_MAX == INT32_MAX
# define Elf_Ehdr Elf32_Ehdr
# define Elf_Shdr Elf32_Shdr
# define ELFCLASS ELFCLASS32
#elif INTPTR_MAX == INT64_MAX
# define Elf_Ehdr Elf64_Ehdr
# define Elf_Shdr Elf64_Shdr
# define ELFCLASS ELFCLASS64
#else
# error Huh?  Neither 32-bit nor 64-bit host.
#endif

static const char *moddir = CONFIG_QEMU_MODDIR;
static const char *dsosuf = CONFIG_HOST_DSOSUF;

static ModuleInfo *modinfo(const char *module, char *info, size_t size)
{
    ModuleInfo *modinfo;
    strList *sl;
    size_t pos = 0, len;

    modinfo = g_new0(ModuleInfo, 1);
    modinfo->name = g_strdup(module);

    if (info) {
        do {
            if (strncmp(info + pos, "obj=", 4) == 0) {
                sl = g_new0(strList, 1);
                sl->value = g_strdup(info + pos + 4);
                sl->next = modinfo->objs;
                modinfo->objs = sl;
                modinfo->has_objs = true;
            } else if (strncmp(info + pos, "dep=", 4) == 0) {
                sl = g_new0(strList, 1);
                sl->value = g_strdup(info + pos + 4);
                sl->next = modinfo->deps;
                modinfo->deps = sl;
                modinfo->has_deps = true;
            } else if (strncmp(info + pos, "arch=", 5) == 0) {
                modinfo->arch = g_strdup(info + pos + 5);
                modinfo->has_arch = true;
            } else if (strncmp(info + pos, "opts=", 5) == 0) {
                modinfo->opts = g_strdup(info + pos + 5);
                modinfo->has_opts = true;
            } else {
                fprintf(stderr, "unknown tag: %s\n", info + pos);
                exit(1);
            }
            len = strlen(info + pos) + 1;
            pos += len;
        } while (pos < size);
    }

    return modinfo;
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

static ModuleInfo *elf_parse_module(const char *module,
                                    const char *filename)
{
    Elf_Ehdr ehdr;
    Elf_Shdr shdr;
    FILE *fp;
    int ret, i;
    char *str;
    size_t str_size;
    char *info;
    size_t info_size;

    fp = fopen(filename, "r");
    if (NULL == fp) {
        fprintf(stderr, "open %s: %s\n", filename, strerror(errno));
        exit(1);
    }

    ret = fread(&ehdr, sizeof(ehdr), 1, fp);
    if (ret != 1) {
        fprintf(stderr, "read error (%s)\n", filename);
        exit(1);
    }

    if (ehdr.e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr.e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr.e_ident[EI_MAG3] != ELFMAG3) {
        fprintf(stderr, "not an elf file (%s)\n", filename);
        exit(1);
    }
    if (ehdr.e_ident[EI_CLASS] != ELFCLASS64) {
        fprintf(stderr, "elf class mismatch (%s)\n", filename);
        exit(1);
    }
    if (ehdr.e_shoff == 0) {
        fprintf(stderr, "no section header (%s)\n", filename);
        exit(1);
    }

    /* read string table */
    if (ehdr.e_shstrndx == 0) {
        fprintf(stderr, "no section strings (%s)\n", filename);
        exit(1);
    }
    str = elf_read_section(fp, &ehdr, ehdr.e_shstrndx, &str_size);
    if (NULL == str) {
        fprintf(stderr, "no section strings (%s)\n", filename);
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
    fclose(fp);

    return modinfo(module, info, info_size);
}

int main(int argc, char **argv)
{
    DIR *dir;
    FILE *fp;
    ModuleInfo *modinfo;
    ModuleInfoList *modlist;
    Modules *modules;
    Visitor *v;
    QObject *obj;
    Error *errp = NULL;
    struct dirent *ent;
    char *ext, *file, *name;
    GString *gjson;
    QString *qjson;
    const char *json;

    if (argc > 1) {
        moddir = argv[1];
    }

    dir = opendir(moddir);
    if (dir == NULL) {
        fprintf(stderr, "opendir(%s): %s\n", moddir, strerror(errno));
        exit(1);
    }

    modules = g_new0(Modules, 1);
    while (NULL != (ent = readdir(dir))) {
        ext = strrchr(ent->d_name, '.');
        if (!ext) {
            continue;
        }
        if (strcmp(ext, dsosuf) != 0) {
            continue;
        }

        name = g_strndup(ent->d_name, ext - ent->d_name);
        file = g_strdup_printf("%s/%s", moddir, ent->d_name);
        modinfo = elf_parse_module(name, file);
        g_free(file);
        g_free(name);

        modlist = g_new0(ModuleInfoList, 1);
        modlist->value = modinfo;
        modlist->next = modules->list;
        modules->list = modlist;
    }
    closedir(dir);

    v = qobject_output_visitor_new(&obj);
    visit_type_Modules(v, NULL, &modules, &errp);
    visit_complete(v, &obj);
    visit_free(v);

    gjson = qobject_to_json(obj);
    qjson = qstring_from_gstring(gjson);
    json = qstring_get_str(qjson);

    file = g_strdup_printf("%s/modinfo.json", moddir);
    fp = fopen(file, "w");
    if (fp == NULL) {
        fprintf(stderr, "open(%s): %s\n", file, strerror(errno));
        exit(1);
    }
    fprintf(fp, "%s", json);
    fclose(fp);

    printf("%s written\n", file);
    g_free(file);
    return 0;
}
