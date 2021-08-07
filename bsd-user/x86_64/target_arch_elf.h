#define ELF_PLATFORM get_elf_platform()

static const char *get_elf_platform(void)
{
    static char elf_platform[] = "i386";
    int family = object_property_get_int(OBJECT(thread_cpu), "family", NULL);
    if (family > 6)
        family = 6;
    if (family >= 3)
        elf_platform[1] = '0' + family;
    return elf_platform;
}

#define ELF_HWCAP get_elf_hwcap()

static uint32_t get_elf_hwcap(void)
{
    X86CPU *cpu = X86_CPU(thread_cpu);

    return cpu->env.features[FEAT_1_EDX];
}

#define ELF_START_MMAP 0x2aaaaab000ULL
#define elf_check_arch(x) ( ((x) == ELF_ARCH) )

#define ELF_CLASS      ELFCLASS64
#define ELF_DATA       ELFDATA2LSB
#define ELF_ARCH       EM_X86_64

static inline void init_thread(struct target_pt_regs *regs, struct image_info *infop)
{
    regs->rax = 0;
    regs->rsp = infop->start_stack;
    regs->rip = infop->entry;
    if (bsd_type == target_freebsd) {
        regs->rdi = infop->start_stack;
    }
}

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE       4096
