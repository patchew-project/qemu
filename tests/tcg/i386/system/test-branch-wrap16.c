/*
 * Regression test: a 16-bit near JMP rel16 whose target wraps EIP through
 * 0xFFFF must land at the truncated target, not at the unwrapped one.
 *
 * gen_jmp_rel() in target/i386/tcg/translate.c decides whether it is safe
 * to chain translation blocks (goto_tb) by checking whether the *linear*
 * address of the branch target lands on the same guest page as the start
 * of the current translation block. For a 16-bit branch (MO_16) the wrap
 * boundary is at 0x10000 in EIP, not in the linear address, so passing
 * that same-page test does not prove the EIP addition did not wrap: two
 * linear addresses can share a page while the corresponding EIP values
 * straddle 0x10000. When that happens on the CF_PCREL path (system-mode
 * TCG, i.e. here, not linux-user, which is why this is not alongside
 * test-i386-code16.S) the masking AND is skipped and EIP is left with
 * the raw, unwrapped, out-of-range value.
 *
 * This only shows up for a branch site whose *own* linear address and
 * whose *unwrapped* target's linear address fall on the same page, which
 * additionally requires cs_base not to be 4 KiB-aligned (with an aligned
 * cs_base the 0x10000 wrap in EIP always coincides with a page boundary
 * in the linear address, and the same-page test can never be fooled).
 *
 * The test below runs the identical wrapping JMP rel16 from two sites 48
 * bytes apart in a purpose-built 16-bit code segment:
 *
 *   A (control): TB starts on a different page than the unwrapped target
 *                -> masked correctly even before the fix -> must pass.
 *   B (regression check): TB starts on the SAME page as the unwrapped
 *                target -> this is exactly where the defect manifested.
 *
 * Both land on a small hand-encoded stub that records where it arrived
 * (via a 32-bit-addressed store, using DS which is still the flat data
 * segment throughout) and far-jumps back into 32-bit code to report it.
 */

#include <stdint.h>
#include <minilib.h>

struct gdt_desc {
    uint16_t limit_lo;
    uint16_t base_lo;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  limit_hi_flags;
    uint8_t  base_hi;
};

struct gdtr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

/*
 * Selectors 0x08/0x10 must describe exactly the same flat code/data
 * segments boot.S already loaded into CS/DS/ES/SS/FS/GS: those registers
 * are never reloaded here, so their cached (shadow) descriptor state has
 * to remain valid against this replacement table.
 */
#define SEL_CODE32 0x08
#define SEL_DATA32 0x10
#define SEL_CODE16 0x18

static struct gdt_desc test_gdt[4];
static struct gdtr test_gdtr;

static void set_desc(struct gdt_desc *d, uint32_t base, uint32_t limit,
                      uint8_t access, uint8_t gran)
{
    d->limit_lo = limit & 0xffff;
    d->base_lo = base & 0xffff;
    d->base_mid = (base >> 16) & 0xff;
    d->access = access;
    d->limit_hi_flags = ((limit >> 16) & 0x0f) | (gran & 0xf0);
    d->base_hi = (base >> 24) & 0xff;
}

/*
 * A page (4 KiB) of headroom, rounded up to at runtime, so that
 * cs_base = page_base(arena) + 0x10 is guaranteed NOT 4 KiB-aligned
 * (cs_base & 0xfff == 0x10) while every other bit of cs_base's low 16
 * bits is otherwise known exactly relative to a 4 KiB page boundary.
 * That is what makes the branch-site/target page relationship below
 * fall out of plain arithmetic instead of luck.
 *
 * This is computed at runtime rather than via an
 * __attribute__((aligned(0x10000))) buffer deliberately: boot.S's
 * multiboot header uses the AOUT-kludge (flags bit 0x10000), which
 * assumes a single constant "file offset -> load address" mapping
 * across the whole image. A 64 KiB-aligned .bss object makes the
 * linker widen that segment's own file alignment past 4 KiB, which
 * breaks that assumption and corrupts the load. Ordinary alignment on
 * the buffer plus a runtime round-up avoids perturbing the segment
 * layout entirely.
 *
 * No paging is set up by boot.S, so linear == physical here and a
 * pointer into arena is directly usable as a linear address.
 */
#define ARENA_SIZE (0x11000 + 0x1000)
static uint8_t arena[ARENA_SIZE] __attribute__((aligned(16)));

/*
 * Geometry (base = page-aligned-up(arena), cs_base = base + 0x10):
 *
 *   branch A  at IP 0xFFC0 -> linear base+0xFFD0, page base+0xF000
 *   branch B  at IP 0xFFF0 -> linear base+0x10000, page base+0x10000
 *   both target IP 0x0020  -> linear base+0x10030 (masked, correct)
 *   unwrapped EIP for both is 0x10020 -> linear base+0x10030 (untruncated)
 *
 * A: unwrapped target's page (base+0x10000) differs from A's own TB
 *    page (base+0xF000) -> gen_jmp_rel() emits the AND -> correct.
 * B: unwrapped target's page (base+0x10000) is the SAME as B's own TB
 *    page (base+0x10000) -> the AND used to be skipped -> defect.
 */
#define BRANCH_A_IP 0xFFC0u
#define BRANCH_B_IP 0xFFF0u
#define TARGET_IP   0x0020u
#define ARENA_OFF(ip) (0x10u + (ip))

static uint8_t *base;

/* rel16 for "jmp" (3-byte instruction), measured from the next IP */
#define A_REL ((uint16_t)((0x10000u + TARGET_IP) - (BRANCH_A_IP + 3u)))
#define B_REL ((uint16_t)((0x10000u + TARGET_IP) - (BRANCH_B_IP + 3u)))

#define RESULT_NONE  0u
#define RESULT_RIGHT 1u
#define RESULT_WRONG 2u
static volatile uint32_t g_result;

static void write_branch(uint32_t off, uint16_t rel)
{
    base[off + 0] = 0xE9;              /* jmp rel16 */
    base[off + 1] = rel & 0xff;
    base[off + 2] = (rel >> 8) & 0xff;
}

/*
 * A landing stub: "mov byte [addr32], marker" (address-size override,
 * since the default in a 16-bit code segment is 16-bit addressing) then
 * a far jump back to 32-bit flat code ("data32 ljmp $sel, $off32").
 */
static void write_stub(uint32_t off, uint8_t marker, uint32_t ret_addr)
{
    uint32_t addr = (uint32_t)&g_result;
    uint8_t *p = &base[off];
    int i = 0;

    p[i++] = 0x67;                      /* address-size override */
    p[i++] = 0xC6;                      /* MOV r/m8, imm8 */
    p[i++] = 0x05;                      /* modrm: disp32, no base */
    p[i++] = addr & 0xff;
    p[i++] = (addr >> 8) & 0xff;
    p[i++] = (addr >> 16) & 0xff;
    p[i++] = (addr >> 24) & 0xff;
    p[i++] = marker;

    p[i++] = 0x66;                      /* operand-size override */
    p[i++] = 0xEA;                      /* JMP ptr16:32 (direct far jump) */
    p[i++] = ret_addr & 0xff;
    p[i++] = (ret_addr >> 8) & 0xff;
    p[i++] = (ret_addr >> 16) & 0xff;
    p[i++] = (ret_addr >> 24) & 0xff;
    p[i++] = SEL_CODE32 & 0xff;
    p[i++] = (SEL_CODE32 >> 8) & 0xff;
}

static void write_stubs(uint32_t ret_addr)
{
    write_stub(ARENA_OFF(TARGET_IP), RESULT_RIGHT, ret_addr);
    write_stub(ARENA_OFF(TARGET_IP + 0x10000u), RESULT_WRONG, ret_addr);
}

int main(void)
{
    uint32_t cs_base;

    base = (uint8_t *)(((uint32_t)arena + 0xFFFu) & ~0xFFFu);
    cs_base = (uint32_t)base + 0x10;

    /* entries 0/1/2 mirror boot.S's null/code32/data32 descriptors */
    set_desc(&test_gdt[0], 0, 0, 0, 0);
    set_desc(&test_gdt[1], 0, 0xFFFFF, 0x9b, 0xC0);
    set_desc(&test_gdt[2], 0, 0xFFFFF, 0x93, 0xC0);
    /* a genuine 16-bit code segment: G=0, D/B=0, byte-granular 64K limit */
    set_desc(&test_gdt[3], cs_base, 0xFFFF, 0x9b, 0x00);

    test_gdtr.limit = sizeof(test_gdt) - 1;
    test_gdtr.base = (uint32_t)&test_gdt;

    write_branch(ARENA_OFF(BRANCH_A_IP), A_REL);
    write_branch(ARENA_OFF(BRANCH_B_IP), B_REL);

    asm volatile("lgdt %0" : : "m"(test_gdtr) : "memory");

    /* -- A: control. Must already pass before the fix. -- */
    write_stubs((uint32_t)&&L_return_a);
    g_result = RESULT_NONE;
    asm volatile("ljmpl $0x18, $0xFFC0" : : : "memory");
L_return_a:
    if (g_result != RESULT_RIGHT) {
        ml_printf("FAIL: control branch A landed wrong (result=%d)\n",
                   (int)g_result);
        return 1;
    }
    ml_printf("A (control, different page): landed correctly\n");

    /* -- B: the regression check. -- */
    write_stubs((uint32_t)&&L_return_b);
    g_result = RESULT_NONE;
    asm volatile("ljmpl $0x18, $0xFFF0" : : : "memory");
L_return_b:
    if (g_result != RESULT_RIGHT) {
        ml_printf("FAIL: branch wraparound left EIP untruncated "
                   "(result=%d)\n", (int)g_result);
        return 1;
    }
    ml_printf("B (same page as unwrapped target): landed correctly\n");

    ml_printf("PASS\n");
    return 0;
}
