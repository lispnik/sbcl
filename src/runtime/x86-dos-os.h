#ifndef _X86_DOS_OS_H
#define _X86_DOS_OS_H

#include <stdint.h>

/* The register context saved by the DPMI exception stub in
 * x86-dos-exc.asm.  The layout is known to that file. */
typedef struct os_context {
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t eip, eflags;
    uint32_t trapno, err, cr2;
    sigset_t sigmask;
    unsigned char fpu[108];     /* FNSAVE image */
    uint32_t pad;
} os_context_t;

typedef int os_context_register_t;

unsigned long os_context_fp_control(os_context_t *context);
#define RESTORE_FP_CONTROL_FROM_CONTEXT
void os_restore_fp_control(os_context_t *context);

#define OS_CONTEXT_PC(context) context->eip

#endif /* _X86_DOS_OS_H */
