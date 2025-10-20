# 中断处理功能测试指南

## 编译和运行

### 1. 编译内核

```bash
cd /home/hwt/桌面/sources/lab/OSLab/WHUOS/whu-oslab-lab3/code
make clean
make
```

### 2. 运行内核 (QEMU)

```bash
make qemu
# 或者
qemu-system-riscv64 \
    -machine virt \
    -nographic \
    -bios none \
    -kernel kernel/kernel \
    -m 128M \
    -smp 2
```

## 预期行为

### 时钟中断测试

**预期输出**:

```
Timer interrupt: ticks = 1
Timer interrupt: ticks = 2
Timer interrupt: ticks = 3
Timer interrupt: ticks = 4
...
```

**说明**:

- 每约 0.1 秒 (INTERVAL=1000000 cycles) 打印一次
- ticks 值应该持续递增
- 这证明 M-mode → S-mode 的时钟中断转发正常工作

### UART 中断测试

**测试步骤**:

1. 在 QEMU 串口终端输入字符
2. 观察字符是否被回显

**预期行为**:

- 输入字符应该立即回显
- uart_intr() 应该被调用处理中断
- 如果有未知 IRQ，会打印警告信息

**测试命令**:

```
输入: hello
预期: hello (回显)
```

### 异常处理测试

**可能的异常触发**:

- 访问非法内存地址
- 执行非法指令
- 页错误

**预期输出**:

```
Exception in kernel at sepc=0x80001234: Illegal instruction
  stval = 0x0
panic: Unhandled exception in kernel mode
```

## 调试技巧

### 1. 查看时钟中断频率

```c
// 修改 INTERVAL 值测试不同频率
#define INTERVAL 1000000  // 约 0.1 秒
#define INTERVAL 10000000 // 约 1 秒
```

### 2. 添加调试输出

```c
// 在 trap_kernel_handler() 中添加
printk("trap: scause=0x%lx sepc=0x%lx\n", scause, sepc);
```

### 3. 检查中断是否使能

```c
// 在初始化后添加
printk("stvec=0x%lx\n", r_stvec());
printk("sie=0x%lx\n", r_sie());
printk("sstatus=0x%lx\n", r_sstatus());
```

## 常见问题排查

### 问题 1: 没有时钟中断输出

**可能原因**:

- timer_init() 未被调用
- stvec 未正确设置
- 中断未使能

**检查点**:

```c
// 确认 start.c 中调用了 timer_init()
// 确认 main.c 中调用了 trap_kernel_inithart()
// 确认中断已使能 (sie, sstatus)
```

### 问题 2: UART 输入无响应

**可能原因**:

- PLIC 未初始化
- UART 中断未使能
- plic_claim() 返回 0

**检查点**:

```c
// 确认调用了 plic_init() 和 plic_inithart()
// 检查 PLIC_SENABLE 是否设置了 UART_IRQ 位
```

### 问题 3: 系统崩溃或 panic

**可能原因**:

- 栈溢出
- 中断处理中死锁
- 寄存器保存/恢复错误

**检查点**:

```c
// 检查 trap.S 中的寄存器保存/恢复是否对称
// 检查锁的获取和释放是否配对
// 确认 sp 寄存器正确恢复
```

## 性能测试

### 测试中断处理延迟

```c
void timer_interrupt_handler()
{
    uint64 start = r_time();  // 读取当前时间
    w_sip(r_sip() & ~2);
    timer_update();
    uint64 end = r_time();
    printk("Timer interrupt latency: %d cycles\n", end - start);
}
```

### 测试中断嵌套

```c
// 在中断处理中临时开启中断 (谨慎!)
void timer_interrupt_handler()
{
    w_sip(r_sip() & ~2);
    intr_on();  // 允许中断嵌套
    timer_update();
    intr_off();
}
```

## 进阶测试

### 1. 多核时钟中断

- 运行 `make qemu CPUS=2`
- 观察两个 CPU 是否都产生时钟中断
- 检查 ticks 是否线程安全递增

### 2. 中断风暴测试

```c
// 快速输入大量字符
// 观察系统是否能正确处理
```

### 3. 压力测试

```c
// 同时产生时钟中断和 UART 中断
// 检查系统稳定性
```

## 参考输出示例

```
WHU OS Lab 3 - Interrupt Handling
Initializing devices...
UART initialized
PLIC initialized
Timer initialized
Trap initialized
Starting kernel...

Timer interrupt: ticks = 1
Timer interrupt: ticks = 2
Timer interrupt: ticks = 3
hello (用户输入)
hello (回显)
Timer interrupt: ticks = 4
world
world
Timer interrupt: ticks = 5
...
```

## 退出 QEMU

- 按 `Ctrl-A` 然后按 `X`
- 或者在另一个终端执行 `killall qemu-system-riscv64`
