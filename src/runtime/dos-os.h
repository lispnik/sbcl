/*
 * OS-dependent definitions for DOS: 32-bit protected mode under a
 * DPMI 1.0 host (HDPMI32), C library from Open Watcom.
 *
 * This software is part of the SBCL system. See the README file for
 * more information.
 *
 * This software is derived from the CMUCL system, which was
 * written at Carnegie Mellon University and released into the
 * public domain. The software is in the public domain and is
 * provided with absolutely no warranty. See the COPYING and CREDITS
 * files for more information.
 */
#ifndef _DOS_OS_H
#define _DOS_OS_H

#include <stdlib.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <signal.h>     /* dos-include/signal.h, not the C library's */
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>

// Needs to be defined before including target-arch.h
typedef caddr_t os_vm_address_t;
typedef size_t os_vm_size_t;
typedef off_t os_vm_offset_t;
typedef int os_vm_prot_t;

#include "target-arch-os.h"
#include "target-arch.h"

#define SIG_MEMORY_FAULT SIGSEGV
#define SIG_STOP_FOR_GC (SIGUSR2)

/* Called first thing in main(): claims the fixed-address spaces before the
 * C heap can grow into them, and installs the DPMI exception handlers. */
void dos_early_init(void);
/* Poll for Ctrl-C / Ctrl-Break */
void dos_poll_interrupt(void);

#endif
