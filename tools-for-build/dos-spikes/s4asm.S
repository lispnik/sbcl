/* S4: GNU as (ELF) object linked by wlink with Watcom C */
    .text
    .globl _asm_add64, _asm_call_c, _asm_read_data
_asm_add64:                     /* long long asm_add64(long long, long long) */
    movl 4(%esp), %eax
    movl 8(%esp), %edx
    addl 12(%esp), %eax
    adcl 16(%esp), %edx
    ret
_asm_call_c:                    /* double asm_call_c(double x): returns c_twice(x)+1, checks callee-saved */
    pushl %ebx
    pushl %esi
    pushl %edi
    pushl %ebp
    movl $0x11111111, %ebx
    movl $0x22222222, %esi
    movl $0x33333333, %edi
    movl $0x44444444, %ebp
    fldl 20(%esp)
    subl $8, %esp
    fstpl (%esp)
    call _c_twice
    addl $8, %esp
    fld1
    faddp
    cmpl $0x11111111, %ebx
    jne 1f
    cmpl $0x22222222, %esi
    jne 1f
    cmpl $0x33333333, %edi
    jne 1f
    cmpl $0x44444444, %ebp
    jne 1f
    incl _c_counter
    jmp 2f
1:  movl $-1, _c_counter
2:  popl %ebp
    popl %edi
    popl %esi
    popl %ebx
    ret
_asm_read_data:
    movl asm_private, %eax
    addl _c_counter, %eax
    ret
    .data
asm_private: .long 0x1000
