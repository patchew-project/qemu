/*
 * Linear Address Masking (LAM) test, run at CPL 0.  LA57 selects
 * 5-level paging, and EXPECT_LAM whether CPUID must report LAM.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>
#include <minilib.h>

#define CR3_LAM_U57     (1ULL << 61)
#define CR3_LAM_U48     (1ULL << 62)
#define CR4_LA57        (1ULL << 12)
#define CR4_LAM_SUP     (1ULL << 28)

#define PTE_P           0x001ULL
#define PTE_W           0x002ULL
#define PTE_U           0x004ULL
#define PTE_PS          0x080ULL
#define PTE_FLAGS       (PTE_P | PTE_W | PTE_U)

#define VEC_NONE        (-1)
#define VEC_GP          13
#define VEC_PF          14

/* Identity map of the first 128 MiB with 4 KiB pages. */
#define ID_PAGES        (128 * 256)

/* User-half test region: PML4 entry 1. */
#define VA_BASE         0x0000008000000000ULL
/* Supervisor-half test region: PML4 entry 256, same tables as VA_BASE. */
#define VA_SUP          0xffff800000000000ULL
/* Pages 0-2 are present, page 3 is not. */
#define VA_PAGE(n)      (VA_BASE + (n) * 4096ULL)
/* A 2 MiB page after the 4 KiB pages. */
#define VA_LP           (VA_BASE + 0x200000ULL)

#define LP0_PHYS        0x04000000ULL
#define LP1_PHYS        0x06000000ULL

/* Tags in the metadata bits of each LAM width. */
#define TAG57(t)        ((uint64_t)(t) << 57)
#define TAG48(t)        ((uint64_t)(t) << 48)

/* VA_SUP with tag @t.  SUP_BAD masks to a non-canonical address. */
#ifdef LA57
#define STAG(t)         ((VA_SUP & ~TAG57(0x3f)) | TAG57(t))
#define SUP_BAD         ((1ULL << 63) | TAG57(0x2a) | 0x1000)
#else
#define STAG(t)         ((1ULL << 63) | TAG48(t) | \
                         (VA_SUP & ((1ULL << 48) - 1)))
#define SUP_BAD         ((1ULL << 63) | TAG48(0x5a5a) | 0x1000)
#endif

/* Vector for an address that is non-canonical only with 4-level paging */
#ifdef LA57
#define VEC_NC4         VEC_PF
#else
#define VEC_NC4         VEC_GP
#endif

#define MARK(n)         (0x1111111111111111ULL * (n))

static uint64_t pml5[512] __attribute__((aligned(4096)));
static uint64_t pml4[512] __attribute__((aligned(4096)));
static uint64_t pdpt_id[512] __attribute__((aligned(4096)));
static uint64_t pd_id[512] __attribute__((aligned(4096)));
static uint64_t pt_id[ID_PAGES] __attribute__((aligned(4096)));
static uint64_t pdpt_t[512] __attribute__((aligned(4096)));
static uint64_t pd_t[512] __attribute__((aligned(4096)));
static uint64_t pt_t[512] __attribute__((aligned(4096)));
static uint8_t frames[4][4096] __attribute__((aligned(4096)));

#ifdef LA57
/* VA_BASE with bit 48 set: PML5 entry 1, mapped to frames[2]. */
#define VA_HIGH         (VA_BASE | (1ULL << 48))
static uint64_t pml4_h[512] __attribute__((aligned(4096)));
static uint64_t pdpt_h[512] __attribute__((aligned(4096)));
static uint64_t pd_h[512] __attribute__((aligned(4096)));
static uint64_t pt_h[512] __attribute__((aligned(4096)));
#endif

struct idt_gate {
    uint16_t off0;
    uint16_t sel;
    uint8_t ist;
    uint8_t type;
    uint16_t off1;
    uint32_t off2;
    uint32_t zero;
} __attribute__((packed));

static struct idt_gate idt[32] __attribute__((aligned(16)));

/* Written by the exception stubs. */
volatile uint64_t fault_vec, fault_err, fault_rip, fault_cr2, fixup_rip;

/* Exception stubs: record the fault, then resume at fixup_rip, or stop if 0. */
asm(".text\n"
    ".irp v, 0,1,2,3,4,5,6,7,9,15,16,18,19,20,22,23,24,25,26,27,28,31\n"
    "isr_\\v:\n"
    "    pushq $0\n"
    "    pushq $\\v\n"
    "    jmp isr_common\n"
    ".endr\n"
    ".irp v, 8,10,11,12,13,14,17,21,29,30\n"
    "isr_\\v:\n"
    "    pushq $\\v\n"
    "    jmp isr_common\n"
    ".endr\n"
    "isr_common:\n"
    /* stack: rax, vector, error code, rip, cs, rflags, rsp, ss */
    "    pushq %rax\n"
    "    movq 8(%rsp), %rax\n"
    "    movq %rax, fault_vec(%rip)\n"
    "    movq 16(%rsp), %rax\n"
    "    movq %rax, fault_err(%rip)\n"
    "    movq 24(%rsp), %rax\n"
    "    movq %rax, fault_rip(%rip)\n"
    "    movq %cr2, %rax\n"
    "    movq %rax, fault_cr2(%rip)\n"
    "    movq fixup_rip(%rip), %rax\n"
    "    testq %rax, %rax\n"
    "    jz 1f\n"
    "    movq %rax, 24(%rsp)\n"
    "    movq $0, fixup_rip(%rip)\n"
    "    popq %rax\n"
    "    addq $16, %rsp\n"
    "    iretq\n"
    "1:  andq $-16, %rsp\n"
    "    call unexpected_exception\n"
    "2:  hlt\n"
    "    jmp 2b\n"
    ".data\n"
    ".align 8\n"
    "isr_table:\n"
    ".irp v, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,"
    "16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31\n"
    "    .quad isr_\\v\n"
    ".endr\n"
    ".text\n");

static int passes, failures;
static uint64_t base_cr3;

void unexpected_exception(void)
{
    ml_printf("FAIL: unexpected exception %llu, error %llx, rip %llx, "
              "cr2 %llx\n", fault_vec, fault_err, fault_rip, fault_cr2);
    asm volatile("outw %w0, $0xf4" : : "a"(0x7f));
    for (;;) {
        asm volatile("hlt");
    }
}

static const char *vec_name(long long vec)
{
    switch (vec) {
    case VEC_NONE:
        return "none";
    case VEC_GP:
        return "#GP";
    case VEC_PF:
        return "#PF";
    default:
        return "other";
    }
}

static void check(int ok, const char *what)
{
    if (ok) {
        passes++;
    } else {
        failures++;
        ml_printf("FAIL: %s\n", what);
    }
}

static uint64_t read_cr3(void)
{
    uint64_t v;

    asm volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}

static uint64_t read_cr4(void)
{
    uint64_t v;

    asm volatile("mov %%cr4, %0" : "=r"(v));
    return v;
}

static void write_cr2(uint64_t v)
{
    asm volatile("mov %0, %%cr2" : : "r"(v) : "memory");
}

/* try_*: one operation, returning the vector it raised or VEC_NONE. */
#define TRY_BEGIN                               \
    "leaq 1f(%%rip), %%rax\n\t"                 \
    "movq %%rax, fixup_rip(%%rip)\n\t"
#define TRY_END                                 \
    "1:\n\t"                                    \
    "movq $0, fixup_rip(%%rip)\n\t"

static long long try_load(uint64_t addr, uint64_t *val)
{
    uint64_t v = 0;

    fault_vec = VEC_NONE;
    asm volatile(TRY_BEGIN
                 "movq (%[a]), %[v]\n\t"
                 TRY_END
                 : [v] "+r"(v) : [a] "r"(addr) : "rax", "memory");
    *val = v;
    return (long long)fault_vec;
}

static long long try_store(uint64_t addr, uint64_t val)
{
    fault_vec = VEC_NONE;
    asm volatile(TRY_BEGIN
                 "movq %[v], (%[a])\n\t"
                 TRY_END
                 : : [v] "r"(val), [a] "r"(addr) : "rax", "memory");
    return (long long)fault_vec;
}

static long long try_write_cr3(uint64_t v)
{
    fault_vec = VEC_NONE;
    asm volatile(TRY_BEGIN
                 "mov %[v], %%cr3\n\t"
                 TRY_END
                 : : [v] "r"(v) : "rax", "memory");
    return (long long)fault_vec;
}

static long long try_write_cr4(uint64_t v)
{
    fault_vec = VEC_NONE;
    asm volatile(TRY_BEGIN
                 "mov %[v], %%cr4\n\t"
                 TRY_END
                 : : [v] "r"(v) : "rax", "memory");
    return (long long)fault_vec;
}

static long long try_invlpg(uint64_t addr)
{
    fault_vec = VEC_NONE;
    asm volatile(TRY_BEGIN
                 "invlpg (%[a])\n\t"
                 TRY_END
                 : : [a] "r"(addr) : "rax", "memory");
    return (long long)fault_vec;
}

/* Jump to an exec stub at @addr.  *ran is its EAX, or 0 if none ran. */
static long long try_exec(uint64_t addr, int *ran)
{
    uint64_t r;

    fault_vec = VEC_NONE;
    asm volatile("leaq 1f(%%rip), %%rdx\n\t"
                 "movq %%rdx, fixup_rip(%%rip)\n\t"
                 "xorl %%eax, %%eax\n\t"
                 "jmp *%[a]\n\t"
                 "1:\n\t"
                 "movq $0, fixup_rip(%%rip)\n\t"
                 : "=&a"(r) : [a] "r"(addr) : "rdx", "memory");
    *ran = r;
    return (long long)fault_vec;
}

/* movl $0x1234, %eax; jmp *%rdx, and the same with $0x2222 */
static const uint8_t exec_stub[] = {
    0xb8, 0x34, 0x12, 0x00, 0x00, 0xff, 0xe2
};
#ifdef LA57
static const uint8_t exec_stub2[] = {
    0xb8, 0x22, 0x22, 0x00, 0x00, 0xff, 0xe2
};
#endif
#define EXEC_OFF        0x800

static void expect_load(const char *what, uint64_t addr, long long vec,
                        uint64_t val)
{
    uint64_t v;
    long long got = try_load(addr, &v);

    if (got == vec && (vec != VEC_NONE || v == val)) {
        passes++;
        return;
    }
    failures++;
    ml_printf("FAIL: %s: load %llx: exception %s, value %llx; "
              "expected exception %s, value %llx\n",
              what, addr, vec_name(got), v, vec_name(vec), val);
}

static void expect_store(const char *what, uint64_t addr, long long vec,
                         uint64_t val)
{
    long long got = try_store(addr, val);

    if (got == vec) {
        passes++;
        return;
    }
    failures++;
    ml_printf("FAIL: %s: store %llx: exception %s, expected %s\n",
              what, addr, vec_name(got), vec_name(vec));
}

static void expect_fault(const char *what, long long got, long long vec)
{
    if (got == vec) {
        passes++;
        return;
    }
    failures++;
    ml_printf("FAIL: %s: exception %s, expected %s\n",
              what, vec_name(got), vec_name(vec));
}

static void set_lam_cr3(uint64_t bits)
{
    long long v = try_write_cr3(base_cr3 | bits);

    if (v != VEC_NONE) {
        ml_printf("FAIL: MOV to CR3 with %llx: exception %s\n",
                  bits, vec_name(v));
        failures++;
    }
}

static void set_lam_sup(int on)
{
    uint64_t cr4 = read_cr4();
    long long v;

    cr4 = on ? cr4 | CR4_LAM_SUP : cr4 & ~CR4_LAM_SUP;
    v = try_write_cr4(cr4);
    if (v != VEC_NONE) {
        ml_printf("FAIL: MOV to CR4 with LAM_SUP=%d: exception %s\n",
                  on, vec_name(v));
        failures++;
    }
}

static uint64_t *phys(uint64_t pa)
{
    return (uint64_t *)pa;
}

static void setup_idt(void)
{
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) idtr = { sizeof(idt) - 1, (uint64_t)idt };
    const uint64_t *isr_table;

    asm("leaq isr_table(%%rip), %0" : "=r"(isr_table));
    for (int i = 0; i < 32; i++) {
        uint64_t h = isr_table[i];

        idt[i].off0 = h;
        idt[i].sel = 0x8;
        idt[i].ist = 0;
        idt[i].type = 0x8e;
        idt[i].off1 = h >> 16;
        idt[i].off2 = h >> 32;
        idt[i].zero = 0;
    }
    asm volatile("lidt %0" : : "m"(idtr));
}

static void setup_paging(void)
{
    for (int i = 0; i < 512; i++) {
        pml5[i] = pml4[i] = pdpt_id[i] = pd_id[i] = 0;
        pdpt_t[i] = pd_t[i] = pt_t[i] = 0;
    }
    for (int i = 0; i < ID_PAGES; i++) {
        pt_id[i] = (uint64_t)i * 4096 | PTE_FLAGS;
    }
    for (int i = 0; i < ID_PAGES / 512; i++) {
        pd_id[i] = (uint64_t)&pt_id[i * 512] | PTE_FLAGS;
    }
    pdpt_id[0] = (uint64_t)pd_id | PTE_FLAGS;
    pml4[0] = (uint64_t)pdpt_id | PTE_FLAGS;

    for (int i = 0; i < 3; i++) {
        pt_t[i] = (uint64_t)frames[i] | PTE_FLAGS;
    }
    pd_t[0] = (uint64_t)pt_t | PTE_FLAGS;
    pd_t[1] = LP0_PHYS | PTE_PS | PTE_FLAGS;
    pdpt_t[0] = (uint64_t)pd_t | PTE_FLAGS;
    pml4[1] = (uint64_t)pdpt_t | PTE_FLAGS;
    pml4[256] = (uint64_t)pdpt_t | PTE_FLAGS;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4096 / 8; j++) {
            ((uint64_t *)frames[i])[j] = MARK(i + 1) + j;
        }
    }
    *phys(LP0_PHYS) = 0xaaaaaaaaaaaaaaaaULL;
    *phys(LP0_PHYS + 0x5000) = 0xaaaaaaaaaaaa5000ULL;
    *phys(LP1_PHYS) = 0xbbbbbbbbbbbbbbbbULL;
    *phys(LP1_PHYS + 0x5000) = 0xbbbbbbbbbbbb5000ULL;
    for (int i = 0; i < (int)sizeof(exec_stub); i++) {
        frames[0][EXEC_OFF + i] = exec_stub[i];
    }

#ifdef LA57
    for (int i = 0; i < 512; i++) {
        pml4_h[i] = pdpt_h[i] = pd_h[i] = pt_h[i] = 0;
    }
    pt_h[0] = (uint64_t)frames[2] | PTE_FLAGS;
    pd_h[0] = (uint64_t)pt_h | PTE_FLAGS;
    pdpt_h[0] = (uint64_t)pd_h | PTE_FLAGS;
    pml4_h[1] = (uint64_t)pdpt_h | PTE_FLAGS;
    for (int i = 0; i < (int)sizeof(exec_stub2); i++) {
        frames[2][EXEC_OFF + i] = exec_stub2[i];
    }
    pml5[0] = (uint64_t)pml4 | PTE_FLAGS;
    pml5[1] = (uint64_t)pml4_h | PTE_FLAGS;
    pml5[511] = (uint64_t)pml4 | PTE_FLAGS;
    base_cr3 = (uint64_t)pml5;
#else
    base_cr3 = (uint64_t)pml4;
#endif
    asm volatile("mov %0, %%cr3" : : "r"(base_cr3) : "memory");
}

static void restore_frames(void)
{
    pt_t[0] = (uint64_t)frames[0] | PTE_FLAGS;
    pd_t[1] = LP0_PHYS | PTE_PS | PTE_FLAGS;
    for (int j = 0; j < 4096 / 8; j++) {
        ((uint64_t *)frames[0])[j] = MARK(1) + j;
    }
    for (int i = 0; i < (int)sizeof(exec_stub); i++) {
        frames[0][EXEC_OFF + i] = exec_stub[i];
    }
}

static int cpu_has_lam(void)
{
    uint32_t a, b, c, d;

    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                 : "a"(7), "c"(1));
    return (a >> 26) & 1;
}

static void test_no_lam_cpu(void)
{
    expect_load("no LAM: tagged load", VA_PAGE(0) | TAG57(1), VEC_GP, 0);
}

static void test_lam_off(void)
{
    set_lam_cr3(0);
    set_lam_sup(0);
    write_cr2(0xdead000);
    expect_load("LAM off: untagged", VA_PAGE(0), VEC_NONE, MARK(1));
    expect_load("LAM off: tag 62:57", VA_PAGE(0) | TAG57(0x2a), VEC_GP, 0);
    check(fault_cr2 == 0xdead000, "LAM off: #GP left CR2 unchanged");
    expect_load("LAM off: tag 56:48", VA_PAGE(0) | TAG48(0x10), VEC_NC4, 0);
    expect_load("LAM off: supervisor tag", VA_SUP & ~(1ULL << 60), VEC_GP,
                0);
}

static void test_cr3_readback(void)
{
    set_lam_cr3(CR3_LAM_U57);
    check(read_cr3() == (base_cr3 | CR3_LAM_U57), "CR3 reads back LAM_U57");
    set_lam_cr3(CR3_LAM_U48);
    check(read_cr3() == (base_cr3 | CR3_LAM_U48), "CR3 reads back LAM_U48");
    set_lam_cr3(0);
}

static void test_u57(void)
{
    uint64_t a = VA_PAGE(0) | TAG57(0x2a);
    uint64_t b = VA_PAGE(0) | TAG57(0x15);
    uint64_t v;

    set_lam_cr3(CR3_LAM_U57);

    expect_load("U57: untagged", VA_PAGE(0), VEC_NONE, MARK(1));
    expect_load("U57: tag 0x2a", a, VEC_NONE, MARK(1));
    expect_load("U57: tag 0x3f", VA_PAGE(0) | TAG57(0x3f), VEC_NONE,
                MARK(1));
    expect_load("U57: tag 0x2a, page 1", VA_PAGE(1) | TAG57(0x2a) | 0x18,
                VEC_NONE, MARK(2) + 3);

    /* Aliases share memory, also when they alternate in one TLB slot. */
    for (int i = 0; i < 8; i++) {
        uint64_t w = i & 1 ? a : b;
        uint64_t r = i & 1 ? b : a;

        expect_store("U57: alias store", w + 8, VEC_NONE, 0x5150 + i);
        expect_load("U57: alias load", r + 8, VEC_NONE, 0x5150 + i);
        expect_load("U57: untagged load", VA_PAGE(0) + 8, VEC_NONE,
                    0x5150 + i);
    }

    /* Bit 56, and with 4-level paging bits 55:47, must equal bit 63. */
    expect_load("U57: bit 56 set", VA_PAGE(0) | (1ULL << 56), VEC_GP, 0);
    expect_load("U57: tag and bit 56 set", a | (1ULL << 56), VEC_GP, 0);
    expect_load("U57: bit 52 set", VA_PAGE(0) | (1ULL << 52), VEC_NC4, 0);
    expect_load("U57: tag and bit 49 set", a | (1ULL << 49), VEC_NC4, 0);
    expect_load("U57: bit 47 set", VA_PAGE(0) | (1ULL << 47), VEC_NC4, 0);

    /* The supervisor half is not masked without LAM_SUP. */
    expect_load("U57: supervisor untagged", VA_SUP, VEC_NONE, MARK(1));
    expect_load("U57: supervisor tagged", VA_SUP & ~TAG57(0x3f), VEC_GP, 0);

    /* #PF through a tagged address reports the masked address in CR2. */
    expect_load("U57: #PF load", VA_PAGE(3) | TAG57(0x2a) | 0x40, VEC_PF,
                0);
    check(fault_cr2 == (VA_PAGE(3) | 0x40), "U57: #PF load CR2 is masked");
    check(fault_err == 0, "U57: #PF load error code");
    expect_store("U57: #PF store", VA_PAGE(3) | TAG57(0x2a) | 0x48, VEC_PF,
                 0);
    check(fault_cr2 == (VA_PAGE(3) | 0x48), "U57: #PF store CR2 is masked");
    check(fault_err == 2, "U57: #PF store error code");

    /* Accesses that cross a page boundary. */
    {
        uint64_t want, cr2;

        try_load(VA_PAGE(1) - 4, &want);
        expect_load("U57: cross-page load", (VA_PAGE(1) - 4) | TAG57(9),
                    VEC_NONE, want);
        expect_load("U57: cross-page #PF untagged", VA_PAGE(3) - 4, VEC_PF,
                    0);
        cr2 = fault_cr2;
        expect_load("U57: cross-page #PF", (VA_PAGE(3) - 4) | TAG57(9),
                    VEC_PF, 0);
        check(fault_cr2 == cr2, "U57: cross-page #PF CR2");
    }

    /* Helpers and atomics. */
    {
        uint64_t p = VA_PAGE(2) | TAG57(0x11);
        uint64_t src = VA_PAGE(0) | TAG57(0x22);
        uint64_t dst = (VA_PAGE(2) + 0x300) | TAG57(0x33);
        uint64_t n = 64;
        uint64_t old = 5, lo = 1, hi = 2;
        uint8_t ok;

        try_store(p + 0x100, 40);
        asm volatile("lock xaddq %0, (%1)" : "+r"(old) : "r"(p + 0x100)
                     : "memory");
        check(old == 40, "U57: lock xadd old value");
        expect_load("U57: lock xadd result", VA_PAGE(2) + 0x100, VEC_NONE,
                    45);

        try_store(VA_PAGE(2) + 0x200, 1);
        try_store(VA_PAGE(2) + 0x208, 2);
        asm volatile("lock cmpxchg16b (%[p])\n\tsetz %[ok]"
                     : "+a"(lo), "+d"(hi), [ok] "=q"(ok)
                     : [p] "r"(p + 0x200), "b"(0x33ULL), "c"(0x44ULL)
                     : "memory");
        check(ok, "U57: cmpxchg16b succeeded");
        expect_load("U57: cmpxchg16b low", VA_PAGE(2) + 0x200, VEC_NONE,
                    0x33);
        expect_load("U57: cmpxchg16b high", VA_PAGE(2) + 0x208, VEC_NONE,
                    0x44);

        asm volatile("rep movsb" : "+S"(src), "+D"(dst), "+c"(n)
                     : : "memory");
        expect_load("U57: rep movsb", VA_PAGE(2) + 0x300 + 16, VEC_NONE,
                    MARK(1) + 2);

        try_store(VA_PAGE(2) + 0x400, 0);
        asm volatile("fninit\n\tfxsave64 (%0)" : : "r"(p + 0x400)
                     : "memory");
        try_load(VA_PAGE(2) + 0x400, &v);
        check((v & 0xffff) == 0x37f, "U57: fxsave64 FCW");
    }

    /* Instruction fetches are not masked. */
    {
        uint64_t code = VA_PAGE(0) + EXEC_OFF;
        int ran;

        expect_fault("U57: exec untagged", try_exec(code, &ran), VEC_NONE);
        check(ran == 0x1234, "U57: exec untagged ran");

        set_lam_cr3(CR3_LAM_U57);
        expect_fault("U57: exec tagged, empty TLB",
                     try_exec(code | TAG57(0x2a), &ran), VEC_GP);
        check(!ran, "U57: exec tagged, empty TLB, did not run");

        set_lam_cr3(CR3_LAM_U57);
        expect_load("U57: data load before exec", code | TAG57(0x2a),
                    VEC_NONE, *(uint64_t *)&frames[0][EXEC_OFF]);
        expect_fault("U57: exec tagged after data load",
                     try_exec(code | TAG57(0x2a), &ran), VEC_GP);
        check(!ran, "U57: exec tagged after data load did not run");
    }

    /* Turning LAM off flushes entries for tagged addresses. */
    expect_load("U57: before CR3 write", a, VEC_NONE, MARK(1));
    set_lam_cr3(0);
    expect_load("U57: after LAM_U57 cleared", a, VEC_GP, 0);
}

static void test_u48(void)
{
    uint64_t a = VA_PAGE(0) | TAG48(0x1234);
    uint64_t b = VA_PAGE(0) | TAG48(0x7fff);

    set_lam_cr3(CR3_LAM_U48);
    expect_load("U48: tag 0x1234", a, VEC_NONE, MARK(1));
    expect_load("U48: tag 0x7fff", b, VEC_NONE, MARK(1));
    expect_store("U48: alias store", a + 16, VEC_NONE, 0x4848);
    expect_load("U48: alias load", b + 16, VEC_NONE, 0x4848);
    expect_load("U48: bit 47 set", VA_PAGE(0) | (1ULL << 47), VEC_GP, 0);
    expect_load("U48: tag and bit 47 set", a | (1ULL << 47), VEC_GP, 0);
    expect_load("U48: #PF", VA_PAGE(3) | TAG48(0x77) | 8, VEC_PF, 0);
    check(fault_cr2 == (VA_PAGE(3) | 8), "U48: #PF CR2 is masked");

    /* LAM_U57 takes precedence over LAM_U48. */
    set_lam_cr3(CR3_LAM_U57 | CR3_LAM_U48);
    expect_load("U57+U48: tag 62:57", VA_PAGE(0) | TAG57(5), VEC_NONE,
                MARK(1));
    expect_load("U57+U48: bit 50", VA_PAGE(0) | (1ULL << 50), VEC_NC4, 0);
    set_lam_cr3(0);
}

static void test_sup(void)
{
    uint64_t a = STAG(0x1a);
    uint64_t b = STAG(0x01);
    uint64_t user = VA_PAGE(0) | TAG57(1);
    uint64_t x = STAG(0x2e) & ~(1ULL << 50);

    set_lam_cr3(0);
    set_lam_sup(1);
    check(read_cr4() & CR4_LAM_SUP, "CR4.LAM_SUP reads back");
    expect_load("SUP: untagged", VA_SUP, VEC_NONE, MARK(1));
    expect_load("SUP: tagged", a, VEC_NONE, MARK(1));
    expect_store("SUP: alias store", b + 24, VEC_NONE, 0x5555);
    expect_load("SUP: alias load", a + 24, VEC_NONE, 0x5555);
    expect_load("SUP: highest address bit clear", SUP_BAD, VEC_GP, 0);
    expect_load("SUP: user half not masked", user, VEC_GP, 0);
    expect_load("SUP: #PF", STAG(0x22) + 3 * 4096 + 0x10, VEC_PF, 0);
    check(fault_cr2 == (VA_SUP + 3 * 4096 + 0x10), "SUP: #PF CR2 is masked");

    /*
     * Bit 50 is metadata with 4-level paging.  With 5-level paging it
     * selects PML5 entry 0x1fb, which is not present.
     */
#ifdef LA57
    expect_load("SUP: bit 50 clear", x, VEC_PF, 0);
    check(fault_cr2 == (VA_SUP & ~(1ULL << 50)), "SUP: bit 50 clear CR2");
#else
    expect_load("SUP: bit 50 clear", x, VEC_NONE, MARK(1));
#endif

    /* Turning LAM_SUP off flushes entries for tagged addresses. */
    expect_load("SUP: before CR4 write", a, VEC_NONE, MARK(1));
    set_lam_sup(0);
    expect_load("SUP: after LAM_SUP cleared", a, VEC_GP, 0);
}

#ifdef LA57
/*
 * With LAM_U48, fetches from VA_HIGH reach frames[2] and data accesses
 * reach frames[0], in either order.
 */
static void test_u48_la57_split(void)
{
    uint64_t data = VA_HIGH, code = VA_HIGH + EXEC_OFF;
    int ran;

    set_lam_cr3(0);
    expect_load("U48 LA57: LAM off, data", data, VEC_NONE, MARK(3));

    set_lam_cr3(CR3_LAM_U48);
    expect_fault("U48 LA57: fetch first", try_exec(code, &ran), VEC_NONE);
    check(ran == 0x2222, "U48 LA57: fetch first ran frames[2]");
    expect_load("U48 LA57: data after fetch", data, VEC_NONE, MARK(1));
    expect_fault("U48 LA57: fetch after data", try_exec(code, &ran),
                 VEC_NONE);
    check(ran == 0x2222, "U48 LA57: fetch after data ran frames[2]");
    expect_load("U48 LA57: data after second fetch", data, VEC_NONE,
                MARK(1));

    set_lam_cr3(CR3_LAM_U48);
    expect_load("U48 LA57: data first", data, VEC_NONE, MARK(1));
    expect_fault("U48 LA57: fetch after data first", try_exec(code, &ran),
                 VEC_NONE);
    check(ran == 0x2222, "U48 LA57: fetch after data first ran frames[2]");

    set_lam_cr3(0);
}
#endif

/* INVLPG of the untagged address must flush every tagged alias. */
static void test_invlpg_4k(const char *what, uint64_t lam, int sup,
                           uint64_t base, uint64_t a, uint64_t b)
{
    restore_frames();
    set_lam_cr3(lam);
    set_lam_sup(sup);

    expect_load(what, a, VEC_NONE, MARK(1));
    expect_load(what, b, VEC_NONE, MARK(1));
    expect_load(what, base, VEC_NONE, MARK(1));

    pt_t[0] = (uint64_t)frames[3] | PTE_FLAGS;
    expect_fault(what, try_invlpg(base), VEC_NONE);

    expect_load(what, a, VEC_NONE, MARK(4));
    expect_load(what, b, VEC_NONE, MARK(4));
    expect_load(what, base, VEC_NONE, MARK(4));

    /* INVLPG of a tagged operand is a no-op and does not fault. */
    expect_fault(what, try_invlpg(a), VEC_NONE);

    restore_frames();
    set_lam_cr3(0);
    set_lam_sup(0);
}

/* The same for a 2 MiB page, with INVLPG of a different 4 KiB part. */
static void test_invlpg_2m(void)
{
    uint64_t t = TAG57(0x2a);

    restore_frames();
    set_lam_cr3(CR3_LAM_U57);

    expect_load("INVLPG 2M: before", (VA_LP + 0x5000) | t, VEC_NONE,
                0xaaaaaaaaaaaa5000ULL);
    pd_t[1] = LP1_PHYS | PTE_PS | PTE_FLAGS;
    expect_fault("INVLPG 2M: invlpg", try_invlpg(VA_LP + 0x1000),
                 VEC_NONE);
    expect_load("INVLPG 2M: after, other 4K part", (VA_LP + 0x5000) | t,
                VEC_NONE, 0xbbbbbbbbbbbb5000ULL);
    expect_load("INVLPG 2M: after, first 4K part", VA_LP | t, VEC_NONE,
                0xbbbbbbbbbbbbbbbbULL);

    restore_frames();
    set_lam_cr3(0);
}

int main(void)
{
    int lam;

    setup_idt();
    setup_paging();

#ifdef LA57
    check(read_cr4() & CR4_LA57, "CR4.LA57 is set");
#endif
    lam = cpu_has_lam();
    check(lam == EXPECT_LAM,
          EXPECT_LAM ? "CPUID reports LAM" : "CPUID reports no LAM");
    if (!lam) {
        ml_printf("CPUID reports no LAM\n");
        test_no_lam_cpu();
    } else {
        ml_printf("CPUID reports LAM\n");
        test_lam_off();
        test_cr3_readback();
        test_u57();
        test_u48();
        test_sup();
        test_invlpg_4k("INVLPG U57", CR3_LAM_U57, 0, VA_PAGE(0),
                       VA_PAGE(0) | TAG57(0x2a), VA_PAGE(0) | TAG57(0x15));
        test_invlpg_4k("INVLPG U48", CR3_LAM_U48, 0, VA_PAGE(0),
                       VA_PAGE(0) | TAG48(0x1234),
                       VA_PAGE(0) | TAG48(0x42));
        test_invlpg_4k("INVLPG SUP", 0, 1, VA_SUP, STAG(0x1a), STAG(0x05));
        test_invlpg_2m();
#ifdef LA57
        test_u48_la57_split();
#endif
    }

    ml_printf("%d passed, %d failed\n", passes, failures);
    return failures ? 1 : 0;
}
