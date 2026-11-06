.set ALIGN,    1<<0
.set MEMINFO,  1<<1
.set FLAGS,    ALIGN | MEMINFO
.set MAGIC,    0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)

.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

.section .bss
.align 16
stack_bottom:
.skip 16384 /* 16 KB Kernel Stack */
stack_top:

.section .text
.global _start
.type _start, @function
_start:
    /* Set up stack */
    movl $stack_top, %esp

    /* Reset EFLAGS */
    pushl $0
    popf

    /* Push Multiboot parameters (mbi in ebx, magic in eax) */
    pushl %ebx
    pushl %eax

    /* Call kernel entry point */
    call kernel_main

    /* If kernel returns, halt CPU */
    cli
1:  hlt
    jmp 1b

.size _start, . - _start
