# kernel.ld 将_entry作为整个OS的起点放置到0x80000000处
# qemu会自动跳转到0x80000000处并开始执行
# 注意: 此时是M-mode

.section .text
.globl _entry
_entry:
        # CPU_stack 定义于start.c中
        # sp = CPU_stack + ((hartid + 1) * 4096)
        # 将sp置于当前CPU的内核栈的栈顶
        la sp, CPU_stack # sp此时为stack0初始基地址
        li a0, 4096
        csrr a1, mhartid # a1为当前CPU的id号
        addi a1, a1, 1
        mul a0, a0, a1
        add sp, sp, a0   # sp偏移后位堆栈指针初始值
        call start       # 跳转到start
spin:
        j spin           # 如果跳转失败则死循环