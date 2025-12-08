此分支为总实验

代码在 whu-oslab-lab/code 中

报告在 whu-oslab-lab/report 中

## 代码组织结构

```
whu-oslab-lab7/code/
├── include/               # 头文件目录
│   ├── common.h           # 公共定义
│   ├── memlayout.h        # 内存布局定义（含 CLINT、PLIC、UART、VirtIO 地址）
│   ├── riscv.h            # RISC-V 相关定义（CSR 寄存器操作）
│   ├── dev/               # 设备相关头文件
│   │   ├── plic.h         # PLIC 中断控制器接口
│   │   ├── timer.h        # 定时器接口
│   │   ├── uart.h         # UART 设备接口
│   │   ├── virtio.h       # VirtIO 块设备接口
│   │   └── vio.h          # VirtIO 辅助定义
│   ├── fs/                # 文件系统相关头文件
│   │   ├── bitmap.h       # 位图管理接口
│   │   ├── buf.h          # 缓冲区管理接口
│   │   ├── dir.h          # 目录操作接口
│   │   ├── elf.h          # ELF 文件格式定义
│   │   ├── file.h         # 文件操作接口
│   │   ├── fs.h           # 文件系统接口
│   │   └── inode.h        # inode 管理接口
│   ├── lib/               # 库相关头文件
│   │   ├── lock.h         # 锁相关定义
│   │   ├── print.h        # 打印相关定义
│   │   └── str.h          # 字符串相关定义
│   ├── mem/               # 内存管理头文件
│   │   ├── pmem.h         # 物理内存管理接口
│   │   ├── kvm.h          # 内核虚拟内存接口
│   │   ├── uvm.h          # 用户虚拟内存接口
│   │   └── mmap.h         # 内存映射接口
│   ├── proc/              # 进程管理头文件
│   │   ├── cpu.h          # CPU 相关定义
│   │   ├── proc.h         # 进程管理接口
│   │   └── initcode.h     # 初始进程代码（生成）
│   ├── syscall/           # 系统调用相关头文件
│   │   ├── syscall.h      # 系统调用接口
│   │   ├── sysfunc.h      # 系统调用函数声明
│   │   └── sysnum.h       # 系统调用号定义
│   └── trap/              # 中断处理头文件 
│       └── trap.h         # 中断处理接口
├── kernel/                # 内核源码目录
│   ├── kernel.ld          # 内核链接脚本
│   ├── Makefile           # 内核构建文件
│   ├── boot/              # 引导相关代码
│   │   ├── entry.S        # 内核入口汇编代码
│   │   ├── main.c         # 内核主函数（初始化系统、文件系统）
│   │   └── start.c        # M-mode 启动代码（初始化时钟中断）
│   ├── dev/               # 设备驱动 
│   │   ├── plic.c         # PLIC 中断控制器驱动实现
│   │   ├── timer.c        # 定时器驱动实现（M-mode 和 S-mode）
│   │   ├── uart.c         # UART 串口驱动实现
│   │   └── virtio.c       # VirtIO 块设备驱动实现
│   ├── fs/                # 文件系统模块
│   │   ├── bitmap.c       # 位图管理实现（inode/数据块分配）
│   │   ├── buf.c          # 缓冲区管理实现（块缓存）
│   │   ├── dir.c          # 目录操作实现（路径解析、目录项管理）
│   │   ├── file.c         # 文件操作实现
│   │   ├── fs.c           # 文件系统初始化和测试
│   │   └── inode.c        # inode 管理实现（创建、读写、锁定）
│   ├── lib/               # 内核库实现
│   │   ├── print.c        # 打印功能实现
│   │   ├── spinlock.c     # 自旋锁实现
│   │   └── str.c          # 字符串操作实现
│   ├── mem/               # 内存管理模块
│   │   ├── pmem.c         # 物理内存分配器实现
│   │   ├── kvm.c          # 内核虚拟内存管理实现
│   │   ├── uvm.c          # 用户虚拟内存管理实现
│   │   └── mmap.c         # 内存映射实现
│   ├── proc/              # 进程管理模块
│   │   ├── cpu.c          # CPU 相关实现
│   │   ├── proc.c         # 进程管理实现（调度、fork、exec）
│   │   └── swtch.S        # 上下文切换汇编代码
│   ├── syscall/           # 系统调用模块
│   │   ├── syscall.c      # 系统调用分发实现
│   │   ├── sysfile.c      # 文件相关系统调用实现
│   │   ├── sysfunc.c      # 其他系统调用实现
│   │   └── sysproc.c      # 进程相关系统调用实现（exec、fork 等）
│   └── trap/              # 中断处理模块 
│       ├── trap.S         # 中断向量汇编代码（kernel_vector）
│       ├── trampoline.S   # 用户态/内核态切换跳板代码
│       ├── trap_kernel.c  # 内核态中断处理实现
│       └── trap_user.c    # 用户态中断处理实现
├── mkfs/                  # 文件系统镜像生成工具
│   └── mkfs.c             # 创建 fs.img 的工具程序
├── testcode/              # 测试代码目录
│   ├── 2路径测试.c        # 路径解析测试
│   └── 3目录测试.c        # 目录操作测试
└── user/                  # 用户态程序目录
    ├── initcode.c         # 初始用户进程代码
    ├── initcode.out       # 编译后的初始进程
    ├── Makefile           # 用户程序构建文件
    ├── syscall_arch.h     # 系统调用架构相关定义
    ├── syscall_num.h      # 系统调用号定义
    └── sys.h              # 系统调用接口
```

## 调试流程

### 使用 GDB 调试

1. 在一个终端启动 QEMU，并暂停等待 gdb 连接：

```sh
cd /home/hwt/桌面/sources/lab/OSLab/WHUOS/whu-oslab-lab7/code
make qemu-gdb
```

2. 在另一终端启动 gdb：

```sh
gdb-multiarch kernel-qemu.elf -ex "target remote :26000" 
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
