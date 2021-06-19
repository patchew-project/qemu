/*
 * Post-process a vdso elf image for inclusion into qemu.
 * Elf size specialization.
 *
 * Copyright 2021 Linaro, Ltd.
 * 
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

static void elfN(bswap_ehdr)(ElfN(Ehdr) *ehdr)
{
    bswaps(&ehdr->e_type);            /* Object file type */
    bswaps(&ehdr->e_machine);         /* Architecture */
    bswaps(&ehdr->e_version);         /* Object file version */
    bswaps(&ehdr->e_entry);           /* Entry point virtual address */
    bswaps(&ehdr->e_phoff);           /* Program header table file offset */
    bswaps(&ehdr->e_shoff);           /* Section header table file offset */
    bswaps(&ehdr->e_flags);           /* Processor-specific flags */
    bswaps(&ehdr->e_ehsize);          /* ELF header size in bytes */
    bswaps(&ehdr->e_phentsize);       /* Program header table entry size */
    bswaps(&ehdr->e_phnum);           /* Program header table entry count */
    bswaps(&ehdr->e_shentsize);       /* Section header table entry size */
    bswaps(&ehdr->e_shnum);           /* Section header table entry count */
    bswaps(&ehdr->e_shstrndx);        /* Section header string table index */
}

static void elfN(bswap_phdr)(ElfN(Phdr) *phdr)
{
    bswaps(&phdr->p_type);            /* Segment type */
    bswaps(&phdr->p_flags);           /* Segment flags */
    bswaps(&phdr->p_offset);          /* Segment file offset */
    bswaps(&phdr->p_vaddr);           /* Segment virtual address */
    bswaps(&phdr->p_paddr);           /* Segment physical address */
    bswaps(&phdr->p_filesz);          /* Segment size in file */
    bswaps(&phdr->p_memsz);           /* Segment size in memory */
    bswaps(&phdr->p_align);           /* Segment alignment */
}

static void elfN(bswap_shdr)(ElfN(Shdr) *shdr)
{
    bswaps(&shdr->sh_name);
    bswaps(&shdr->sh_type);
    bswaps(&shdr->sh_flags);
    bswaps(&shdr->sh_addr);
    bswaps(&shdr->sh_offset);
    bswaps(&shdr->sh_size);
    bswaps(&shdr->sh_link);
    bswaps(&shdr->sh_info);
    bswaps(&shdr->sh_addralign);
    bswaps(&shdr->sh_entsize);
}

static void elfN(bswap_sym)(ElfN(Sym) *sym)
{
    bswaps(&sym->st_name);
    bswaps(&sym->st_value);
    bswaps(&sym->st_size);
    bswaps(&sym->st_shndx);
}

static void elfN(bswap_dyn)(ElfN(Dyn) *dyn)
{
    bswaps(&dyn->d_tag);              /* Dynamic type tag */
    bswaps(&dyn->d_un.d_ptr);         /* Dynamic ptr or val, in union */
}

static void elfN(process)(FILE *outf, void *buf, long total_len,
                          bool need_bswap)
{
    ElfN(Ehdr) *ehdr = buf;
    ElfN(Phdr) *phdr;
    ElfN(Shdr) *shdr;
    unsigned phnum, shnum;
    unsigned dynamic_ofs = 0;
    unsigned dynamic_addr = 0;
    unsigned dynsym_addr = 0;
    unsigned sigreturn_addr = 0;
    unsigned rt_sigreturn_addr = 0;
    unsigned first_segsz = 0;
    int errors = 0;

    if (need_bswap) {
        elfN(bswap_ehdr)(ehdr);
    }

    phnum = ehdr->e_phnum;
    phdr = buf + ehdr->e_phoff;
    if (need_bswap) {
        for (unsigned i = 0; i < phnum; ++i) {
            elfN(bswap_phdr)(phdr + i);
        }
    }

    shnum = ehdr->e_shnum;
    shdr = buf + ehdr->e_shoff;
    if (need_bswap) {
        for (unsigned i = 0; i < shnum; ++i) {
            elfN(bswap_shdr)(shdr + i);
        }
    }

    /*
     * Validate the VDSO is created as we expect: that PT_PHDR,
     * PT_DYNAMIC, and PT_NOTE located in a writable data segment.
     * PHDR and DYNAMIC require relocation, and NOTE will get the
     * linux version number.
     */
    for (unsigned i = 0; i < phnum; ++i) {
        if (phdr[i].p_type == PT_LOAD && phdr[i].p_vaddr == 0) {
            if (first_segsz != 0) {
                fprintf(stderr, "Multiple load segments covering EHDR\n");
                errors++;
            }
            if (phdr[i].p_offset != 0) {
                fprintf(stderr, "First vdso segment does not cover EHDR\n");
                errors++;
            }
            if (phdr[i].p_vaddr != 0) {
                fprintf(stderr, "First vdso segment not loaded at address 0\n");
                errors++;
            }
            if ((phdr[i].p_flags & (PF_R | PF_W)) != (PF_R | PF_W)) {
                fprintf(stderr, "First vdso segment is not read-write\n");
                errors++;
            }
            first_segsz = phdr[i].p_filesz;
            if (first_segsz < ehdr->e_phoff + phnum * sizeof(*phdr)) {
                fprintf(stderr, "First vdso segment does not cover PHDRs\n");
                errors++;
            }
        }
    }
    for (unsigned i = 0; i < phnum; ++i) {
        const char *which;

        switch (phdr[i].p_type) {
        case PT_PHDR:
            which = "PT_PHDR";
            break;
        case PT_NOTE:
            which = "PT_NOTE";
            break;
        case PT_DYNAMIC:
            dynamic_ofs = phdr[i].p_offset;
            dynamic_addr = phdr[i].p_vaddr;
            which = "PT_DYNAMIC";
            break;
        default:
            continue;
        }
        if (first_segsz < phdr[i].p_vaddr + phdr[i].p_filesz) {
            fprintf(stderr, "First vdso segment does not cover %s\n", which);
            errors++;
        }
    }
    if (errors) {
        exit(EXIT_FAILURE);
    }
        
    /*
     * We need to relocate the VDSO image.  The one built into the kernel
     * is built for a fixed address.  The one we built for QEMU is not,
     * since that requires close control of the guest address space.
     *
     * ??? One might think that we'd need to relocate ehdr->e_entry,
     * but for some reason glibc does that one itself, though that
     * is also available via the AT_SYSINFO entry.
     *
     * Output relocation addresses as we go.
     */
    fputs("static const unsigned vdso_relocs[] = {\n", outf);

    /* Relocate the program headers. */
    for (unsigned i = 0; i < phnum; ++i) {
        output_reloc(outf, buf, &phdr[i].p_vaddr);
        output_reloc(outf, buf, &phdr[i].p_paddr);
    }

    /* Relocate the DYNAMIC entries. */
    if (dynamic_addr) {
        ElfN(Dyn) *dyn = buf + dynamic_ofs;
        __typeof(dyn->d_tag) tag;

        do {

            if (need_bswap) {
                elfN(bswap_dyn)(dyn);
            }
            tag = dyn->d_tag;

            switch (tag) {
            case DT_SYMTAB:
                dynsym_addr = dyn->d_un.d_val;
                /* fall through */
            case DT_HASH:
            case DT_STRTAB:
            case DT_VERDEF:
            case DT_VERSYM:
            case DT_PLTGOT:
            case DT_ADDRRNGLO ... DT_ADDRRNGHI:
                /* These entries store an address in the entry. */
                output_reloc(outf, buf, &dyn->d_un.d_val);
                break;

            case DT_NULL:
            case DT_STRSZ:
            case DT_SONAME:
            case DT_DEBUG:
            case DT_FLAGS:
            case DT_FLAGS_1:
            case DT_BIND_NOW:
            case DT_VERDEFNUM:
            case DT_VALRNGLO ... DT_VALRNGHI:
                /* These entries store an integer in the entry. */
                break;

            case DT_SYMENT:
                if (dyn->d_un.d_val != sizeof(ElfN(Sym))) {
                    fprintf(stderr, "VDSO has incorrect dynamic symbol size\n");
                    errors++;
                }
                break;

            case DT_REL:
            case DT_RELSZ:
            case DT_RELENT:
            case DT_RELA:
            case DT_RELASZ:
            case DT_RELAENT:
            case DT_TEXTREL:
                /*
                 * These entries indicate that the VDSO was built incorrectly.
                 * It should not have any real relocations.
                 */
                fprintf(stderr, "VDSO has dynamic relocations\n");
                errors++;
                break;

            case DT_NEEDED:
            case DT_VERNEED:
            case DT_PLTREL:
            case DT_JMPREL:
            case DT_RPATH:
            case DT_RUNPATH:
                fprintf(stderr, "VDSO has external dependencies\n");
                errors++;
                break;

            default:
                /* This is probably something target specific. */
                fprintf(stderr, "VDSO has unknown DYNAMIC entry (%lx)\n",
                        (unsigned long)tag);
                errors++;
                break;
            }
            dyn++;
        } while (tag != DT_NULL);
        if (errors) {
            exit(EXIT_FAILURE);
        }
    }

    if (dynsym_addr) {
        __typeof(shdr->sh_size) dynsym_n = 0;
        ElfN(Sym) *sym = NULL;
        const char *str = NULL;

        for (unsigned i = 0; i < shnum; ++i) {
            if (shdr[i].sh_addr == dynsym_addr) {
                dynsym_n = shdr[i].sh_size / sizeof(*sym);
                sym = buf + shdr[i].sh_offset;
                str = buf + shdr[shdr[i].sh_link].sh_offset;
                break;
            }
        }

        for (unsigned i = 0; i < dynsym_n; ++i) {
            if (need_bswap) {
                elfN(bswap_sym)(sym + i);
            }

            /* Relocate the dynamic symbol table. */
            output_reloc(outf, buf, &sym[i].st_value);

            /* Locate the signal return symbols. */
            const char *name = str + sym[i].st_name;
            if (strcmp("__kernel_sigreturn", name) == 0) {
                sigreturn_addr = sym[i].st_value;
            } else if (strcmp("__kernel_rt_sigreturn", name) == 0) {
                rt_sigreturn_addr = sym[i].st_value;
            }
        }
    }

    fputs("};\n\n", outf);   /* end vdso_relocs. */

    fprintf(outf, "#define vdso_sigreturn    0x%x\n", sigreturn_addr);
    fprintf(outf, "#define vdso_rt_sigreturn 0x%x\n", rt_sigreturn_addr);
}
