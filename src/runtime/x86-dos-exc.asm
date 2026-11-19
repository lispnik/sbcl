; DPMI exception -> POSIX-style signal bridge for the DOS port of SBCL.
;
; This software is part of the SBCL system. See the README file for
; more information.
;
; A DPMI host calls a client's exception handler on a locked stack of its
; own, with interrupts disabled, and expects a far return.  Nothing useful
; can be done there (no DOS calls, no unwinding), so the stubs below only:
;   1. save the full register context into a static slot,
;   2. rewrite the exception frame so that the "faulting" code resumes at
;      dos_exc_trampoline, on the interrupted stack,
;   3. return to the host.
; The trampoline then runs in the ordinary client context and calls
; dos_exception_dispatch() (dos-os.c), which delivers the "signal" to the
; runtime's handlers with an os_context_t and finally calls dos_resume()
; to reload the possibly modified context.
;
; Assemble with: nasm -f obj
;
; The context layout must match os_context_t in x86-dos-os.h.
%define CTX_EAX 0
%define CTX_ECX 4
%define CTX_EDX 8
%define CTX_EBX 12
%define CTX_ESP 16
%define CTX_EBP 20
%define CTX_ESI 24
%define CTX_EDI 28
%define CTX_EIP 32
%define CTX_EFL 36
%define CTX_TRAP 40
%define CTX_ERR 44
%define CTX_CR2 48
%define CTX_MASK 52
%define CTX_FPU 56
%define CTX_SIZE 168
%define NSLOTS 8

segment _DATA public align=4 class=DATA use32
global _dos_exc_ds, _dos_exc_depth
_dos_exc_ds:    dd 0            ; flat data selector, set by dos-os.c
_dos_exc_depth: dd 0            ; number of context slots in use
exc_slots:      times CTX_SIZE*NSLOTS db 0

; Ctrl-Break (see dos_tick_entry below)
global _dos_tick_old, _dos_break_pending, _dos_break_chord, _dos_break_sigbit
extern _dos_sigmask
_dos_tick_old:      dd 0            ; the timer interrupt's previous handler:
                    dd 0            ;   offset, then selector
_dos_break_pending: dd 0            ; an interrupt has been asked for
_dos_break_chord:   dd 1            ; Ctrl-Alt-Shift asks for one too
_dos_break_sigbit:  dd 0            ; SIGINT's bit in a signal mask
break_chord_was:    dd 0
break_inflight:     dd 0            ; dos_break_entry has yet to pick these up:
break_eip:          dd 0
break_eax:          dd 0
break_esp:          dd 0

segment _TEXT public align=16 class=CODE use32
group DGROUP _DATA
extern _dos_exception_dispatch

%macro EXC 1
global _dos_exc_entry_%1
_dos_exc_entry_%1:
    push dword %1
    jmp exc_common
%endmacro
EXC 0
EXC 1
EXC 3
EXC 4
EXC 5
EXC 6
EXC 12
EXC 13
EXC 14
EXC 16

exc_common:
    ; [esp]=trapno +4 ret eip +8 ret cs +12 err +16 eip +20 cs +24 eflags +28 esp +32 ss
    push eax
    mov ax, [cs:_dos_exc_ds]
    mov ds, ax
    mov es, ax
    mov eax, [_dos_exc_depth]
    cmp eax, NSLOTS
    jae .overflow
    inc dword [_dos_exc_depth]
    imul eax, eax, CTX_SIZE
    add eax, exc_slots
    pop dword [eax+CTX_EAX]
    pop dword [eax+CTX_TRAP]
    mov [eax+CTX_ECX], ecx
    mov [eax+CTX_EDX], edx
    mov [eax+CTX_EBX], ebx
    mov [eax+CTX_EBP], ebp
    mov [eax+CTX_ESI], esi
    mov [eax+CTX_EDI], edi
    ; [esp]=ret eip +4 ret cs +8 err +12 eip +16 cs +20 eflags +24 esp +28 ss
    mov ecx, [esp+8]
    mov [eax+CTX_ERR], ecx
    mov ecx, [esp+12]
    mov [eax+CTX_EIP], ecx
    mov ecx, [esp+20]
    mov [eax+CTX_EFL], ecx
    mov ecx, [esp+24]
    mov [eax+CTX_ESP], ecx
    xor ecx, ecx
    cmp dword [eax+CTX_TRAP], 14
    jne .nocr2
    mov ecx, cr2                    ; privileged, but emulated by the DPMI host
.nocr2:
    mov [eax+CTX_CR2], ecx
    mov dword [esp+12], dos_exc_trampoline
    and dword [esp+20], ~0x100      ; clear TF
    mov ecx, [eax+CTX_ESP]
    sub ecx, 64
    and ecx, ~15
    mov [esp+24], ecx
    mov ebx, eax                    ; the context slot travels in EBX
    retf
.overflow:
    ; Exceptions nested deeper than we have slots for: the handlers
    ; themselves are faulting.  Give up.
    mov ax, 0x4CFE
    int 0x21

dos_exc_trampoline:
    fnsave [ebx+CTX_FPU]            ; also leaves the FPU in its initial state
    cld
    push ebx
    call _dos_exception_dispatch    ; never returns
    hlt

; Ctrl-Break: interrupting Lisp that is running.
;
; Nothing in a loop that makes no calls will ever look at the keyboard, so
; the interrupt has to come from outside it.  The timer interrupt (18.2
; times a second) is handled here first.  When an interrupt has been asked
; for -- the BIOS has seen Ctrl-Break and set its flag at 0040:0071, or
; Ctrl, Alt and Shift are all down, or DOS has seen Ctrl-C (dos_int23_entry)
; -- and the timer has interrupted our own code, the address that code will
; resume at is changed to dos_break_entry, which delivers SIGINT as the
; exception stub above delivers a trap: the context it saves resumes the
; interrupted code exactly, should the handler return.
;
; HDPMI calls the handler on its locked stack with, above the IRETD frame,
; the address the interrupted program resumes at (found by looking; it is
; not in the DPMI specification).  That is only our code when its selector
; is ours and the frame is the first thing on that stack; in every other
; case (the timer interrupted DOS, the BIOS, or the host) the request stays
; pending for a later tick.  It also stays pending while SIGINT is blocked.
global _dos_tick_entry
_dos_tick_entry:
    push eax
    push ds
    mov ax, [cs:_dos_exc_ds]
    mov ds, ax
    test byte [0x471], 0x80         ; the BIOS's Ctrl-Break flag
    jz .chord
    and byte [0x471], 0x7F
    mov dword [_dos_break_pending], 1
.chord:
    cmp dword [_dos_break_chord], 0
    je .nochord
    mov al, [0x417]                 ; shift states: right, left, Ctrl, Alt
    mov ah, al
    and ah, 0x0C
    cmp ah, 0x0C
    jne .nochord
    test al, 0x03
    jz .nochord
    cmp dword [break_chord_was], 0  ; once for each time it is pressed
    jne .deliver
    mov dword [break_chord_was], 1
    mov dword [_dos_break_pending], 1
    jmp .deliver
.nochord:
    mov dword [break_chord_was], 0
.deliver:
    cmp dword [_dos_break_pending], 0
    je .chain
    cmp dword [break_inflight], 0
    jne .chain
    mov eax, [_dos_sigmask]
    test eax, [_dos_break_sigbit]
    jnz .chain
    ; [esp] ds, eax, the IRETD frame (EIP CS EFLAGS), then the program's EIP and CS
    mov eax, ss
    lsl eax, eax
    jnz .chain
    inc eax
    sub eax, esp
    cmp eax, 28
    jne .chain
    mov eax, cs
    cmp ax, [esp+24]
    jne .chain
    mov eax, [esp+20]
    mov [break_eip], eax
    mov dword [esp+20], dos_break_entry
    mov dword [break_inflight], 1
    mov dword [_dos_break_pending], 0
.chain:
    pop ds
    pop eax
    jmp far [cs:_dos_tick_old]

; Reached in place of the interrupted instruction, with every register and
; flag as that instruction would have found them.  Nothing is pushed until
; the stack pointer has been moved clear of the interrupted code's stack.
dos_break_entry:
    mov [break_eax], eax
    mov [break_esp], esp
    lea esp, [esp-64]
    pushfd
    mov eax, [_dos_exc_depth]
    cmp eax, NSLOTS
    jae .full
    inc dword [_dos_exc_depth]
    imul eax, eax, CTX_SIZE
    add eax, exc_slots
    pop dword [eax+CTX_EFL]
    mov [eax+CTX_ECX], ecx
    mov [eax+CTX_EDX], edx
    mov [eax+CTX_EBX], ebx
    mov [eax+CTX_EBP], ebp
    mov [eax+CTX_ESI], esi
    mov [eax+CTX_EDI], edi
    mov ecx, [break_eax]
    mov [eax+CTX_EAX], ecx
    mov ecx, [break_esp]
    mov [eax+CTX_ESP], ecx
    mov ecx, [break_eip]
    mov [eax+CTX_EIP], ecx
    mov dword [eax+CTX_TRAP], 0x100 ; not a processor exception: Ctrl-Break
    mov dword [eax+CTX_ERR], 0
    mov dword [eax+CTX_CR2], 0
    mov dword [break_inflight], 0
    mov ebx, eax
    and esp, ~15
    jmp dos_exc_trampoline
.full:                              ; no room to deliver it: carry on
    popfd
    mov esp, [break_esp]
    mov eax, [break_eax]
    mov dword [break_inflight], 0
    jmp [break_eip]

; DOS has seen Ctrl-C, and would end the program.  Returning with IRETD
; tells it to carry on instead; the interrupt is delivered by the timer.
global _dos_int23_entry
_dos_int23_entry:
    push eax
    push ds
    mov ax, [cs:_dos_exc_ds]
    mov ds, ax
    mov dword [_dos_break_pending], 1
    pop ds
    pop eax
    iretd

; void dos_resume(os_context_t *)  -- never returns
global _dos_resume
_dos_resume:
    mov eax, [esp+4]
    frstor [eax+CTX_FPU]
    mov ecx, [eax+CTX_ESP]
    sub ecx, 12
    mov edx, [eax+CTX_EIP]
    mov [ecx], edx
    xor edx, edx
    mov dx, cs
    mov [ecx+4], edx
    mov edx, [eax+CTX_EFL]
    mov [ecx+8], edx
    mov edx, [eax+CTX_EDX]
    mov ebx, [eax+CTX_EBX]
    mov ebp, [eax+CTX_EBP]
    mov esi, [eax+CTX_ESI]
    mov edi, [eax+CTX_EDI]
    mov esp, ecx
    mov ecx, [eax+CTX_ECX]
    mov eax, [eax+CTX_EAX]
    iretd

; void dos_fldcw(unsigned short *cw)
global _dos_fldcw
_dos_fldcw:
    mov eax, [esp+4]
    fldcw [eax]
    ret

; void dos_capture_context(os_context_t *): fill in a context describing
; the caller, for "signals" that are delivered synchronously (raise()).
global _dos_capture_context
_dos_capture_context:
    mov eax, [esp+4]
    mov [eax+CTX_ECX], ecx
    mov [eax+CTX_EDX], edx
    mov [eax+CTX_EBX], ebx
    mov [eax+CTX_EBP], ebp
    mov [eax+CTX_ESI], esi
    mov [eax+CTX_EDI], edi
    mov dword [eax+CTX_EAX], 0
    mov ecx, [esp]
    mov [eax+CTX_EIP], ecx
    lea ecx, [esp+4]
    mov [eax+CTX_ESP], ecx
    pushfd
    pop dword [eax+CTX_EFL]
    fnsave [eax+CTX_FPU]
    frstor [eax+CTX_FPU]
    mov ecx, [eax+CTX_ECX]
    ret
