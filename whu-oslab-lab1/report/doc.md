## 实验报告一

### 启动

#### entry.S

```
# kernel.ld 将_entry作为整个OS的起点放置到0x80000000处
# qemu会自动跳转到0x80000000处并开始执行
# 注意: 此时是M-mode

.section .text
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
```

kernel.ld中可以看出_entry位于地址0x80000000，即qemu启动后执行内核代码的初始位置，该位置放置的代码使用户自定义内核的第一行代码。

stack0的定义位于start.c中，按16字节对齐

```c++
//start.c

//为每一个核设置初始启动时的C语言栈帧空间
__attribute__ ((aligned (16))) uint8 CPU_stack[4096 * NCPU];
//aligned(16)限制对齐
```

### start.c

通过mret指令将内核从M-Mode引入Supervisor-Mode并执行main函数。

mret 指令执行时几步操作如下：

- 0：把 s e p c sepcsepc 的内容放入 P C PCPC ，其为 m a i n mainmain 函数的地址。
- 1：切换特权级到 S − M o d e S-ModeS−Mode ，这一步主要是把 m s t a t u s mstatusmstatus 寄存器中 M P P MPPMPP 位设置为当前模式。代码中已经设置了 m s t a t u s mstatusmstatus 寄存器 M P P MPPMPP 为 S − M o d e S-ModeS−Mode 。
- 2：把 m s t a t u s mstatusmstatus 寄存器中 M I E MIEMIE 位设置为 M P I E MPIEMPIE 。M P P MPPMPP 置0，M P I E MPIEMPIE 置1。(M P I E MPIEMPIE 位用于在陷入 M − M o d e M-ModeM−Mode 时记录进入之前的中断使能与否)
