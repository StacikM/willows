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

/* first activation of a new thread (thread.c): same as proc_start */
.global thread_start
thread_start:
    sti
    and $-16, %rsp
    call thread_main
1:  hlt
    jmp 1b

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

/* ---- ctx_restore(CONTEXT *ctx): resume a thread exactly as a Windows CONTEXT (x64 layout) describes it - all general registers,
   rflags, rsp, rip and the SSE state. Used when a vectored exception handler answers "continue execution". Never returns. ---- */
.global ctx_restore
ctx_restore:
    fxrstor64 0x100(%rdi)
    movzwq 0x42(%rdi), %rax
    push %rax                    /* ss */
    pushq 0x98(%rdi)             /* rsp */
    movl 0x44(%rdi), %eax
    push %rax                    /* rflags */
    movzwq 0x38(%rdi), %rax
    push %rax                    /* cs */
    pushq 0xf8(%rdi)             /* rip */
    mov 0x78(%rdi), %rax
    mov 0x80(%rdi), %rcx
    mov 0x88(%rdi), %rdx
    mov 0x90(%rdi), %rbx
    mov 0xa0(%rdi), %rbp
    mov 0xa8(%rdi), %rsi
    mov 0xb8(%rdi), %r8
    mov 0xc0(%rdi), %r9
    mov 0xc8(%rdi), %r10
    mov 0xd0(%rdi), %r11
    mov 0xd8(%rdi), %r12
    mov 0xe0(%rdi), %r13
    mov 0xe8(%rdi), %r14
    mov 0xf0(%rdi), %r15
    mov 0xb0(%rdi), %rdi
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
/* The timer: as the IRQ stubs above, plus preemption. When it interrupts a program's own code (not the kernel's) after the
   thread's time slice, timer_preempt switches to another thread (schedule); this thread continues here later. The interrupted
   code may have anything live, so the whole FPU/SSE state is kept too (fxsave), on this thread's own stack. */
.global irq0_stub
irq0_stub:
    push %rax
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %r8
    push %r9
    push %r10
    push %r11
    push %rbp
    mov %rsp, %rbp
    and $-16, %rsp
    sub $512, %rsp
    fxsave64 (%rsp)
    cld
    call timer_tick
    mov $0x20, %al
    out %al, $0x20                      /* EOI first: the next thread must get timer interrupts */
    mov 80(%rbp), %rdi                  /* interrupted rip */
    mov 88(%rbp), %rsi                  /* interrupted cs */
    call timer_preempt
    cli                                 /* resumed (or not switched): finish with interrupts off until iretq */
    fxrstor64 (%rsp)
    mov %rbp, %rsp
    pop %rbp
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
IRQ irq1_stub, keyboard_irq, 0
IRQ irq12_stub, mouse_irq, 1

/* ---------------------------------------------------------------------------------------------------------------
   32-bit (WOW64-style) support. A 32-bit Windows program runs in compatibility mode (CS=0x18) on its own stack below 4 GB.
   Its imports point at small stubs ("push <index>; jmp wow_common32"). wow_common32 jumps into 64-bit code, wow_thunk64 turns the
   32-bit stdcall arguments into a Win64 call of the real implementation (wow_table[index]), then returns to 32-bit mode and pops the
   arguments (stdcall). wow_enter starts the program; when its entry function returns, wow_ret32 brings control back.
   --------------------------------------------------------------------------------------------------------------- */
.section .text
.code32
.global wow_common32
wow_common32:
    ljmp $0x08, $wow_thunk64
.global wow_ret32
wow_ret32:                          /* the program's entry returned: [esp] = pointer to the saved-rsp slot, eax = exit value */
    movl (%esp), %ecx
    ljmp $0x08, $wow_back64
.code64
wow_thunk64:                        /* stack: [esp] = table index (4 bytes), then the 32-bit return address, then the arguments */
    mov %esp, %esp                  /* zero-extend the 32-bit stack pointer */
    movl (%rsp), %eax
    add $4, %rsp
    mov wow_pos(%rip), %ecx         /* remember the last 32 calls in a ring (shown when the program crashes or hangs) */
    lea wow_ring(%rip), %r10
    mov %eax, (%r10,%rcx,4)
    inc %ecx
    and $31, %ecx
    mov %ecx, wow_pos(%rip)
    shl $4, %rax
    lea wow_table(%rip), %r11
    add %rax, %r11
    push %r12
    push %r13
    lea 16(%rsp), %r12              /* r12 -> the 32-bit return address, arguments follow */
    movl 8(%r11), %r13d             /* number of 32-bit argument slots */
    mov (%r11), %r10                /* the Win64 function */
    sub $128, %rsp
    and $-16, %rsp
    movl 4(%r12), %ecx              /* first four arguments in registers, the rest on the stack (zero-extended) */
    movl 8(%r12), %edx
    movl 12(%r12), %r8d
    movl 16(%r12), %r9d
    movl 20(%r12), %eax
    mov %rax, 32(%rsp)
    movl 24(%r12), %eax
    mov %rax, 40(%rsp)
    movl 28(%r12), %eax
    mov %rax, 48(%rsp)
    movl 32(%r12), %eax
    mov %rax, 56(%rsp)
    movl 36(%r12), %eax
    mov %rax, 64(%rsp)
    movl 40(%r12), %eax
    mov %rax, 72(%rsp)
    movl 44(%r12), %eax
    mov %rax, 80(%rsp)
    movl 48(%r12), %eax
    mov %rax, 88(%rsp)
    call *%r10
    mov %rax, (%rsp)
    mov %rsi, 96(%rsp)              /* esi/edi belong to the 32-bit program, but win32_set_context (System V) is free to clobber them */
    mov %rdi, 104(%rsp)
    mov cur(%rip), %rdi             /* the call may have switched processes: reload this process's GS and FS */
    call win32_set_context
    mov 96(%rsp), %rsi
    mov 104(%rsp), %rdi
    mov (%rsp), %rax
    movl (%r12), %ecx               /* 32-bit return address */
    lea 4(%r12,%r13,4), %rdx        /* esp after the callee pops its arguments (stdcall) */
    mov -8(%r12), %r10              /* saved r12 */
    mov -16(%r12), %r11             /* saved r13 */
    lea -16(%rdx), %rsp             /* free stack below the final esp: build the far-return frame there */
    mov %rcx, (%rsp)
    movq $0x18, 8(%rsp)
    mov %r10, %r12
    mov %r11, %r13
    lretq                           /* back to 32-bit mode, eax = result */
.comm wow_ring, 128, 8
.comm wow_pos, 4, 4
.global wow_enter
wow_enter:                          /* void wow_enter(u64 entry32, u64 esp32, u64 *save_slot): returns the program's eax */
    push %rbx
    push %rbp
    push %r12
    push %r13
    push %r14
    push %r15
    mov %rsp, (%rdx)
    mov %esi, %esi
    lea -8(%rsi), %rsi              /* 32-bit stack: [esp] = wow_ret32, [esp+4] = the save slot */
    movl $wow_ret32, (%rsi)
    mov %edx, 4(%rsi)
    mov $0x20, %ax
    mov %ax, %fs                    /* FS base = this program's TEB */
    mov %rsi, %rsp
    sub $16, %rsp
    mov %rdi, (%rsp)
    movq $0x18, 8(%rsp)
    xor %eax, %eax
    lretq
wow_back64:                         /* entered from wow_ret32 in 64-bit mode: rcx = save slot, eax = result */
    mov %ecx, %ecx
    mov %eax, %eax
    mov (%rcx), %rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbp
    pop %rbx
    ret

.section .rodata
.global isr_table
.align 8
isr_table:
.irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    .quad isr\n
.endr

.section .data
.align 8
.global gdt64
gdt64:
    .quad 0
    .quad 0x00AF9A000000FFFF           /* 0x08 64-bit code */
    .quad 0x00CF92000000FFFF           /* 0x10 data */
    .quad 0x00CF9A000000FFFF           /* 0x18 32-bit code (compatibility mode: 32-bit Windows programs) */
    .quad 0x00CF92000000FFFF           /* 0x20 data whose base is the running 32-bit program's TEB (loaded into FS) */
.global gdt64_tss
gdt64_tss:
    .quad 0, 0                         /* 0x28 the TSS (16-byte system descriptor, filled in by idt.c): page faults run on their own stack */
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
