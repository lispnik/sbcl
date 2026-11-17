/*
 * Compiler compatibility for building the runtime with Open Watcom C
 * (DOS target).  Force-included ahead of every source file (wcc386 -fi).
 *
 * This software is part of the SBCL system. See the README file for
 * more information.
 */
#ifndef SBCL_DOS_COMPAT_H
#define SBCL_DOS_COMPAT_H
#ifdef __WATCOMC__

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <alloca.h>
#include <strings.h>
#include <sys/types.h>

/* The C runtime presents a POSIX-like signal model on DOS (see
 * dos-include/signal.h and dos-os.c), so it takes the same paths through
 * the common code as the Unix ports.  The Lisp side does NOT have :UNIX. */
#define LISP_FEATURE_UNIX

/* GCC attributes: all advisory here except packed/aligned, which are
 * handled at their (few) use sites. */
#define __attribute__(x)
#define __attribute(x)
#define __extension__
#define __builtin_expect(x, y) (x)

/* The DOS port is single-threaded with synchronous "signals", so the GCC
 * atomic builtins reduce to plain C.  They operate on word-sized objects. */
static inline uintptr_t dos_sync_vcas(uintptr_t *p, uintptr_t o, uintptr_t n)
{ uintptr_t v = *p; if (v == o) *p = n; return v; }
static inline uintptr_t dos_sync_fetch_add(uintptr_t *p, uintptr_t n)
{ uintptr_t v = *p; *p = v + n; return v; }
static inline uintptr_t dos_sync_fetch_and(uintptr_t *p, uintptr_t n)
{ uintptr_t v = *p; *p = v & n; return v; }
static inline uintptr_t dos_sync_fetch_or(uintptr_t *p, uintptr_t n)
{ uintptr_t v = *p; *p = v | n; return v; }
#define __sync_val_compare_and_swap(p, o, n) \
    dos_sync_vcas((uintptr_t *)(p), (uintptr_t)(o), (uintptr_t)(n))
#define __sync_bool_compare_and_swap(p, o, n) \
    (dos_sync_vcas((uintptr_t *)(p), (uintptr_t)(o), (uintptr_t)(n)) == (uintptr_t)(o))
#define __sync_fetch_and_add(p, n) dos_sync_fetch_add((uintptr_t *)(p), (uintptr_t)(n))
#define __sync_add_and_fetch(p, n) (dos_sync_fetch_add((uintptr_t *)(p), (uintptr_t)(n)) + (uintptr_t)(n))
#define __sync_fetch_and_and(p, n) dos_sync_fetch_and((uintptr_t *)(p), (uintptr_t)(n))
#define __sync_fetch_and_or(p, n) dos_sync_fetch_or((uintptr_t *)(p), (uintptr_t)(n))

/* Frame address of the calling function.  The runtime is compiled with
 * -of+ so EBP is always a frame pointer. */
void *dos_frame_address(void);
#pragma aux dos_frame_address = "mov eax,ebp" __value [__eax] __modify __exact [__eax]
#define __builtin_frame_address(n) dos_frame_address()

static inline int dos_ffs(unsigned x)
{ int i; if (!x) return 0; for (i = 1; !(x & 1); x >>= 1) i++; return i; }
#define __builtin_ffs(x) dos_ffs(x)

/* Don't use the library's bzero(): in this build bzero(p, 36) was seen
 * filling memory with 0x24s until it ran off the end of static space
 * (cause not investigated).  bcopy() gets the same treatment to be safe. */
#undef bzero
#undef bcopy
#define bzero(p, n) ((void)memset((p), 0, (n)))
#define bcopy(s, d, n) ((void)memmove((d), (s), (n)))

/* POSIX odds and ends the C library lacks */
typedef char *caddr_t;
#include <errno.h>
#include <fcntl.h>
#ifndef ELOOP
#define ELOOP 1000              /* no symbolic links: never happens */
#endif
#ifndef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN
#endif
#ifndef O_NOCTTY
#define O_NOCTTY 0
#endif
#define _SC_NPROCESSORS_ONLN 84
#define getpagesize() 4096
#ifndef PATH_MAX
#define PATH_MAX 260
#endif
#ifndef MAXPATHLEN
#define MAXPATHLEN PATH_MAX
#endif
/* Exit codes do not reliably reach COMMAND.COM (seen on FreeDOS, where
 * ERRORLEVEL is garbage after any HX "DOS PE" program), so the exit code
 * can also be left in a file for whoever started us: see dos_exit(). */
#include <process.h>    /* declares exit() too: must precede the macro */
void dos_exit(int code);
#define exit(code) dos_exit(code)

struct tm;
char *realpath(const char *path, char *resolved);
struct tm *localtime_r(const time_t *when, struct tm *result);
struct tm *gmtime_r(const time_t *when, struct tm *result);
int munmap(void *addr, size_t length);
int mprotect(void *addr, size_t length, int prot);

#endif /* __WATCOMC__ */
#endif
