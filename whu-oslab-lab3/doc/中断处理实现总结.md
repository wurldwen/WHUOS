# 中断处理实现总结

## 概述

本次实现完成了 RISC-V 操作系统内核中的时钟中断和外设中断处理功能。

## 已实现的文件和函数

### 1. kernel/dev/timer.c - 时钟管理

#### `timer_init()` - M-mode 时钟初始化

- **功能**: 在 M-mode 下初始化时钟中断
- **实现要点**:
  - 获取当前 CPU 的 hartid
  - 设置第一次时钟中断时间: `CLINT_MTIMECMP(hartid) = CLINT_MTIME + INTERVAL`
  - 准备 mscratch 数组供 timer_vector 使用:
    - mscratch[0..2]: 保存寄存器的临时空间
    - mscratch[3]: CLINT_MTIMECMP(hartid) 地址
    - mscratch[4]: 时钟中断间隔 INTERVAL
  - 设置 M-mode trap 向量为 timer_vector
  - 使能 M-mode 中断和时钟中断

#### `timer_create()` - 创建系统时钟

- **功能**: 初始化 S-mode 系统时钟
- **实现要点**:
  - 初始化 sys_timer 结构体
  - 将 ticks 设为 0
  - 初始化自旋锁 "timer"

#### `timer_update()` - 更新时钟

- **功能**: 增加系统滴答计数
- **实现要点**:
  - 使用自旋锁保护
  - sys_timer.ticks++

#### `timer_get_ticks()` - 获取时钟滴答数

- **功能**: 线程安全地读取当前 ticks
- **实现要点**:
  - 使用自旋锁保护读取操作
  - 返回当前 ticks 值

---

### 2. kernel/trap/trap_kernel.c - 内核态中断处理

#### `trap_kernel_init()` - 初始化全局 trap 资源

- **功能**: 初始化内核 trap 系统的全局资源
- **实现要点**:
  - 调用 timer_create() 初始化系统时钟

#### `trap_kernel_inithart()` - 每个 CPU 核心的 trap 初始化

- **功能**: 为每个 CPU 核心设置 trap 处理
- **实现要点**:
  - 设置 stvec 寄存器指向 kernel_vector
  - 这样所有 S-mode 的 trap 都会跳转到 kernel_vector

#### `trap_kernel_handler()` - 核心中断/异常处理逻辑

- **功能**: 分发和处理所有内核态的中断和异常
- **实现要点**:
  - 读取 CSR 寄存器: sepc, sstatus, scause, stval
  - 安全检查: 确认来自 S-mode 且中断已关闭
  - 根据 scause 最高位判断是中断还是异常
  - **中断处理** (scause[63] = 1):
    - Case 1: S-mode 软件中断 → 调用 timer_interrupt_handler()
    - Case 5: S-mode 时钟中断 → 打印信息
    - Case 9: S-mode 外设中断 → 调用 external_interrupt_handler()
    - Default: 打印未知中断信息
  - **异常处理** (scause[63] = 0):
    - 打印异常信息和 sepc, stval
    - 调用 panic() 终止系统

#### `timer_interrupt_handler()` - 时钟中断处理

- **功能**: 处理由 M-mode 转发的时钟中断
- **实现要点**:
  - 清除 S-mode 软件中断挂起位: `w_sip(r_sip() & ~2)`
  - 调用 timer_update() 增加 ticks
  - 打印当前 ticks 值

#### `external_interrupt_handler()` - 外设中断处理

- **功能**: 处理 PLIC 管理的外部设备中断
- **实现要点**:
  - 调用 plic_claim() 获取中断号
  - 根据 IRQ 分发:
    - UART_IRQ: 调用 uart_intr()
    - 其他: 打印未知中断信息
  - 调用 plic_complete(irq) 通知 PLIC 中断处理完成

---

## 中断处理流程

### 时钟中断流程

```
硬件: CLINT 时钟到期
    ↓
M-mode: timer_vector (trap.S)
    ├─ 保存寄存器 a0-a3 到 mscratch
    ├─ 更新 CLINT_MTIMECMP += INTERVAL (设置下次中断)
    ├─ 触发 S-mode 软件中断: w_sip(2)
    ├─ 恢复寄存器
    └─ mret 返回
    ↓
S-mode: kernel_vector (trap.S)
    ├─ 保存所有寄存器到栈
    ├─ 调用 trap_kernel_handler()
    │   ├─ 读取 scause = 0x8000000000000001 (S-mode 软件中断)
    │   └─ 调用 timer_interrupt_handler()
    │       ├─ 清除 SIP.SSIP 位
    │       ├─ timer_update() (ticks++)
    │       └─ 打印 ticks
    ├─ 恢复所有寄存器
    └─ sret 返回
```

### 外设中断流程 (以 UART 为例)

```
硬件: UART 数据到达
    ↓
PLIC: 产生中断请求
    ↓
S-mode: kernel_vector (trap.S)
    ├─ 保存所有寄存器到栈
    ├─ 调用 trap_kernel_handler()
    │   ├─ 读取 scause = 0x8000000000000009 (S-mode 外设中断)
    │   └─ 调用 external_interrupt_handler()
    │       ├─ plic_claim() 获取 IRQ = UART_IRQ
    │       ├─ uart_intr() 处理 UART 中断
    │       │   └─ 读取输入并回显
    │       └─ plic_complete(irq) 通知完成
    ├─ 恢复所有寄存器
    └─ sret 返回
```

---

## 关键数据结构

### timer_t (dev/timer.h)

```c
typedef struct timer {
    uint64 ticks;      // 时钟滴答计数
    spinlock_t lk;     // 保护 ticks 的自旋锁
} timer_t;
```

### mscratch 数组 (timer.c)

```c
static uint64 mscratch[NCPU][5];
// [hartid][0..2]: 临时保存 a1, a2, a3
// [hartid][3]: CLINT_MTIMECMP(hartid) 地址
// [hartid][4]: INTERVAL 值
```

---

## 关键寄存器

### scause 寄存器格式

```
63        62-0
┌─┬───────────┐
│I│ Exception │
│ │   Code    │
└─┴───────────┘

I=1: 中断
I=0: 异常

常用中断代码:
  1: S-mode 软件中断 (M-mode 时钟中断转发)
  5: S-mode 时钟中断
  9: S-mode 外设中断

常用异常代码:
  2: 非法指令
  8: 系统调用 (ecall)
 12: 指令页错误
 13: 加载页错误
 15: 存储页错误
```

---

## 内存映射地址 (memlayout.h)

### CLINT (Core Local Interruptor)

- `CLINT_BASE = 0x2000000`
- `CLINT_MTIMECMP(hartid) = CLINT_BASE + 0x4000 + 8*hartid`
- `CLINT_MTIME = CLINT_BASE + 0xBFF8`

### PLIC (Platform-Level Interrupt Controller)

- `PLIC_BASE = 0x0c000000`
- `PLIC_PRIORITY(id) = PLIC_BASE + id*4`
- `PLIC_SENABLE(hart) = PLIC_BASE + 0x2080 + hart*0x100`
- `PLIC_SPRIORITY(hart) = PLIC_BASE + 0x201000 + hart*0x2000`
- `PLIC_SCLAIM(hart) = PLIC_BASE + 0x201004 + hart*0x2000`

### UART

- `UART_BASE = 0x10000000`
- `UART_IRQ = 10`

---

## 编译验证

所有实现的文件都通过了编译检查，没有语法错误或类型错误。

## 测试建议

1. **时钟中断测试**:

   - 启动内核后应该看到定期打印的时钟中断信息
   - 每约 0.1 秒 (INTERVAL=1000000) 打印一次
2. **UART 中断测试**:

   - 在串口输入字符
   - 应该看到字符被回显
   - uart_intr() 会读取输入并输出
3. **异常测试**:

   - 触发非法指令等异常
   - 应该看到详细的异常信息和 panic

---

## 相关文件清单

### 实现的文件

- `/kernel/dev/timer.c` - 时钟管理实现
- `/kernel/trap/trap_kernel.c` - 内核态 trap 处理实现

### 已存在的支持文件

- `/kernel/trap/trap.S` - 汇编入口 (kernel_vector, timer_vector)
- `/kernel/dev/plic.c` - PLIC 驱动 (已实现)
- `/kernel/dev/uart.c` - UART 驱动 (已实现)

### 头文件

- `/include/dev/timer.h` - 时钟接口
- `/include/trap/trap.h` - trap 接口
- `/include/riscv.h` - CSR 寄存器操作
- `/include/memlayout.h` - 内存映射定义
