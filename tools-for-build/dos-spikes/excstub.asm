; DPMI exception -> C handler bridge (spike for the SBCL DOS port).
; Stub runs on the host's locked stack: saves a full context into a static slot,
; rewrites the exception frame to resume at a trampoline on the interrupted stack,
; and returns to the host with RETF.  The trampoline calls C with the context;
; dos_resume() reloads a (possibly edited) context and continues with IRETD.
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
%define CTX_FPU 56
%define CTX_SIZE 168
%define NSLOTS 8

segment _DATA public align=4 class=DATA use32
global _exc_ds, _exc_depth, _exc_slots
_exc_ds:    dd 0
_exc_depth: dd 0
_exc_slots: times CTX_SIZE*NSLOTS db 0
segment _TEXT public align=16 class=CODE use32
group DGROUP _DATA
extern _dos_exception_dispatch

%macro EXC 1
global _exc_entry_%1
_exc_entry_%1:
    push dword %1
    jmp short exc_common
%endmacro
EXC 1
EXC 3
EXC 6
EXC 13
EXC 14
EXC 16

exc_common:
    ; [esp]=trapno +4 ret eip +8 ret cs +12 err +16 eip +20 cs +24 eflags +28 esp +32 ss
    push eax
    mov ax, [cs:_exc_ds]
    mov ds, ax
    mov es, ax
    mov eax, [_exc_depth]
    inc dword [_exc_depth]
    imul eax, eax, CTX_SIZE
    add eax, _exc_slots
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
    mov ecx, cr2                    ; emulated by HDPMI
.nocr2:
    mov [eax+CTX_CR2], ecx
    mov dword [esp+12], exc_trampoline
    and dword [esp+20], ~0x100      ; clear TF
    mov ecx, [eax+CTX_ESP]
    sub ecx, 64
    and ecx, ~15
    mov [esp+24], ecx
    mov ebx, eax                    ; ctx slot travels in EBX
    retf

exc_trampoline:
    fnsave [ebx+CTX_FPU]
    cld
    push ebx
    call _dos_exception_dispatch    ; never returns
    hlt

; void dos_resume(struct ctx *)  -- never returns
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

; ---- test helpers ----
; int trap_int3(void): int3 + code byte; handler must skip the byte and set ECX
global _trap_int3, _trap_ud2, _call_on_stack, _single_step, _get_regs_after
_trap_int3:
    push ebx
    push esi
    push edi
    push ebp
    mov ebx, 0x11111111
    mov esi, 0x22222222
    mov edi, 0x33333333
    mov ebp, 0x44444444
    xor ecx, ecx
    int3
    db 0x5A
    mov eax, ecx
    cmp ebx, 0x11111111
    jne .bad
    cmp esi, 0x22222222
    jne .bad
    cmp edi, 0x33333333
    jne .bad
    cmp ebp, 0x44444444
    jne .bad
    jmp .ok
.bad: mov eax, -1
.ok:
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
_trap_ud2:
    xor ecx, ecx
    ud2
    db 0x7B
    mov eax, ecx
    ret
; int call_on_stack(int (*fn)(void), void *newsp)
_call_on_stack:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    mov esp, [ebp+12]
    call eax
    mov esp, ebp
    pop ebp
    ret
; int single_step(void): set TF, run 3 nops; handler counts steps
_single_step:
    pushfd
    or dword [esp], 0x100
    popfd
    nop
    nop
    nop
    pushfd
    and dword [esp], ~0x100
    popfd
    ret
