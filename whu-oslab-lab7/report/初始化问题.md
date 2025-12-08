# 文件系统初始化问题的发现与解决过程

## 问题背景

在实现 Lab 7 时，需要集成文件系统并实现 `sys_exec` 系统调用。在测试过程中，系统在文件系统初始化阶段陷入死循环，无法正常启动。

## 问题现象

系统启动后的表现：
- VirtIO 磁盘设备初始化成功（状态 0xf）
- 文件系统初始化时调用 `buf_read()` 读取超级块
- VirtIO 发送磁盘读取请求后，进程进入睡眠状态
- **系统完全挂起**，进程永远无法被唤醒
- 只有定时器中断在运行，没有任何 VirtIO 中断

典型的日志输出：
```
[VIRTIO_INIT] Device status after DRIVER_OK: f
[VIRTIO_INIT] Interrupt status: 0
[VIRTIO_RW] Notifying device: avail[1]=1, idx[0]=0
[VIRTIO_RW] Waiting for disk, b=0x0000000080020e18
[SLEEP] pid=1 sleeping on 0x0000000080020e18
[WAKEUP] waking up processes on 0x000000008000c070, caller=0x00000000800002a4
[WAKEUP] waking up processes on 0x000000008000c070, caller=0x00000000800002a4
...（无限重复）
```

关键观察：
- 所有 `wakeup` 调用都来自 `caller=0x800002a4`（定时器中断）
- **从未出现 VirtIO 中断相关的日志**
- 进程在缓冲区 `0x80020e18` 上睡眠，永远不会被唤醒

## 问题排查过程

### 第一阶段：初始化顺序问题

**问题发现：**
最初遇到的错误是在 `main.c` 中直接调用 `fs_init()` 时，系统报错：
```
panic: sleep without process context
```

**原因分析：**
- `fs_init()` 需要调用 `buf_read()`，而 `buf_read()` 会调用 `sleep()`
- `sleep()` 需要访问当前进程 `myproc()`，但此时还没有创建任何进程
- 在 `main()` 中调用 `fs_init()` 时，`myproc()` 返回 `NULL`

**解决方案：**
参考 xv6 的实现模式，将文件系统初始化延迟到第一个进程上下文中执行：

1. 在 `proc.c` 中创建 `proc_forkret()` 函数：
```c
void proc_forkret() {
    static int first = 1;
    spinlock_release(&myproc()->lk);  // 释放调度器持有的锁
    if (first) {
        first = 0;
        fs_init();  // 在进程上下文中初始化文件系统
    }
    trap_user_return();
}
```

2. 修改 `proc_make_first()`，让第一个进程返回到 `proc_forkret`：
```c
p->ctx.ra = (uint64)proc_forkret;
```

3. 从 `main.c` 中移除 `fs_init()` 调用

**结果：**
这解决了进程上下文问题，但系统仍然挂起在 VirtIO 磁盘 I/O 上。

### 第二阶段：调度器中断使能问题

**问题发现：**
通过添加日志发现，系统能够进入调度器，但设备中断似乎被屏蔽了。

**原因分析：**
调度器主循环在遍历进程时，中断可能被禁用，导致设备中断无法及时处理。

**解决方案：**
在调度器主循环开始时显式启用中断：
```c
void proc_scheduler() {
    printf("[SCHEDULER] CPU%d starting scheduler\n", mycpuid());
    for(;;) {
        intr_on();  // 确保设备可以中断调度器
        for(p = procs; p < &procs[NPROC]; p++) {
            // ...
        }
    }
}
```

**结果：**
定时器中断开始正常工作（通过 WAKEUP 日志确认），但 VirtIO 中断仍然不触发。

### 第三阶段：VirtIO 中断处理逻辑问题

**问题发现：**
通过对比参考实现（oslab-main）发现，VirtIO 中断处理函数存在几个问题。

**问题 1：`used` 结构体字段名错误**

在 `include/dev/virtio.h` 中：
```c
struct UsedArea {
    uint16 flags;
    uint16 id;  // 错误！应该是 idx
    struct VRingUsedElem elems[NUM];
};
```

而在 `virtio_disk_intr()` 中尝试访问 `disk.used->idx`，导致编译错误。

**修复：**
```c
struct UsedArea {
    uint16 flags;
    uint16 idx;  // 修正：设备在添加条目时会递增此字段
    struct VRingUsedElem elems[NUM];
};
```

**问题 2：中断确认时机错误**

原始代码在处理完所有请求后才确认中断：
```c
void virtio_disk_intr() {
    // ... 处理请求 ...
    *R(VIRTIO_MMIO_INTERRUPT_ACK) = ...;  // 最后才确认
}
```

**修复：**
参考 xv6，应该**先确认中断**，再处理请求：
```c
void virtio_disk_intr() {
    spinlock_acquire(&disk.vdisk_lock);
    
    // 首先确认中断
    *R(VIRTIO_MMIO_INTERRUPT_ACK) = *R(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;
    __sync_synchronize();
    
    // 然后处理完成的请求
    while (disk.used_idx != disk.used->idx) {
        // ...
    }
    
    spinlock_release(&disk.vdisk_lock);
}
```

**问题 3：缺少内存屏障**

在访问共享内存（descriptor ring）时，需要确保内存操作的顺序性。

**修复：**
在关键位置添加 `__sync_synchronize()`：
```c
while (disk.used_idx != disk.used->idx) {
    __sync_synchronize();  // 确保读取最新的 used ring 内容
    int id = disk.used->elems[disk.used_idx % NUM].id;
    // ...
}
```

**结果：**
这些修复仍然没有解决根本问题——VirtIO 中断根本没有触发。

### 第四阶段：PLIC 配置问题（根本原因）

**关键发现：**

通过添加调试日志发现，虽然外部中断被启用（SIE 寄存器为 0x222），但从未收到任何 VirtIO 中断：
```
[VIRTIO_INIT] SIE register: 0x0000000000000222
[EXT_INTR] Got external interrupt, irq=10  // UART 中断
[EXT_INTR] Got external interrupt, irq=10
...（只有 UART 中断，从未出现 IRQ 1）
```

**深入分析：**

检查 PLIC（Platform-Level Interrupt Controller）的初始化代码：

在 `dev/plic.c` 中有两个函数：
```c
void plic_init()      // 设置中断优先级
void plic_inithart()  // 使能当前 CPU 核心的中断
```

但在 `main.c` 中只调用了设备初始化，**从未调用 PLIC 的初始化函数**！

对比参考实现（oslab-main）：
```c
// ref_project/oslab-main/src/boot/main.c
plicinit();          // 设置中断控制器
plicinithart();      // 向PLIC请求设备中断
```

**根本原因：**

`plic_inithart()` 负责配置 PLIC，使其能够将设备中断路由到当前 CPU：
```c
void plic_inithart() {   
    int hartid = mycpuid();
    // 使能中断开关（包括 UART 和 VIRTIO）
    *(uint32*)PLIC_SENABLE(hartid) = (1 << UART_IRQ) | (1 << VIRTIO_IRQ);
    // 设置响应阈值
    *(uint32*)PLIC_SPRIORITY(hartid) = 0;
}
```

没有调用这个函数，意味着：
- PLIC 知道中断优先级（通过 `plic_init()` 设置）
- 但不知道应该把中断发送给哪个 CPU
- VirtIO 设备产生中断信号，但 PLIC 不会转发给 CPU
- 只有 UART 中断工作（可能是默认配置）

**最终解决方案：**

在 `kernel/boot/main.c` 中添加 PLIC 初始化：
```c
int main() {
    int cpuid = r_tp();
    if(cpuid == 0) {
        // ... 其他初始化 ...
        
        // 初始化内核trap系统
        trap_kernel_init();
        trap_kernel_inithart();
        
        // 初始化PLIC (Platform-Level Interrupt Controller)
        plic_init();        // 设置中断优先级
        plic_inithart();    // 使能当前CPU的中断路由
        
        // 初始化VirtIO磁盘设备
        virtio_disk_init();
        
        // ...
    }
}
```

## 验证结果

修复后的系统行为：
```
[VIRTIO_INIT] Device status after DRIVER_OK: f
[SCHEDULER] CPU0 starting scheduler
[SCHEDULER] CPU0 found RUNNABLE pid=1
[VIRTIO_RW] Notifying device
File system initialized successfully!
```

关键变化：
- VirtIO 中断正常触发
- 进程能够从磁盘 I/O 等待中被唤醒
- 文件系统成功初始化
- 系统正常运行

## 问题总结

### 根本原因
**遗漏了 PLIC 的 per-hart 初始化（`plic_inithart()`）**，导致中断控制器无法将 VirtIO 设备的中断路由到 CPU。

### 解决的三个层次问题

1. **进程上下文问题**（初级）
   - 症状：`panic: sleep without process context`
   - 原因：在 `main()` 中调用需要 `sleep()` 的函数
   - 解决：将 `fs_init()` 延迟到 `proc_forkret()` 中执行

2. **调度器中断使能问题**（中级）
   - 症状：定时器中断不工作
   - 原因：调度器循环中中断被禁用
   - 解决：在调度器循环开始时调用 `intr_on()`

3. **PLIC 配置缺失问题**（核心）
   - 症状：VirtIO 中断永远不触发
   - 原因：`plic_inithart()` 从未被调用
   - 解决：在 `main()` 中添加 `plic_init()` 和 `plic_inithart()` 调用

### 调试技巧

1. **追踪调用来源**
   - 通过记录 `ra` 寄存器识别函数调用者
   - 发现所有 `wakeup` 都来自定时器，而非 VirtIO

2. **对比参考实现**
   - 仔细对比 oslab-main 和 xv6 的初始化流程
   - 发现遗漏的关键初始化步骤

3. **分层诊断**
   - 从外到内：中断使能（SIE） → PLIC 配置 → 设备初始化
   - 从下到上：设备 → 中断控制器 → CPU → 软件处理

4. **日志驱动调试**
   - 在关键路径添加详细日志
   - 通过日志缺失发现代码路径未执行

## 经验教训

1. **硬件初始化的完整性**
   - 不仅要初始化设备本身，还要配置中断路由
   - PLIC 需要全局初始化（`plic_init`）和 per-hart 初始化（`plic_inithart`）

2. **参考实现的价值**
   - 当遇到底层硬件问题时，参考实现是最可靠的指南
   - 不仅要看实现逻辑，更要看初始化顺序

3. **中断处理的关键点**
   - 中断确认时机很重要（先确认再处理）
   - 内存屏障对于共享内存同步至关重要
   - 中断使能是分层的（CPU → PLIC → 设备）

4. **系统调试的方法论**
   - 从现象出发，逐层排查
   - 利用日志追踪控制流
   - 对比工作的参考实现找出差异
