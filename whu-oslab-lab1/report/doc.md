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

### spinlock.c

待补充

### print.c

待补充

### proc.c

待补充

## 成功启动

修改好spinlock，print，proc后首先输出

```bash
hwt@WP:~/sources/lab/OSLab/WHUOS/whu-oslab-lab1/code$ make qemu
make build --directory=kernel
make[1]: 进入目录“/home/hwt/sources/lab/OSLab/WHUOS/whu-oslab-lab1/code/kernel”
make build --directory=boot/
make[2]: 进入目录“/home/hwt/sources/lab/OSLab/WHUOS/whu-oslab-lab1/code/kernel/boot”
make[2]: “build”已是最新。
make[2]: 离开目录“/home/hwt/sources/lab/OSLab/WHUOS/whu-oslab-lab1/code/kernel/boot”
make build --directory=dev/
make[2]: 进入目录“/home/hwt/sources/lab/OSLab/WHUOS/whu-oslab-lab1/code/kernel/dev”
make[2]: “build”已是最新。
make[2]: 离开目录“/home/hwt/sources/lab/OSLab/WHUOS/whu-oslab-lab1/code/kernel/dev”
make build --directory=lib/
make[2]: 进入目录“/home/hwt/sources/lab/OSLab/WHUOS/whu-oslab-lab1/code/kernel/lib”
riscv64-linux-gnu-gcc -Wall -Werror -O -fno-omit-frame-pointer -ggdb -gdwarf-2 -MD -mcmodel=medany -ffreestanding -fno-common -nostdlib -mno-relax -I. -fno-stack-protector -fno-pie -no-pie -I ../../include -c spinlock.c
make[2]: 离开目录“/home/hwt/sources/lab/OSLab/WHUOS/whu-oslab-lab1/code/kernel/lib”
make build --directory=proc/
make[2]: 进入目录“/home/hwt/sources/lab/OSLab/WHUOS/whu-oslab-lab1/code/kernel/proc”
riscv64-linux-gnu-gcc -Wall -Werror -O -fno-omit-frame-pointer -ggdb -gdwarf-2 -MD -mcmodel=medany -ffreestanding -fno-common -nostdlib -mno-relax -I. -fno-stack-protector -fno-pie -no-pie -I ../../include -c proc.c
make[2]: 离开目录“/home/hwt/sources/lab/OSLab/WHUOS/whu-oslab-lab1/code/kernel/proc”
ls: 无法访问 './*/*/*.o': 没有那个文件或目录
riscv64-linux-gnu-ld -z max-page-size=4096 -T kernel.ld -o ../kernel-qemu ./boot/entry.o ./boot/main.o ./boot/start.o ./dev/uart.o ./lib/print.o ./lib/spinlock.o ./proc/proc.o
riscv64-linux-gnu-ld: 警告: 无法找到项目符号 _entry; 缺省为 0000000080000000
make[1]: 离开目录“/home/hwt/sources/lab/OSLab/WHUOS/whu-oslab-lab1/code/kernel”
qemu-system-riscv64 -machine virt -bios none -kernel kernel-qemu  -m 128M -smp 2 -nographic
QEMU: Terminated
```

输出中的主要问题是链接器警告“无法找到项目符号 _entry”，这是因为在 [entry.S](vscode-file://vscode-app/usr/share/code/resources/app/out/vs/code/electron-browser/workbench/workbench.html) 中 `_entry` 符号没有使用 `.globl` 导出，导致链接器无法识别入口点。

在 [entry.S](vscode-file://vscode-app/usr/share/code/resources/app/out/vs/code/electron-browser/workbench/workbench.html) 中添加了 `.globl _entry`符号可以正确导出，链接器应该能够找到入口点。

至于 `ls` 命令的错误，它在查找 `.o` 文件时失败，但这不影响编译和链接，因为链接命令直接指定了文件路径。

![1758528308558](image/doc/1758528308558.png)

修改main.c

**正确做法：**

- 用 `kernel-qemu.elf` 作为gdb的调试目标（它是ELF格式，包含符号）。
- 用 `kernel-qemu` 作为QEMU的启动镜像。

**操作步骤：**

1. 启动QEMU（用裸机镜像）：
   ```
   qemu-system-riscv64 -machine virt -bios none -kernel kernel-qemu -m 128M -smp 2 -nographic -serial mon:stdio -S -gdb tcp::26000
   ```
2. 启动gdb（用ELF文件）：
   ```
   gdb-multiarch kernel-qemu.elf
   ```
3. 在gdb中连接QEMU：
   ```
   (gdb) target remote :26000
   (gdb) b main
   (gdb) c
   ```

```
 x/10i $pc
```

```
riscv64-linux-gnu-objdump -d kernel-qemu.elf 
```
