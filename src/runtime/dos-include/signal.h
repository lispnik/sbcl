/* POSIX-style signal definitions for the DOS port of SBCL.
 *
 * DOS has no signals.  The runtime's "signals" are DPMI processor
 * exceptions (and, later, Ctrl-C) that dos-os.c converts into calls of
 * the handlers installed through sigaction().  This header deliberately
 * shadows the C library's <signal.h>; the numbering follows Linux/x86. */
#ifndef SBCL_DOS_SIGNAL_H
#define SBCL_DOS_SIGNAL_H
#include <stddef.h>

#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGILL 4
#define SIGTRAP 5
#define SIGABRT 6
#define SIGBUS 7
#define SIGFPE 8
#define SIGKILL 9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGCHLD 17
#define SIGCONT 18
#define SIGSTOP 19
#define SIGTSTP 20
#define SIGTTIN 21
#define SIGTTOU 22
#define SIGURG 23
#define SIGXCPU 24
#define SIGXFSZ 25
#define SIGVTALRM 26
#define SIGPROF 27
#define SIGWINCH 28
#define SIGIO 29
#define SIGSYS 31
#define NSIG 32

typedef unsigned long sigset_t;
typedef int sig_atomic_t;

typedef struct {
    int si_signo;
    int si_code;
    void *si_addr;
} siginfo_t;

#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIG_ERR ((void (*)(int))-1)

struct sigaction {
    union {
        void (*sa_handler_)(int);
        void (*sa_sigaction_)(int, siginfo_t *, void *);
    } u;
    sigset_t sa_mask;
    int sa_flags;
};
#define sa_handler u.sa_handler_
#define sa_sigaction u.sa_sigaction_

#define SA_NOCLDSTOP 1
#define SA_SIGINFO 4
#define SA_ONSTACK 0x08000000
#define SA_RESTART 0x10000000
#define SA_NODEFER 0x40000000
#define SA_RESETHAND 0x80000000

#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

typedef struct { void *ss_sp; int ss_flags; size_t ss_size; } stack_t;
#define SIGSTKSZ 8192
#define MINSIGSTKSZ 2048
#define SS_ONSTACK 1
#define SS_DISABLE 2

/* si_code values */
#define FPE_INTDIV 1
#define FPE_INTOVF 2
#define FPE_FLTDIV 3
#define FPE_FLTOVF 4
#define FPE_FLTUND 5
#define FPE_FLTRES 6
#define FPE_FLTINV 7
#define FPE_FLTSUB 8
#define SEGV_MAPERR 1
#define SEGV_ACCERR 2

int sigemptyset(sigset_t *);
int sigfillset(sigset_t *);
int sigaddset(sigset_t *, int);
int sigdelset(sigset_t *, int);
int sigismember(const sigset_t *, int);
int sigprocmask(int, const sigset_t *, sigset_t *);
int sigpending(sigset_t *);
int sigaction(int, const struct sigaction *, struct sigaction *);
int sigaltstack(const stack_t *, stack_t *);
int sigwait(const sigset_t *, int *);
int kill(int, int);
int killpg(int, int);
int raise(int);
void (*signal(int, void (*)(int)))(int);

#endif
