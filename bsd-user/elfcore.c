/* Stubbed out version of core dump support, explicitly in public domain */

static int elf_core_dump(int signr, CPUArchState *env)
{
    struct elf_note en;

    bswap_note(&en);

    return 0;
}
