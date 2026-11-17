/*
 * OS-dependent routines for DOS: 32-bit protected mode under a DPMI 1.0
 * host (HDPMI32 from the HX DOS extender), built with Open Watcom C.
 *
 * What the rest of the runtime expects from an OS, and how DOS supplies it:
 *
 *  - Memory at chosen addresses (static space, the alien linkage table):
 *    DPMI 1.0 function 0504h, "allocate linear memory block" at a
 *    specified linear address.
 *  - Demand-zero memory (dynamic space): blocks are allocated uncommitted
 *    and committed 64KB at a time from the page-fault handler.
 *  - Page protection: DPMI 1.0 function 0507h, "set page attributes".
 *  - Signals with an editable context: DPMI processor exceptions are
 *    converted to calls of sigaction()-installed handlers; see
 *    x86-dos-exc.asm for the low-level half.
 *  - dlsym(): a static table, dos_symtab[] (generated; see Makefile.dos).
 */

/*
 * This software is part of the SBCL system. See the README file for
 * more information.
 *
 * This software is derived from the CMUCL system, which was
 * written at Carnegie Mellon University and released into the
 * public domain. The software is in the public domain and is
 * provided with absolutely no warranty. See the COPYING and CREDITS
 * files for more information.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <i86.h>
#include <dos.h>
#include <time.h>
#include <sys/resource.h>
#include "genesis/sbcl.h"
#include "os.h"
#include "arch.h"
#include "globals.h"
#include "interrupt.h"
#include "interr.h"
#include "lispregs.h"
#include "runtime.h"
#include "thread.h"
#include "gc.h"

/* ---------------------------------------------------------------- DPMI */

static unsigned short get_cs(void);
#pragma aux get_cs = "mov ax,cs" __value [__ax]
static unsigned short get_ds(void);
#pragma aux get_ds = "mov ax,ds" __value [__ax]

/* DPMI 0504h: allocate a linear memory block, at ADDR if nonzero. */
static int dpmi_alloc_linear(uintptr_t addr, size_t size, int commit,
                             uintptr_t *base, unsigned *handle)
{
    union REGS r;
    memset(&r, 0, sizeof r);
    r.w.ax = 0x504; r.x.ebx = addr; r.x.ecx = size; r.x.edx = commit ? 1 : 0;
    int386(0x31, &r, &r);
    if (r.x.cflag) return -1;
    *base = r.x.ebx; *handle = r.x.esi;
    return 0;
}

/* DPMI 0502h: free a memory block. */
static void dpmi_free(unsigned handle)
{
    union REGS r;
    memset(&r, 0, sizeof r);
    r.w.ax = 0x502; r.w.si = handle >> 16; r.w.di = handle & 0xffff;
    int386(0x31, &r, &r);
}

/* DPMI 0507h: set page attributes.  1 = committed read-only,
 * 9 = committed read/write. */
#define PAGE_UNCOMMITTED 0
#define PAGE_RO 1
#define PAGE_RW 9
static int dpmi_set_page_attributes(unsigned handle, size_t offset, unsigned npages,
                                    unsigned short attr)
{
    static unsigned short attrs[256];
    union REGS r; struct SREGS s;
    unsigned i;
    for (i = 0; i < 256; i++) attrs[i] = attr;
    while (npages) {
        unsigned n = npages > 256 ? 256 : npages;
        memset(&r, 0, sizeof r); segread(&s);
        r.w.ax = 0x507; r.x.esi = handle; r.x.ebx = offset; r.x.ecx = n;
        r.x.edx = (unsigned)attrs;
        int386x(0x31, &r, &r, &s);
        if (r.x.cflag) return -1;
        offset += n * 4096; npages -= n;
    }
    return 0;
}

/* ------------------------------------------------------ memory blocks */

#define CHUNK_SHIFT 16          /* demand-commit granule: 64KB */
#define CHUNK_BYTES (1u << CHUNK_SHIFT)

struct dos_block {
    uintptr_t base;
    size_t size;
    unsigned handle;
    unsigned char *committed;   /* per-chunk flags if demand-committed, else 0 */
};
#define MAX_BLOCKS 64
static struct dos_block blocks[MAX_BLOCKS];
static int n_blocks;

static struct dos_block *find_block(uintptr_t addr)
{
    int i;
    for (i = 0; i < n_blocks; i++)
        if (addr >= blocks[i].base && addr - blocks[i].base < blocks[i].size)
            return &blocks[i];
    return 0;
}

/* Make sure the chunk of B containing ADDR has memory behind it. */
static int commit_chunk(struct dos_block *b, uintptr_t addr)
{
    size_t chunk = (addr - b->base) >> CHUNK_SHIFT;
    size_t offset = chunk << CHUNK_SHIFT;
    size_t len = b->size - offset;
    if (b->committed[chunk]) return 0;  /* not ours to fix */
    if (len > CHUNK_BYTES) len = CHUNK_BYTES;
    if (dpmi_set_page_attributes(b->handle, offset, len >> 12, PAGE_RW) < 0) {
        fprintf(stderr, "Out of memory: cannot commit %lu bytes at %p\n",
                (unsigned long)len, (void *)(b->base + offset));
        exit(1);
    }
    memset((void *)(b->base + offset), 0, len);
    b->committed[chunk] = 1;
    return 1;
}

/* Commit every page in [addr, addr+len) so that code which must not take
 * a page fault (DOS, the DPMI host) can use it. */
void dos_prefault(void *address, size_t len)
{
    uintptr_t addr = (uintptr_t)address, end = addr + len;
    struct dos_block *b;
    if (!len || !(b = find_block(addr)) || !b->committed) return;
    if (end > b->base + b->size) end = b->base + b->size;
    for (addr &= ~(uintptr_t)(CHUNK_BYTES - 1); addr < end; addr += CHUNK_BYTES)
        commit_chunk(b, addr);
}

/* Give [addr, addr+len) back to the DPMI host where possible; either
 * way its contents read as zero afterwards.  This is what the GC uses
 * instead of re-mmap()ing freed pages. */
void dos_release_range(void *address, size_t len)
{
    uintptr_t addr = (uintptr_t)address, end = addr + len;
    struct dos_block *b = find_block(addr);
    uintptr_t first, last;
    if (!b || !b->committed || end > b->base + b->size) { memset(address, 0, len); return; }
    /* whole chunks, relative to the block */
    first = (addr - b->base + CHUNK_BYTES - 1) >> CHUNK_SHIFT;
    last = (end - b->base) >> CHUNK_SHIFT;
    if (first >= last) { memset(address, 0, len); return; }
    memset(address, 0, b->base + (first << CHUNK_SHIFT) - addr);
    memset((void *)(b->base + (last << CHUNK_SHIFT)), 0, end - (b->base + (last << CHUNK_SHIFT)));
    for (; first < last; first++) {
        if (!b->committed[first]) continue;
        if (dpmi_set_page_attributes(b->handle, first << CHUNK_SHIFT, CHUNK_BYTES >> 12,
                                     PAGE_UNCOMMITTED) < 0)
            memset((void *)(b->base + (first << CHUNK_SHIFT)), 0, CHUNK_BYTES);
        else
            b->committed[first] = 0;
    }
}

os_vm_address_t
os_alloc_gc_space(int space_id, int attributes, os_vm_address_t addr, os_vm_size_t len)
{
    uintptr_t base = 0;
    unsigned handle = 0;
    /* Dynamic space is big and mostly untouched: commit it on demand. */
    int lazy = (space_id == DYNAMIC_CORE_SPACE_ID);
    struct dos_block *b;

    if (n_blocks == MAX_BLOCKS) return 0;
    len = ALIGN_UP(len, 4096);
    if (!addr || dpmi_alloc_linear((uintptr_t)addr, len, !lazy, &base, &handle) < 0) {
        if (addr && !(attributes & MOVABLE)) return 0;
        if (dpmi_alloc_linear(0, len, !lazy, &base, &handle) < 0) return 0;
    }
    b = &blocks[n_blocks];
    b->base = base; b->size = len; b->handle = handle; b->committed = 0;
    if (lazy) {
        b->committed = calloc((len + CHUNK_BYTES - 1) >> CHUNK_SHIFT, 1);
        if (!b->committed) { dpmi_free(handle); return 0; }
    } else {
        memset((void *)base, 0, len);   /* DPMI memory is not zeroed */
    }
    n_blocks++;
    return (os_vm_address_t)base;
}

/* Unlike DPMI blocks, mmap()ed memory can be given back piecemeal (the
 * hopscotch table cache in the GC does), so only a request covering a
 * whole block frees it.  Anything less just uncommits the whole pages in
 * the range; the address space stays reserved. */
int munmap(void *address, size_t length)
{
    uintptr_t addr = (uintptr_t)address;
    struct dos_block *b = find_block(addr);
    uintptr_t first, last;
    if (!b) return 0;
    if (addr == b->base && length >= b->size) {
        dpmi_free(b->handle);
        free(b->committed);
        *b = blocks[--n_blocks];
        return 0;
    }
    if (b->committed) {         /* demand-committed: let chunks go */
        dos_release_range(address, length);
        return 0;
    }
    first = (addr - b->base + 4095) >> 12;
    last = (addr + length - b->base) >> 12;
    if (last > first)
        dpmi_set_page_attributes(b->handle, first << 12, last - first, PAGE_UNCOMMITTED);
    return 0;
}

/* There is no "no access" page attribute short of decommitting, which
 * would lose the contents, so PROT_NONE is treated as read-only.  That is
 * enough for guard pages, which are there to catch writes. */
void
os_protect(os_vm_address_t address, os_vm_size_t length, os_vm_prot_t prot)
{
    uintptr_t addr = (uintptr_t)address;
    struct dos_block *b = find_block(addr);
    if (!b || !length) return;
    dos_prefault(address, length);
    if (dpmi_set_page_attributes(b->handle, (addr - b->base) & ~(uintptr_t)4095,
                                 (length + (addr & 4095) + 4095) >> 12,
                                 (prot & OS_VM_PROT_WRITE) ? PAGE_RW : PAGE_RO) < 0)
        lose("os_protect(%p,%lx,%d) failed", address, (long)length, prot);
}

int mprotect(void *addr, size_t length, int prot)
{
    os_protect(addr, length, prot);
    return 0;
}

void* load_core_bytes(int fd, os_vm_offset_t offset, os_vm_address_t addr, os_vm_size_t len,
                      int is_readonly_space)
{
    size_t done = 0;
    (void)is_readonly_space;
    dos_prefault(addr, len);
    if (lseek(fd, offset, SEEK_SET) == -1) lose("load_core_bytes: lseek failed");
    while (done < len) {
        size_t want = len - done;
        int got;
        if (want > 0x100000) want = 0x100000;
        got = read(fd, (char *)addr + done, want);
        if (got <= 0)
            lose("load_core_bytes(%d,%lx,%p,%lx) failed", fd, (long)offset, addr, (long)len);
        done += got;
    }
    return (void *)addr;
}

/* ------------------------------------------------------------ signals */

/* There is one thread and no asynchronous delivery: a "signal" is either
 * a processor exception or a raise().  Bit N of a sigset_t is signal N. */
static struct sigaction dos_sigactions[NSIG];
static sigset_t dos_sigmask, dos_sigpending;

extern unsigned dos_exc_ds, dos_exc_depth;
extern void dos_resume(os_context_t *);
extern void dos_capture_context(os_context_t *);

int sigemptyset(sigset_t *set) { *set = 0; return 0; }
int sigfillset(sigset_t *set) { *set = ~0UL; return 0; }
int sigaddset(sigset_t *set, int sig) { *set |= 1UL << sig; return 0; }
int sigdelset(sigset_t *set, int sig) { *set &= ~(1UL << sig); return 0; }
int sigismember(const sigset_t *set, int sig) { return (*set >> sig) & 1; }
int sigpending(sigset_t *set) { *set = dos_sigpending; return 0; }
int sigaltstack(const stack_t *ss, stack_t *old) { (void)ss; (void)old; return 0; }
int sigwait(const sigset_t *set, int *sig) { (void)set; (void)sig; errno = EINVAL; return -1; }

int sigaction(int sig, const struct sigaction *new, struct sigaction *old)
{
    if (sig <= 0 || sig >= NSIG) { errno = EINVAL; return -1; }
    if (old) *old = dos_sigactions[sig];
    if (new) dos_sigactions[sig] = *new;
    return 0;
}

void dos_exception_dispatch(os_context_t *slot);

/* An exception nothing wanted: say as much as we can about it.  Addresses
 * in the runtime can be matched against the linker map (dos-obj/sbcl.map)
 * by comparing with the address printed for dos_exception_dispatch. */
static void dos_crash_report(int sig, siginfo_t *info, os_context_t *context)
{
    unsigned *fp = (unsigned *)context->ebp;
    unsigned char *pc = (unsigned char *)context->eip;
    int i;
    fprintf(stderr, "Unhandled signal %d: trap %u, error code %x, at EIP=%08x, address %p\n"
            "EAX=%08x ECX=%08x EDX=%08x EBX=%08x ESP=%08x EBP=%08x ESI=%08x EDI=%08x EFL=%08x\n",
            sig, context->trapno, context->err, context->eip, info->si_addr,
            context->eax, context->ecx, context->edx, context->ebx,
            context->esp, context->ebp, context->esi, context->edi, context->eflags);
    fprintf(stderr, "(dos_exception_dispatch is at %p)\ncode:", (void *)dos_exception_dispatch);
    if (find_block((uintptr_t)pc) || ((uintptr_t)pc >= 0x110000 && (uintptr_t)pc < 0x200000))
        for (i = -8; i < 16; i++) fprintf(stderr, "%s%02x", i == 0 ? " | " : " ", pc[i]);
    fprintf(stderr, "\nframes:");
    for (i = 0; i < 16 && fp && !((uintptr_t)fp & 3)
             && (find_block((uintptr_t)fp) || ((uintptr_t)fp >= 0x110000 && (uintptr_t)fp < 0x1000000)); i++) {
        fprintf(stderr, " %08x", fp[1]);
        if ((unsigned *)fp[0] <= fp) break;
        fp = (unsigned *)fp[0];
    }
    fprintf(stderr, "\n");
    lose("Unhandled signal %d (trap %u) at EIP=%08x", sig, context->trapno, context->eip);
}

static void deliver_signal(int sig, siginfo_t *info, os_context_t *context)
{
    struct sigaction *sa = &dos_sigactions[sig];
    if (sa->sa_handler == SIG_IGN) return;
    if (sa->sa_handler == SIG_DFL) {
        if (sig == SIGCHLD || sig == SIGURG || sig == SIGWINCH || sig == SIGCONT) return;
        if (sig == SIGINT || sig == SIGTERM) exit(1);
        dos_crash_report(sig, info, context);
    }
    context->sigmask = dos_sigmask;
    dos_sigmask |= sa->sa_mask;
    if (!(sa->sa_flags & SA_NODEFER)) dos_sigmask |= 1UL << sig;
    if (sa->sa_flags & SA_SIGINFO) sa->sa_sigaction(sig, info, context);
    else sa->sa_handler(sig);
    /* As when returning from a real signal handler, the mask comes from
     * the context, which the handler may have changed. */
    dos_sigmask = context->sigmask;
}

static void deliver_pending(void)
{
    int sig;
    for (sig = 1; sig < NSIG; sig++)
        if ((dos_sigpending & ~dos_sigmask) & (1UL << sig)) {
            os_context_t context;
            siginfo_t info;
            dos_sigpending &= ~(1UL << sig);
            memset(&context, 0, sizeof context);
            dos_capture_context(&context);
            memset(&info, 0, sizeof info);
            info.si_signo = sig;
            deliver_signal(sig, &info, &context);
        }
}

int sigprocmask(int how, const sigset_t *set, sigset_t *old)
{
    if (old) *old = dos_sigmask;
    if (set) {
        switch (how) {
        case SIG_BLOCK: dos_sigmask |= *set; break;
        case SIG_UNBLOCK: dos_sigmask &= ~*set; break;
        case SIG_SETMASK: dos_sigmask = *set; break;
        default: errno = EINVAL; return -1;
        }
        if (dos_sigpending & ~dos_sigmask) deliver_pending();
    }
    return 0;
}

int raise(int sig)
{
    if (sig <= 0 || sig >= NSIG) { errno = EINVAL; return -1; }
    dos_sigpending |= 1UL << sig;
    if (!(dos_sigmask & (1UL << sig))) deliver_pending();
    return 0;
}
int kill(int pid, int sig) { (void)pid; return sig ? raise(sig) : 0; }
int killpg(int pgrp, int sig) { (void)pgrp; return sig ? raise(sig) : 0; }

/* Called by the trampoline in x86-dos-exc.asm on the interrupted stack,
 * with the context slot the exception stub filled in.  Never returns. */
void dos_exception_dispatch(os_context_t *slot)
{
    os_context_t context;
    siginfo_t info;
    int sig;

    /* Free the slot right away: the handler may unwind instead of returning. */
    memcpy(&context, slot, sizeof context);
    dos_exc_depth--;

    if (context.trapno == 14) {
        struct dos_block *b = find_block(context.cr2);
        if (b && b->committed && commit_chunk(b, context.cr2))
            dos_resume(&context);       /* demand-zero page: retry */
    }
    memset(&info, 0, sizeof info);
    switch (context.trapno) {
    case 1: case 3: sig = SIGTRAP; break;
    case 6: sig = SIGILL; break;
    case 0: sig = SIGFPE; info.si_code = FPE_INTDIV; break;
    case 4: sig = SIGFPE; info.si_code = FPE_INTOVF; break;
    case 16: {
        unsigned sw = *(unsigned short *)(context.fpu + 4);
        sig = SIGFPE;
        info.si_code = (sw & 1) ? FPE_FLTINV : (sw & 4) ? FPE_FLTDIV : (sw & 8) ? FPE_FLTOVF
            : (sw & 16) ? FPE_FLTUND : (sw & 32) ? FPE_FLTRES : FPE_FLTINV;
        break;
    }
    case 14:
        sig = SIGSEGV;
        info.si_code = (context.err & 1) ? SEGV_ACCERR : SEGV_MAPERR;
        info.si_addr = (void *)context.cr2;
        break;
    default: sig = SIGSEGV; break;
    }
    info.si_signo = sig;
    deliver_signal(sig, &info, &context);
    dos_resume(&context);
}

static void set_exception_handler(int n, void (*handler)(void))
{
    union REGS r;
    memset(&r, 0, sizeof r);
    r.w.ax = 0x203; r.h.bl = n; r.w.cx = get_cs(); r.x.edx = (unsigned)handler;
    int386(0x31, &r, &r);
    if (r.x.cflag) lose("cannot install DPMI exception handler %d", n);
}

#if INSTALL_SIG_MEMORY_FAULT_HANDLER
static void
sigsegv_handler(int signal, siginfo_t *info, os_context_t *context)
{
    void* fault_addr = (void*)info->si_addr;
    (void)signal;
    if (gencgc_handle_wp_violation(context, fault_addr)) return;
    if (!handle_guard_page_triggered(context, fault_addr))
        lisp_memory_fault_error(context, fault_addr);
}
#endif

void
os_install_interrupt_handlers(void)
{
#if INSTALL_SIG_MEMORY_FAULT_HANDLER
    ll_install_handler(SIG_MEMORY_FAULT, sigsegv_handler);
#endif
}

/* ------------------------------------------------------------ startup */

extern void dos_exc_entry_0(void), dos_exc_entry_1(void), dos_exc_entry_3(void),
    dos_exc_entry_4(void), dos_exc_entry_5(void), dos_exc_entry_6(void),
    dos_exc_entry_12(void), dos_exc_entry_13(void), dos_exc_entry_14(void),
    dos_exc_entry_16(void);

static char dos_host_name[64] = "unknown DPMI host";

/* DOS keeps local time and has no notion of a time zone, and the C
 * library's default is US Eastern.  Unless told otherwise, treat the
 * clock as UTC.  (Called from gettimeofday() too, because the runtime
 * reads the clock before os_preinit().) */
static void dos_init_timezone(void)
{
    static int done;
    if (done) return;
    done = 1;
    if (!getenv("TZ")) { putenv("TZ=UTC0"); tzset(); }
}

/* Runs before anything else in the runtime (see runtime.c).  The fixed
 * address spaces are claimed here, while the linear address space above
 * the program image is still empty. */
int os_preinit(char *argv[], char *envp[])
{
    static unsigned char buf[128];
    union REGS r; struct SREGS s;
    uintptr_t base; unsigned handle;
    (void)argv; (void)envp;

    /* Text mode would mangle everything the Lisp side reads and writes. */
    _fmode = O_BINARY;
    setmode(0, O_BINARY);
    /* ... except that the console needs a carriage return with each line
     * feed, for the runtime's own messages as much as for Lisp's output. */
    setmode(1, isatty(1) ? O_TEXT : O_BINARY);
    setmode(2, isatty(2) ? O_TEXT : O_BINARY);
    setvbuf(stdout, 0, _IONBF, 0);
    /* COMMAND.COM cannot redirect standard error.  With this set, error
     * output follows standard output, so "SBCL ... > LOG" captures both. */
    if (getenv("SBCL_DOS_MERGE_STDERR")) dup2(1, 2);
    dos_init_timezone();

    memset(&r, 0, sizeof r); segread(&s);
    r.w.ax = 0x401; r.x.edi = (unsigned)buf;
    int386x(0x31, &r, &r, &s);
    if (!r.x.cflag)
        snprintf(dos_host_name, sizeof dos_host_name, "%s %d.%d", buf + 2, buf[0], buf[1]);
    /* Probe for the DPMI 1.0 memory functions we cannot do without. */
    if (dpmi_alloc_linear(0, 4096, 0, &base, &handle) < 0
        || dpmi_set_page_attributes(handle, 0, 1, PAGE_RW) < 0) {
        fprintf(stderr,
                "SBCL needs a DPMI 1.0 host with linear memory functions (0504h, 0507h),\n"
                "such as HDPMI32 from the HX DOS extender; this host (%s)\n"
                "does not provide them.\n", dos_host_name);
        exit(1);
    }
    dpmi_free(handle);

    dos_exc_ds = get_ds();
    set_exception_handler(0, dos_exc_entry_0);
    set_exception_handler(1, dos_exc_entry_1);
    set_exception_handler(3, dos_exc_entry_3);
    set_exception_handler(4, dos_exc_entry_4);
    set_exception_handler(5, dos_exc_entry_5);
    set_exception_handler(6, dos_exc_entry_6);
    set_exception_handler(12, dos_exc_entry_12);
    set_exception_handler(13, dos_exc_entry_13);
    set_exception_handler(14, dos_exc_entry_14);
    set_exception_handler(16, dos_exc_entry_16);

    allocate_hardwired_spaces(1);
    return 1;                   /* the hardwired spaces now exist */
}

void os_init() {}

int sb_GetTID() { return 0; }

char *os_get_runtime_executable_path()
{
    extern char **_argv;        /* the C library keeps the full path here */
    return (_argv && _argv[0]) ? copied_string(_argv[0]) : 0;
}

/* Lisp: SOFTWARE-VERSION */
char *dos_software_version(void)
{
    static char version[128];
    union REGS r;
    memset(&r, 0, sizeof r);
    r.w.ax = 0x3000;
    int386(0x21, &r, &r);
    snprintf(version, sizeof version, "%d.%02d (%s)", r.h.al, r.h.ah, dos_host_name);
    return version;
}

/* All calls of exit() in the runtime come here (see dos-compat.h).  If the
 * environment variable SBCL_DOS_EXIT_CODE_FILE is set, the exit code is
 * written to the file it names before exiting. */
extern void dos_network_shutdown(void);     /* dos-wrap.c */
void dos_record_exit_code(int code)
{
    const char *name = getenv("SBCL_DOS_EXIT_CODE_FILE");
    FILE *f;
    if (name && (f = fopen(name, "w")) != 0) {
        fprintf(f, "%d\n", code);
        fclose(f);
    }
}
#undef exit
void dos_exit(int code)
{
    dos_network_shutdown();
    dos_record_exit_code(code);
    exit(code);
}
#define exit(code) dos_exit(code)

/* ------------------------------------------------- foreign symbol table */

struct dos_symbol { const char *name; void *address; };
extern const struct dos_symbol dos_symtab[];
extern const int dos_symtab_size;

void *os_dlsym_default(char *name)
{
    int lo = 0, hi = dos_symtab_size - 1;       /* sorted by name */
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int cmp = strcmp(name, dos_symtab[mid].name);
        if (!cmp) return dos_symtab[mid].address;
        if (cmp < 0) hi = mid - 1; else lo = mid + 1;
    }
    return 0;
}

/* ------------------------------------------- small libc compatibility */

char *realpath(const char *path, char *resolved)
{
    if (access(path, F_OK) != 0) return 0;
    return _fullpath(resolved, path, resolved ? PATH_MAX : 0);
}

struct tm *localtime_r(const time_t *when, struct tm *result)
{
    return _localtime(when, result);
}

struct tm *gmtime_r(const time_t *when, struct tm *result)
{
    return _gmtime(when, result);
}

int getrusage(int who, struct rusage *usage)
{
    clock_t ticks = clock();
    (void)who;
    memset(usage, 0, sizeof *usage);
    usage->ru_utime.tv_sec = ticks / CLOCKS_PER_SEC;
    usage->ru_utime.tv_usec = (ticks % CLOCKS_PER_SEC) * (1000000 / CLOCKS_PER_SEC);
    return 0;
}

/* The statistical profiler (sprof.c) needs SIGPROF and is not built. */
void sigprof_handler(int sig, siginfo_t *info, void *context)
{
    (void)sig; (void)info; (void)context;
}
void allocator_record_backtrace(void *fp, struct thread *th)
{
    (void)fp; (void)th;
}

int gettimeofday(struct timeval *tv, void *tz)
{
    struct dostime_t t;
    (void)tz;
    dos_init_timezone();
    /* time() has one-second resolution; take the fraction from the DOS clock */
    /* (again if the second changed between the two readings) */
    do {
        tv->tv_sec = time(0);
        _dos_gettime(&t);
    } while (time(0) != tv->tv_sec);
    tv->tv_usec = t.hsecond * 10000L;
    return 0;
}
