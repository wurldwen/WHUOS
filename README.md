此分支为第二次实验（内存管理与分页）

代码在 whu-oslab-lab2/code 中

报告在 whu-oslab-lab2/report 中

## 代码组织结构

```
whu-oslab-lab2/code/
├── common.mk              # 公共构建配置
├── LICENSE                # 许可证文件
├── Makefile               # 主构建文件
├── README.md              # 代码说明
├── include/               # 头文件目录
│   ├── common.h           # 公共定义
│   ├── memlayout.h        # 内存布局定义
│   ├── riscv.h            # RISC-V 相关定义
│   ├── dev/               # 设备相关头文件
│   │   └── uart.h         # UART 设备头文件
│   ├── lib/               # 库相关头文件
│   │   ├── lock.h         # 锁相关定义
│   │   ├── print.h        # 打印相关定义
│   │   └── str.h          # 字符串相关定义
│   ├── mem/               # 内存管理头文件
│   │   ├── pmem.h         # 物理内存管理接口
│   │   └── vmem.h         # 虚拟内存管理接口
│   └── proc/              # 进程管理头文件
│       └── proc.h         # 进程管理接口
├── kernel/                # 内核源码目录
│   ├── kernel.ld          # 内核链接脚本
│   ├── Makefile           # 内核构建文件
│   ├── boot/              # 引导相关代码
│   │   ├── entry.S        # 内核入口汇编代码
│   │   ├── main.c         # 内核主函数
│   │   └── start.c        # 内核启动代码
│   ├── dev/               # 设备驱动
│   │   └── uart.c         # UART 串口驱动实现
│   ├── lib/               # 内核库实现
│   │   ├── print.c        # 打印功能实现
│   │   ├── spinlock.c     # 自旋锁实现
│   │   └── str.c          # 字符串操作实现
│   ├── mem/               # 内存管理模块
│   │   ├── pmem.c         # 物理内存分配器实现
│   │   └── vmem.c         # 虚拟内存管理器实现
│   └── proc/              # 进程管理模块
│       └── proc.c         # 进程管理实现
└── picture/               # 图片资源目录
```


## 调试与测试流程

1. 编译并生成镜像：

```sh
make clean
make build --directory=kernel
```

2. 在一个终端启动 QEMU，并暂停等待 gdb 连接（端口示例 26000）：

```sh
qemu-system-riscv64 -machine virt -bios none -kernel kernel-qemu -m 128M -smp 2 -nographic -serial mon:stdio -S -gdb tcp::26000
```

3. 在另一终端启动 gdb 并加载 ELF（带符号文件）：

```sh
gdb-multiarch kernel-qemu.elf
(gdb) target remote :26000
(gdb) b _entry
(gdb) c
```
