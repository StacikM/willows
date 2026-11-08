/* GRUB (Multiboot 1) enters in 32-bit protected mode. We build identity page tables for 4GB,
   switch to long mode, enable SSE (Windows x64 code uses it everywhere) and call kmain in 64-bit. */
.set MB_MAGIC, 0x1BADB002
.set MB_FLAGS, 0x00000007          /* page-align modules, memory info, video mode */

.section .multiboot
.align 4
.long MB_MAGIC, MB_FLAGS, -(MB_MAGIC + MB_FLAGS)
.long 0, 0, 0, 0, 0
.long 0, 1280, 720, 32

.section .text
.code32
.global _start
_start:
    cli
    cld
    mov %eax, mb_magic
    mov %ebx, mb_info
    mov $stack_top, %esp

    mov $pdpt, %eax
    or  $3, %eax
    mov %eax, pml4                      /* pml4[0] -> pdpt */
    xor %ecx, %ecx
1:  mov %ecx, %eax
    shl $12, %eax
    add $pds, %eax
    or  $3, %eax
    mov %eax, pdpt(,%ecx,8)             /* pdpt[i] -> pd i (i = 0..3) */
    inc %ecx
    cmp $4, %ecx
    jne 1b
    xor %ecx, %ecx
2:  mov %ecx, %eax
    shl $21, %eax
    or  $0x83, %eax                     /* present, writable, 2MB page */
    mov %eax, pds(,%ecx,8)
    inc %ecx
    cmp $2048, %ecx
    jne 2b

    mov $1536, %ecx                     /* 3GB..4GB holds device memory (framebuffer, NIC): uncached */
3:  orl $0x18, pds(,%ecx,8)
    inc %ecx
    cmp $2048, %ecx
    jne 3b

    mov $pml4, %eax
    mov %eax, %cr3
    mov %cr4, %eax
    or  $0x20, %eax                     /* PAE */
    mov %eax, %cr4
    mov $0xC0000080, %ecx
    rdmsr
    or  $0x100, %eax                    /* EFER.LME */
    wrmsr
    mov %cr0, %eax
    or  $0x80000000, %eax               /* paging on -> long mode (compat) */
    mov %eax, %cr0
    lgdt gdt64_desc
    ljmp $0x08, $long_mode

.code64
long_mode:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    xor %ax, %ax
    mov %ax, %fs
    mov %ax, %gs
    lea stack_top(%rip), %rsp
    mov %cr0, %rax
    and $-5, %rax                       /* clear EM */
    or  $2, %rax                        /* set MP */
    mov %rax, %cr0
    mov %cr4, %rax
    or  $0x600, %rax                    /* OSFXSR | OSXMMEXCPT */
    mov %rax, %cr4
    fninit
    mov mb_magic(%rip), %edi
    mov mb_info(%rip), %esi
    call kmain
1:  cli
    hlt
    jmp 1b

/* void ctx_switch(u64 *save_rsp, u64 new_rsp): cooperative context switch (callee-saved registers on the stack) */
.global ctx_switch
ctx_switch:
    push %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15
    sub $160, %rsp                      /* xmm6-xmm15 are callee-saved in the Win64 ABI */
    movdqu %xmm6, 0(%rsp)
    movdqu %xmm7, 16(%rsp)
    movdqu %xmm8, 32(%rsp)
    movdqu %xmm9, 48(%rsp)
    movdqu %xmm10, 64(%rsp)
    movdqu %xmm11, 80(%rsp)
    movdqu %xmm12, 96(%rsp)
    movdqu %xmm13, 112(%rsp)
    movdqu %xmm14, 128(%rsp)
    movdqu %xmm15, 144(%rsp)
    mov %rsp, (%rdi)
    mov %rsi, %rsp
    movdqu 0(%rsp), %xmm6
    movdqu 16(%rsp), %xmm7
    movdqu 32(%rsp), %xmm8
    movdqu 48(%rsp), %xmm9
    movdqu 64(%rsp), %xmm10
    movdqu 80(%rsp), %xmm11
    movdqu 96(%rsp), %xmm12
    movdqu 112(%rsp), %xmm13
    movdqu 128(%rsp), %xmm14
    movdqu 144(%rsp), %xmm15
    add $160, %rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    pop %rbp
    ret

/* first activation of a new process: the initial stack "returns" here */
.global proc_start
proc_start:
    sti
    and $-16, %rsp
    call proc_main
1:  hlt
    jmp 1b

/* ---- exceptions ---- */
.macro ISR_NOERR n
isr\n: pushq $0
    pushq $\n
    jmp isr_common
.endm
.macro ISR_ERR n
isr\n: pushq $\n
    jmp isr_common
.endm
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

isr_common:
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15
    mov %rsp, %rdi
    cld
    call exception_handler
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax
    add $16, %rsp
    iretq

/* ---- IRQs: save caller-saved registers, call C handler, EOI ---- */
.macro IRQ name, handler, slave
.global \name
\name:
    push %rax
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %r8
    push %r9
    push %r10
    push %r11
    cld
    call \handler
    mov $0x20, %al
.if \slave
    out %al, $0xA0
.endif
    out %al, $0x20
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rax
    iretq
.endm
IRQ irq0_stub, timer_tick, 0
IRQ irq1_stub, keyboard_irq, 0
IRQ irq12_stub, mouse_irq, 1

.section .rodata
.global isr_table
.align 8
isr_table:
.irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    .quad isr\n
.endr

.section .data
.align 8
gdt64:
    .quad 0
    .quad 0x00AF9A000000FFFF           /* 0x08 64-bit code */
    .quad 0x00CF92000000FFFF           /* 0x10 data */
gdt64_desc:
    .word gdt64_desc - gdt64 - 1
    .long gdt64

.section .bss
.align 4096
.global pml4, pdpt
pml4: .skip 4096
pdpt: .skip 4096
pds:  .skip 16384
.align 16
mb_magic:  .skip 4
mb_info:   .skip 4
.skip 65536
stack_top:
