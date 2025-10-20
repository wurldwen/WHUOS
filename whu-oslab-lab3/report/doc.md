# 中断处理完整流程

## 详细流程图

```
┌─────────────────────────────────────────────────────────────┐
│ 1. 程序正常执行阶段                                          │
└─────────────────────────────────────────────────────────────┘
用户程序执行 / 内核代码执行
    ↓
┌─────────────────────────────────────────────────────────────┐
│ 2. 中断/异常触发                                             │
└─────────────────────────────────────────────────────────────┘
发生中断/异常（硬件事件）
    │
    ├─ 时钟中断（CLINT 定时器到期）
    ├─ 外设中断（UART 数据到达、磁盘完成等）
    ├─ 系统调用（ecall 指令）
    └─ 异常（页错误、非法指令等）
    ↓
硬件自动操作：
    ├─ 保存当前 PC 到 sepc（Supervisor Exception PC）
    ├─ 保存中断原因到 scause（Supervisor Cause）
    ├─ 保存附加信息到 stval（Supervisor Trap Value）
    ├─ 更新 sstatus 寄存器（SPP=之前的模式，SIE=0 关闭中断）
    └─ 跳转到 stvec 寄存器指向的地址（kernel_vector）
    ↓
┌─────────────────────────────────────────────────────────────┐
│ 3. 汇编入口：保存上下文                                      │
└─────────────────────────────────────────────────────────────┘
[trap/trap.S] kernel_vector:
    ├─ 在内核栈上分配空间（256字节）
    │   addi sp, sp, -256
    │
    ├─ 保存所有通用寄存器（x0-x31）
    │   sd ra, 0(sp)      # x1: 返回地址
    │   sd sp, 8(sp)      # x2: 栈指针
    │   sd gp, 16(sp)     # x3: 全局指针
    │   sd tp, 24(sp)     # x4: 线程指针
    │   sd t0, 32(sp)     # x5: 临时寄存器
    │   ...
    │   sd t6, 248(sp)    # x31
    │
    ├─ 保存浮点寄存器（如果使能）
    │   fsd f0, 256(sp)
    │   ...
    │
    └─ 调用 C 语言处理函数
        call trap_kernel_handler
    ↓
┌─────────────────────────────────────────────────────────────┐
│ 4. C语言处理：判断中断类型                                   │
└─────────────────────────────────────────────────────────────┘
[trap/trap_kernel.c] trap_kernel_handler():
    │
    ├─ 读取关键 CSR 寄存器
    │   uint64 sepc = r_sepc();      // 异常发生时的 PC
    │   uint64 sstatus = r_sstatus(); // 状态寄存器
    │   uint64 scause = r_scause();   // 中断/异常原因
    │   uint64 stval = r_stval();     // 附加信息
    │
    ├─ 安全性检查
    │   assert(sstatus & SSTATUS_SPP);  // 确认来自 S-mode
    │   assert(intr_get() == 0);        // 确认中断已关闭
    │
    ├─ 解析 scause 寄存器
    │   int is_interrupt = scause & (1UL << 63);  // 最高位
    │   int trap_id = scause & 0xf;               // 低4位
    │
    └─ 分发到具体处理函数
        ↓
┌─────────────────────────────────────────────────────────────┐
│ 5. 中断类型分发                                              │
└─────────────────────────────────────────────────────────────┘
        if (is_interrupt) {
            switch (trap_id) {
                case 1: // S-mode 软件中断
                    ├─ 进程间通信/软中断处理
        
                case 5: // S-mode 时钟中断 ───────────────────┐
                    └─ timer_interrupt_handler()              │
                        ↓                                     │
                ┌───────────────────────────────────────┐     │
                │ 5.1 时钟中断处理                      │     │
                └───────────────────────────────────────┘     │
                [dev/timer.c] timer_interrupt_handler():      │
                    │                                         │
                    ├─ [riscv.h] w_sip() 清除挂起的时钟中断    │
                    │   w_sip(r_sip() & ~SIP_SSIP);          │
                    │                                         │
                    ├─ [dev/timer.c] timer_tick()            │
                    │   ├─ 增加系统滴答计数                   │
                    │   │   jiffies++;                        │
                    │   │                                     │
                    │   ├─ 设置下次时钟中断                   │
                    │   │   *(uint64*)CLINT_MTIMECMP(hartid) │
                    │   │       = *(uint64*)CLINT_MTIME       │
                    │   │       + interval;                   │
                    │   │                                     │
                    │   └─ 更新进程时间片                     │
                    │       [proc/proc.c]                     │
                    │       current->counter--;               │
                    │                                         │
                    └─ 判断是否需要调度                       │
                        if (current->counter <= 0) {          │
                            [proc/proc.c] schedule();         │
                            ├─ 选择下一个进程                 │
                            └─ 切换进程上下文                 │
                        }                                     │
                        ↓                                     │
                                                              │
                case 9: // S-mode 外设中断 ───────────────────┤
                    └─ external_interrupt_handler()           │
                        ↓                                     │
                ┌───────────────────────────────────────┐     │
                │ 5.2 外设中断处理                      │     │
                └───────────────────────────────────────┘     │
                [trap/trap_kernel.c]                          │
                external_interrupt_handler():                 │
                    │                                         │
                    ├─ [dev/plic.c] plic_claim()             │
                    │   ├─ 读取 PLIC 的 claim 寄存器          │
                    │   │   int irq = *(uint32*)PLIC_SCLAIM(hart);
                    │   └─ 返回中断请求号（IRQ）              │
                    │       return irq;                       │
                    │                                         │
                    ├─ 根据 IRQ 分发到设备驱动                │
                    │   switch (irq) {                        │
                    │       case UART0_IRQ: ─────────────┐    │
                    │           uart_intr();             │    │
                    │           break;                   │    │
                    │       case VIRTIO0_IRQ:            │    │
                    │           disk_intr();             │    │
                    │           break;                   │    │
                    │   }                                │    │
                    │                                    │    │
                    └─ [dev/plic.c] plic_complete(irq)  │    │
                        ├─ 通知 PLIC 中断已处理          │    │
                        └─ 写入 complete 寄存器          │    │
                            *(uint32*)PLIC_SCLAIM(hart)=irq; │
                        ↓                                │    │
                ┌────────────────────────────────────┐  │    │
                │ 5.2.1 UART 中断处理                │  │    │
                └────────────────────────────────────┘  │    │
                [dev/uart.c] uart_intr(): <─────────────┘    │
                    │                                         │
                    ├─ 读取 UART 状态寄存器                   │
                    │   uint8 lsr = *(uint8*)(UART0 + UART_LSR);
                    │                                         │
                    ├─ 检查是否有数据可读                     │
                    │   if (lsr & UART_LSR_RX) {              │
                    │       while (有数据) {                   │
                    │           int c = uart_getc_sync();     │
                    │           [lib/console.c]               │
                    │           console_intr(c);              │
                    │           ├─ 处理特殊字符（回车、退格）  │
                    │           ├─ 回显字符                   │
                    │           └─ 放入输入缓冲区             │
                    │               cons.buf[cons.w++] = c;   │
                    │       }                                 │
                    │   }                                     │
                    │                                         │
                    └─ 检查是否可以发送                       │
                        if (lsr & UART_LSR_TX) {              │
                            [lib/console.c]                   │
                            console_putc(); // 继续发送输出   │
                        }                                     │
                        ↓                                     │
                                                              │
                default: // 未知中断                          │
                    panic("unknown interrupt");               │
            }                                                 │
        } else { // 异常处理 ──────────────────────────────────┘
            ↓
        ┌───────────────────────────────────────┐
        │ 5.3 异常处理                          │
        └───────────────────────────────────────┘
            switch (trap_id) {
                case 8: // 来自 U-mode 的 ecall（系统调用）
                    [trap/syscall.c] syscall();
                    ├─ 读取系统调用号（a7 寄存器）
                    ├─ 读取参数（a0-a5）
                    ├─ 分发到具体系统调用
                    │   ├─ sys_write()
                    │   ├─ sys_read()
                    │   ├─ sys_fork()
                    │   └─ ...
                    ├─ 设置返回值（a0）
                    └─ sepc += 4 (跳过 ecall 指令)
        
                case 12: // 指令页错误
                    [mm/vm.c] page_fault_handler();
                    ├─ 读取出错地址（stval）
                    ├─ 检查是否合法访问
                    ├─ 分配物理页面
                    └─ 更新页表
        
                case 13: // 加载页错误
                case 15: // 存储页错误
                    [mm/vm.c] page_fault_handler();
                    // 同上
        
                case 2: // 非法指令
                    printk("Illegal instruction at 0x%lx\n", sepc);
                    [proc/proc.c] exit(-1);
        
                case 5: // 加载访问错误
                case 7: // 存储访问错误
                    printk("Access fault at 0x%lx\n", stval);
                    [proc/proc.c] exit(-1);
        
                default:
                    printk("Unknown exception: %s\n", 
                           exception_info[trap_id]);
                    panic("unhandled exception");
            }
        }
        ↓
┌─────────────────────────────────────────────────────────────┐
│ 6. 返回汇编：恢复上下文                                      │
└─────────────────────────────────────────────────────────────┘
[trap/trap.S] kernel_vector (继续):
    │
    ├─ 恢复所有通用寄存器
    │   ld ra, 0(sp)
    │   ld sp, 8(sp)
    │   ld gp, 16(sp)
    │   ...
    │   ld t6, 248(sp)
    │
    ├─ 恢复浮点寄存器（如果使能）
    │   fld f0, 256(sp)
    │   ...
    │
    ├─ 释放栈空间
    │   addi sp, sp, 256
    │
    └─ 返回到被中断的代码
        sret  // 硬件自动：
              // ├─ PC = sepc
              // ├─ 特权级 = sstatus.SPP
              // ├─ sstatus.SIE = sstatus.SPIE
              // └─ sstatus.SPP = U-mode
    ↓
┌─────────────────────────────────────────────────────────────┐
│ 7. 恢复执行                                                  │
└─────────────────────────────────────────────────────────────┘
程序从 sepc 指向的地址继续执行
```

## 关键数据结构

### 1. scause 寄存器格式

```
63        62-0
┌─┬───────────┐
│I│ Exception │
│ │   Code    │
└─┴───────────┘

I=1: 中断
I=0: 异常

中断代码：
  1: S-mode 软件中断
  5: S-mode 时钟中断
  9: S-mode 外设中断

异常代码：
  0: 指令地址不对齐
  2: 非法指令
  8: U-mode 系统调用
 12: 指令页错误
 13: 加载页错误
 15: 存储页错误
```

### 2. trapframe 结构

```c
// proc/proc.h
struct trapframe {
    uint64 kernel_satp;   // 内核页表
    uint64 kernel_sp;     // 内核栈
    uint64 kernel_trap;   // trap handler 地址
    uint64 epc;           // 用户 PC
    uint64 kernel_hartid; // CPU ID
    uint64 ra;            // x1
    uint64 sp;            // x2
    uint64 gp;            // x3
    uint64 tp;            // x4
    uint64 t0;            // x5
    uint64 t1;            // x6
    uint64 t2;            // x7
    uint64 s0;            // x8
    uint64 s1;            // x9
    uint64 a0;            // x10
    uint64 a1;            // x11
    uint64 a2;            // x12
    uint64 a3;            // x13
    uint64 a4;            // x14
    uint64 a5;            // x15
    uint64 a6;            // x16
    uint64 a7;            // x17
    uint64 s2;            // x18
    uint64 s3;            // x19
    uint64 s4;            // x20
    uint64 s5;            // x21
    uint64 s6;            // x22
    uint64 s7;            // x23
    uint64 s8;            // x24
    uint64 s9;            // x25
    uint64 s10;           // x26
    uint64 s11;           // x27
    uint64 t3;            // x28
    uint64 t4;            // x29
    uint64 t5;            // x30
    uint64 t6;            // x31
};
```

## 涉及的关键文件

### trap 目录

- **`trap/trap.S`**: 汇编入口，保存/恢复寄存器
- **`trap/trap_kernel.c`**: 内核态中断处理核心逻辑
- **`trap/trap.h`**: 相关宏定义和函数声明

### dev 目录

- **`dev/timer.c`**: CLINT 定时器驱动
- **`dev/plic.c`**: PLIC 中断控制器驱动
- **`dev/uart.c`**: UART 串口驱动
- **`dev/disk.c`**: 磁盘驱动（virtio）

## 初始化流程

```c
// kernel/main.c
void main() {
    // 1. 初始化设备
    uart_init();           // [dev/uart.c]
    plic_init();           // [dev/plic.c]
    timer_init();          // [dev/timer.c]
  
    // 2. 初始化 trap 系统
    trap_kernel_init();    // [trap/trap_kernel.c]
  
    // 3. 每个 CPU 核心初始化
    trap_kernel_inithart();
    // 内部调用: w_stvec((uint64)kernel_vector);
  
    // 4. 使能中断
    plic_inithart();       // 使能 PLIC
    timer_inithart();      // 设置第一次时钟中断
    intr_on();             // 开启中断
}
```
