/*
 * The x86 DOS incarnation of arch-dependent OS-dependent routines.
 * See also "dos-os.c" and "x86-dos-exc.asm".
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
#include <stddef.h>
#include "genesis/sbcl.h"
#include "globals.h"
#include "os.h"
#include "arch.h"
#include "interrupt.h"
#include "interr.h"
#include "lispregs.h"
#include "thread.h"

extern void dos_fldcw(unsigned short *cw);

int arch_os_thread_init(struct thread *thread) { (void)thread; return 1; }
int arch_os_thread_cleanup(struct thread *thread) { (void)thread; return 1; }

os_context_register_t *
os_context_register_addr(os_context_t *context, int offset)
{
    switch (offset) {
    case reg_EAX: return (os_context_register_t *)&context->eax;
    case reg_ECX: return (os_context_register_t *)&context->ecx;
    case reg_EDX: return (os_context_register_t *)&context->edx;
    case reg_EBX: return (os_context_register_t *)&context->ebx;
    case reg_ESP: return (os_context_register_t *)&context->esp;
    case reg_EBP: return (os_context_register_t *)&context->ebp;
    case reg_ESI: return (os_context_register_t *)&context->esi;
    case reg_EDI: return (os_context_register_t *)&context->edi;
    default: return 0;
    }
}

os_context_register_t *
os_context_sp_addr(os_context_t *context)
{
    return (os_context_register_t *)&context->esp;
}

os_context_register_t *
os_context_fp_addr(os_context_t *context)
{
    return (os_context_register_t *)&context->ebp;
}

/* The FNSAVE image: control word at 0, status word at 4, and the eight
 * 10-byte registers from offset 28. */
unsigned long
os_context_fp_control(os_context_t *context)
{
    unsigned cw = *(unsigned short *)(context->fpu + 0);
    unsigned sw = *(unsigned short *)(context->fpu + 4);
    return ((cw & 0xffff) ^ 0x3f) | ((sw & 0xffff) << 16);
}

sigset_t *
os_context_sigmask_addr(os_context_t *context)
{
    return &context->sigmask;
}

void
os_restore_fp_control(os_context_t *context)
{
    dos_fldcw((unsigned short *)(context->fpu + 0));
}

os_context_register_t *
os_context_float_register_addr(os_context_t *context, int offset)
{
    return (os_context_register_t *)(context->fpu + 28 + 10 * offset);
}

void
os_flush_icache(os_vm_address_t address, os_vm_size_t length)
{
    (void)address; (void)length;
}
