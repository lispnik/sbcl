/* S2+S3: fixed-address allocation, page attributes, exceptions with editable context */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <i86.h>

struct ctx { unsigned eax, ecx, edx, ebx, esp, ebp, esi, edi, eip, efl, trap, err, cr2, pad; unsigned char fpu[108]; unsigned pad2; };
extern unsigned exc_ds, exc_depth;
extern void exc_entry_1(void), exc_entry_3(void), exc_entry_6(void), exc_entry_13(void), exc_entry_14(void), exc_entry_16(void);
extern void dos_resume(struct ctx *);
extern int trap_int3(void), trap_ud2(void), single_step(void);
extern int call_on_stack(int (*fn)(void), void *newsp);

static unsigned short get_cs(void);
#pragma aux get_cs = "mov ax,cs" value [ax];
static unsigned short get_ds(void);
#pragma aux get_ds = "mov ax,ds" value [ax];

static int dpmi_alloc_at(unsigned addr, unsigned size, int commit, unsigned *base, unsigned *handle) {
    union REGS r; memset(&r, 0, sizeof r);
    r.w.ax = 0x504; r.x.ebx = addr; r.x.ecx = size; r.x.edx = commit ? 1 : 0;
    int386(0x31, &r, &r);
    if (r.x.cflag) return r.w.ax ? r.w.ax : -1;
    *base = r.x.ebx; *handle = r.x.esi; return 0;
}
static unsigned short attrbuf[1024];
static int dpmi_set_attr(unsigned handle, unsigned off, unsigned npages, unsigned short attr) {
    union REGS r; struct SREGS s; unsigned i;
    while (npages) {
        unsigned n = npages > 1024 ? 1024 : npages;
        for (i = 0; i < n; i++) attrbuf[i] = attr;
        memset(&r, 0, sizeof r); segread(&s);
        r.w.ax = 0x507; r.x.esi = handle; r.x.ebx = off; r.x.ecx = n; r.x.edx = (unsigned)attrbuf;
        int386x(0x31, &r, &r, &s);
        if (r.x.cflag) return r.w.ax ? r.w.ax : -1;
        off += n * 4096; npages -= n;
    }
    return 0;
}
static void set_exc(int n, void (*h)(void)) {
    union REGS r; memset(&r, 0, sizeof r);
    r.w.ax = 0x203; r.h.bl = n; r.w.cx = get_cs(); r.x.edx = (unsigned)h;
    int386(0x31, &r, &r);
    if (r.x.cflag) printf("0203h for exc %d FAILED ax=%04x\n", n, r.w.ax);
}

static jmp_buf jb; static int mode, steps, seen14;
static unsigned fix_handle, fix_base, last_cr2, last_trap, last_esp;
enum { M_SKIP, M_LONGJMP, M_UNPROTECT };

void dos_exception_dispatch(struct ctx *slot) {
    struct ctx c = *slot;
    exc_depth--;                       /* slot released: we may never return */
    last_trap = c.trap; last_cr2 = c.cr2; last_esp = c.esp;
    switch (c.trap) {
    case 3:  /* eip is past the int3; skip the code byte */
        c.ecx = 0x3000 + *(unsigned char *)c.eip; c.eip += 1; break;
    case 6:  /* eip at ud2 */
        c.ecx = 0x6000 + *(unsigned char *)(c.eip + 2); c.eip += 3; break;
    case 1:  steps++; if (steps < 3) c.efl |= 0x100; break;
    case 13: case 14:
        seen14++;
        if (mode == M_LONGJMP) longjmp(jb, c.trap);
        if (mode == M_UNPROTECT) {
            int e = dpmi_set_attr(fix_handle, (c.cr2 & ~0xFFF) - fix_base, 1, 9);
            if (e) { printf("unprotect failed %x\n", e); exit(3); }
            break;
        }
    default:
        printf("unexpected exception %u eip=%08x cr2=%08x err=%x\n", c.trap, c.eip, c.cr2, c.err); exit(2);
    }
    dos_resume(&c);
}
static int both(void) { int a = trap_int3(), b = trap_ud2(); return (a == 0x305A && b == 0x607B) ? 1 : (printf("a=%x b=%x\n", a, b), 0); }

int main(void) {
    unsigned base, handle, dbase, dhandle; int e, i, ok = 1; volatile int n;
    char *stk = malloc(65536);
    exc_ds = get_ds();
    set_exc(1, exc_entry_1); set_exc(3, exc_entry_3); set_exc(6, exc_entry_6);
    set_exc(13, exc_entry_13); set_exc(14, exc_entry_14);

    /* S3a: int3 / ud2 on the normal stack and on a malloc'ed stack */
    printf("int3 -> %x (want 305a), trap %u\n", trap_int3(), last_trap);
    printf("ud2  -> %x (want 607b), trap %u\n", trap_ud2(), last_trap);
    i = call_on_stack(both, stk + 65536 - 16);
    printf("on malloc stack: %s (last esp %08x, stack %p..%p)\n", i ? "ok" : "FAIL", last_esp, stk, stk + 65536);
    ok &= i;
    for (i = 0; i < 100000; i++) if (trap_int3() != 0x305A) { ok = 0; break; }
    printf("100000 int3 traps: %s, depth %u\n", i == 100000 ? "ok" : "FAIL", exc_depth);

    /* S3b: fault + longjmp out, x1000 */
    mode = M_LONGJMP;
    for (n = 0; n < 1000; n++) if (!setjmp(jb)) { *(volatile int *)0xDEAD0000 = 1; printf("no fault!\n"); ok = 0; break; }
    printf("1000 faults+longjmp: n=%d trap %u cr2=%08x depth %u\n", n, last_trap, last_cr2, exc_depth);

    /* S3c: single step */
    steps = 0; single_step(); printf("single-step traps: %d (want 3)\n", steps);

    /* S2: fixed-address allocation and page attributes */
    e = dpmi_alloc_at(0x01000000, 2 << 20, 1, &base, &handle);
    printf("0504h committed 2MB at 01000000: err=%x base=%08x handle=%08x\n", e, base, handle);
    if (!e) {
        fix_base = base; fix_handle = handle;
        memset((void *)base, 0x55, 2 << 20);
        e = dpmi_set_attr(handle, 0, 1, 1);          /* committed, read-only */
        printf("0507h read-only: err=%x; reading %02x\n", e, *(unsigned char *)base);
        mode = M_UNPROTECT; seen14 = 0;
        *(volatile unsigned char *)(base + 0x123) = 0xAA;
        printf("write after protect: faults=%d cr2=%08x value=%02x\n", seen14, last_cr2, *(unsigned char *)(base + 0x123));
    }
    e = dpmi_alloc_at(0x10000000, 256u << 20, 0, &dbase, &dhandle);
    printf("0504h uncommitted 256MB at 10000000: err=%x base=%08x\n", e, dbase);
    if (!e) {
        e = dpmi_set_attr(dhandle, 0, (64u << 20) / 4096, 9);
        printf("0507h commit 64MB: err=%x\n", e);
        if (!e) { memset((void *)dbase, 1, 64u << 20); printf("touched 64MB ok\n"); }
        mode = M_LONGJMP;
        if (!setjmp(jb)) { *(volatile char *)(dbase + (100u << 20)) = 1; printf("uncommitted write did NOT fault\n"); }
        else printf("uncommitted write faulted, cr2=%08x\n", last_cr2);
    }
    printf("malloc(4M) after = %p\n", malloc(4 << 20));
    printf(ok ? "S3 PASS\n" : "S3 FAIL\n");
    return 0;
}
