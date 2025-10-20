此分支为第三次实验

代码在 whu-oslab-lab3/code 中

报告在 whu-oslab-lab3/report 中

## 代码组织结构

```
whu-oslab-lab3/code/
├── include/               # 头文件目录
│   ├── common.h           # 公共定义
│   ├── memlayout.h        # 内存布局定义（含 CLINT、PLIC、UART 地址）
│   ├── riscv.h            # RISC-V 相关定义（CSR 寄存器操作）
│   ├── dev/               # 设备相关头文件
│   │   ├── plic.h         # PLIC 中断控制器接口
│   │   ├── timer.h        # 定时器接口
│   │   └── uart.h         # UART 设备接口
│   ├── lib/               # 库相关头文件
│   │   ├── lock.h         # 锁相关定义
│   │   ├── print.h        # 打印相关定义
│   │   └── str.h          # 字符串相关定义
│   ├── mem/               # 内存管理头文件
│   │   ├── pmem.h         # 物理内存管理接口
│   │   └── vmem.h         # 虚拟内存管理接口
│   ├── proc/              # 进程管理头文件
│   │   └── proc.h         # 进程管理接口
│   └── trap/              # 中断处理头文件 ⭐新增
│       └── trap.h         # 中断处理接口
└── kernel/                # 内核源码目录
    ├── kernel.ld          # 内核链接脚本
    ├── Makefile           # 内核构建文件
    ├── boot/              # 引导相关代码
    │   ├── entry.S        # 内核入口汇编代码
    │   ├── main.c         # 内核主函数（初始化中断系统）⭐修改
    │   └── start.c        # M-mode 启动代码（初始化时钟中断）⭐修改
    ├── dev/               # 设备驱动 ⭐扩展
    │   ├── plic.c         # PLIC 中断控制器驱动实现
    │   ├── timer.c        # 定时器驱动实现（M-mode 和 S-mode）
    │   └── uart.c         # UART 串口驱动实现
    ├── lib/               # 内核库实现
    │   ├── print.c        # 打印功能实现
    │   ├── spinlock.c     # 自旋锁实现
    │   └── str.c          # 字符串操作实现
    ├── mem/               # 内存管理模块
    │   ├── pmem.c         # 物理内存分配器实现
    │   └── vmem.c         # 虚拟内存管理器实现
    ├── proc/              # 进程管理模块
    │   └── proc.c         # 进程管理实现
    └── trap/              # 中断处理模块 ⭐新增
        ├── trap.S         # 中断向量汇编代码（kernel_vector, timer_vector）
        └── trap_kernel.c  # 内核态中断处理实现
```

## 调试流程

### 使用 GDB 调试

1. 在一个终端启动 QEMU，并暂停等待 gdb 连接：

```sh
qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel \
    -m 128M -smp 2 -nographic -serial mon:stdio -S -gdb tcp::26000
```

2. 在另一终端启动 gdb：

```sh
riscv64-unknown-elf-gdb kernel/kernel
(gdb) target remote :26000
(gdb) b trap_kernel_handler
(gdb) b timer_interrupt_handler
(gdb) c
```

### 常用调试命令

```gdb
# 查看寄存器
(gdb) info registers

# 查看 CSR 寄存器
(gdb) p/x $stvec
(gdb) p/x $scause
(gdb) p/x $sepc

# 查看调用栈
(gdb) bt

# 单步执行
(gdb) si    # 单步（包括函数内部）
(gdb) ni    # 单步（跳过函数）
```

## 文件功能说明

### 关键实现文件

| 文件                          | 功能          | 关键函数                                                                                   |
| ----------------------------- | ------------- | ------------------------------------------------------------------------------------------ |
| `kernel/dev/timer.c`        | 时钟管理      | `timer_init()`, `timer_create()`, `timer_update()`                                   |
| `kernel/trap/trap_kernel.c` | 中断处理      | `trap_kernel_handler()`, `timer_interrupt_handler()`, `external_interrupt_handler()` |
| `kernel/trap/trap.S`        | 中断入口      | `kernel_vector`, `timer_vector`                                                        |
| `kernel/dev/plic.c`         | PLIC 驱动     | `plic_init()`, `plic_claim()`, `plic_complete()`                                     |
| `kernel/boot/start.c`       | M-mode 启动   | `start()` + `timer_init()`                                                             |
| `kernel/boot/main.c`        | S-mode 主函数 | `main()` + 中断系统初始化                                                                |
