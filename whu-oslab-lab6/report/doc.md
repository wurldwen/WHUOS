# 实验六：进程管理 实验报告

## 一、实验目标

本实验在实验五的基础上，实现完整的进程管理系统，包括：

1. 完善进程体定义，实现进程的多种状态转换
2. 实现 fork、exit、wait 等核心系统调用
3. 实现进程调度器（时间片轮转算法）
4. 实现 sleep 和 wakeup 机制

## 二、实验内容

### 2.1 任务一：实现 fork + exit + wait

#### 2.1.1 进程结构体定义

完善了进程结构体 `proc_t`，包含以下关键字段：

```c
typedef struct proc {
    spinlock_t lk;              // 自旋锁
    int pid;                    // 进程标识符
    enum proc_state state;      // 进程状态
    struct proc* parent;        // 父进程指针
    int exit_state;            // 退出状态
    void* sleep_space;         // 睡眠等待的资源
    pgtbl_t pgtbl;             // 用户态页表
    uint64 heap_top;           // 堆顶地址
    uint64 ustack_base;        // 用户栈基地址
    uint64 ustack_pages;       // 用户栈页数
    mmap_region_t* mmap;       // mmap区域链表
    trapframe_t* tf;           // 陷阱帧
    uint64 kstack;             // 内核栈地址
    context_t ctx;             // 内核态上下文
} proc_t;
```

#### 2.1.2 核心函数实现

1. **进程初始化与管理**

   - `proc_init()`: 初始化进程数组，设置每个进程的 kstack 字段
   - `proc_alloc()`: 从进程数组分配一个空闲进程，初始化各字段
   - `proc_free()`: 释放进程资源，包括页表、trapframe、内核栈等
2. **进程创建**

   - `proc_fork()`: 创建子进程，复制父进程的内存空间、页表和 trapframe
   - 父进程返回子进程 PID，子进程返回 0
3. **进程退出与等待**

   - `proc_exit()`: 进程退出，进入 ZOMBIE 状态，唤醒父进程
   - `proc_wait()`: 父进程等待子进程退出，回收子进程资源
   - `proc_reparent()`: 将孤儿进程托付给 proczero

### 2.2 任务二：进程调度

#### 2.2.1 调度算法

采用时间片轮转（Round Robin）算法：

- 每个进程配置固定时间片
- 时钟中断时递减时间片
- 时间片耗尽时触发调度

#### 2.2.2 调度机制

实现了两阶段调度：

1. **`proc_sched()`**: 当前进程切换到调度器

   - 检查锁状态和中断状态
   - 保存当前进程上下文
   - 切换到调度器上下文
2. **`proc_scheduler()`**: 调度器选择新进程

   - 遍历进程数组寻找 RUNNABLE 进程
   - 切换到选中进程的上下文
   - 恢复进程执行

#### 2.2.3 Sleep 和 Wakeup

- `proc_sleep()`: 进程睡眠等待资源，释放 CPU
- `proc_wakeup()`: 唤醒所有等待特定资源的进程
- 使用 wait_lock 保证睡眠和唤醒的原子性

## 三、遇到的问题及解决方案

### 问题 1: EPC 寄存器设置错误

**问题描述**：
在用户态陷入内核态后，返回用户态时 PC 指向错误位置，导致程序执行异常。

**原因分析**：
在 `trap_user.c` 的 `trap_user_return()` 函数中，错误地将 trapframe 的 epc 写入了 sepc 寄存器两次，导致返回地址被覆盖。

**解决方案** (commit `a4bf8bb` 和 `fcbea3e`)：

```c
// 修改前（错误）
w_sepc(p->tf->epc);
// ... 其他操作
w_sepc(p->tf->epc);  // 重复写入

// 修改后（正确）
w_sepc(p->tf->epc);  // 只在开始时写入一次
// ... 其他操作不再修改 sepc
```

**修改文件**：

- `whu-oslab-lab6/code/kernel/trap/trap_user.c`
- `whu-oslab-lab6/code/kernel/proc/proc.c`

---

### 问题 2: scause==8 时未正确处理系统调用

**问题描述**：
当用户程序触发系统调用（scause == 8）时，内核没有正确处理，导致系统调用无法执行。

**原因分析**：
在 `trap_user_handler()` 中，缺少对 scause == 8（环境调用异常）的处理逻辑，没有调用 `syscall_handler()`。

**解决方案** (commit `9f1b245`)：
在 `trap_user.c` 中添加系统调用处理：

```c
void trap_user_handler(trapframe_t* tf) {
    uint64 scause_val = r_scause();
  
    if (scause_val == 8) {
        // 系统调用
        if (myproc()->state == UNUSED) {
            panic("trap_user_handler: proc is UNUSED");
        }
  
        // 调用系统调用处理函数
        tf->epc += 4;  // ecall 指令后移
        intr_on();
        syscall_handler();
        intr_off();
    }
    // ... 其他中断处理
}
```

**修改文件**：

- `whu-oslab-lab6/code/kernel/trap/trap_user.c`
- `whu-oslab-lab6/code/user/initcode.c`
- `whu-oslab-lab6/code/user/syscall_num.h`

---

### 问题 3: mmap_init 和 proc_init 未在 main 中调用

**问题描述**：
系统启动时，mmap 分配器和进程管理模块未初始化，导致：

- mmap 系统调用失败
- 进程的 kstack 字段为 0，引发栈指针错误

**原因分析**：
在 `main.c` 中遗漏了 `mmap_init()` 和 `proc_init()` 的调用。

**解决方案** (commit `7b12dda` 和 `2790c73`)：

```c
int main() {
    int cpuid = r_tp();
  
    if (cpuid == 0) {
        // ... 其他初始化
  
        // 初始化 mmap 区域管理器
        mmap_init();
        printf("MMAP region allocator initialized\n");
  
        // 初始化内核虚拟内存
        kvm_init();
        kvm_inithart();
  
        // 初始化进程表（必须在 kvm_inithart 之后）
        proc_init();
  
        // 初始化 CPU 结构
        cpu_init();
  
        // ... 其他初始化
    }
    // ...
}
```

**影响**：

- `mmap_init()` 缺失导致用户程序的 mmap 分配失败
- `proc_init()` 缺失导致所有进程的 `kstack` 为 0，进而导致：
  - 初始栈指针 `sp = kstack + PGSIZE = 0x1000`（错误）
  - 正确应该是 `sp = 0x3fffffd000`（内核栈高地址）

**修改文件**：

- `whu-oslab-lab6/code/kernel/boot/main.c`

---

### 问题 4: MMAP 和 HEAP 分配失败

**问题描述**：
用户程序调用 mmap 和 sbrk 分配内存时失败，无法正常分配堆和映射区域。

**原因分析**：

1. `mmap_init()` 未调用（问题3）
2. `uvm_mmap()` 和 `uvm_sbrk()` 函数实现不完整
3. 进程结构体缺少 `ustack_base` 字段

**解决方案** (commit `f14b536` 和 `9304438`)：

**修改 1**: 完善 `uvm_mmap()` 函数

```c
uint64 uvm_mmap(proc_t* p, uint64 length) {
    // 计算需要的页数
    uint32 npages = (length + PGSIZE - 1) / PGSIZE;
  
    // 从 mmap 区域分配
    mmap_region_t* region = mmap_region_alloc();
    if (region == 0)
        return 0;
  
    // 计算起始地址
    uint64 begin;
    if (p->mmap == NULL) {
        begin = MMAP_START;
    } else {
        // 找到最后一个区域
        mmap_region_t* last = p->mmap;
        while (last->next != NULL)
            last = last->next;
        begin = last->begin + last->npages * PGSIZE;
    }
  
    // 分配物理页并映射
    for (uint64 va = begin; va < begin + npages * PGSIZE; va += PGSIZE) {
        void* pa = pmem_alloc(false);
        if (pa == 0) {
            // 回滚已分配的页
            vm_unmappages(p->pgtbl, begin, va - begin, true);
            mmap_region_free(region);
            return 0;
        }
        memset(pa, 0, PGSIZE);
        vm_mappages(p->pgtbl, va, (uint64)pa, PGSIZE, 
                    PTE_R | PTE_W | PTE_U);
    }
  
    // 设置 region 信息
    region->begin = begin;
    region->npages = npages;
    region->next = NULL;
  
    // 添加到链表
    if (p->mmap == NULL) {
        p->mmap = region;
    } else {
        mmap_region_t* last = p->mmap;
        while (last->next != NULL)
            last = last->next;
        last->next = region;
    }
  
    return begin;
}
```

**修改 2**: 完善 `uvm_sbrk()` 函数

```c
uint64 uvm_sbrk(proc_t* p, int n) {
    uint64 old_top = p->heap_top;
    uint64 new_top = old_top + n;
  
    if (n > 0) {
        // 扩展堆
        uint64 old_top_aligned = PGROUNDUP(old_top);
        uint64 new_top_aligned = PGROUNDUP(new_top);
  
        // 分配新页
        for (uint64 va = old_top_aligned; va < new_top_aligned; va += PGSIZE) {
            void* pa = pmem_alloc(false);
            if (pa == 0) {
                // 回滚
                vm_unmappages(p->pgtbl, old_top_aligned, 
                             va - old_top_aligned, true);
                return -1;
            }
            memset(pa, 0, PGSIZE);
            vm_mappages(p->pgtbl, va, (uint64)pa, PGSIZE,
                       PTE_R | PTE_W | PTE_U);
        }
    } else if (n < 0) {
        // 收缩堆
        uint64 new_top_aligned = PGROUNDUP(new_top);
        uint64 old_top_aligned = PGROUNDUP(old_top);
  
        if (new_top_aligned < old_top_aligned) {
            uint64 release_size = old_top_aligned - new_top_aligned;
            vm_unmappages(p->pgtbl, new_top_aligned, release_size, true);
        }
    }
  
    p->heap_top = new_top;
    return old_top;
}
```

**修改 3**: 添加 `ustack_base` 字段

```c
// 在 proc.h 中添加
typedef struct proc {
    // ...
    uint64 ustack_base;    // 用户栈基地址
    uint64 ustack_pages;   // 用户栈页数
    // ...
} proc_t;
```

**修改文件**：

- `whu-oslab-lab6/code/kernel/mem/uvm.c`
- `whu-oslab-lab6/code/include/proc/proc.h`
- `whu-oslab-lab6/code/include/mem/vmem.h`

---

### 问题 5: 父进程没有输出 "parent: hello"

**问题描述**：
子进程正常执行并输出，但父进程调用 wait 等待子进程后，没有继续执行，无法输出 "parent: hello"。

**问题分析过程**：

通过添加大量调试输出，逐步定位问题：

1. **确认子进程工作正常**：子进程能正常打印 "child: hello"、"MMAP"、"HEAP"
2. **确认 exit 正常**：子进程调用 exit 后正确进入 ZOMBIE 状态
3. **确认 wakeup 正常**：子进程 exit 时正确唤醒父进程（状态变为 RUNNABLE）
4. **确认调度器工作**：调度器找到父进程并切换到父进程
5. **发现关键问题**：父进程的 `swtch()` 返回了，但之后的代码不执行

**根本原因**：

经过深入调试，发现了两个关键 bug：

**Bug 1: 栈指针损坏**

添加调试输出显示上下文：

```c
printf("[SCHEDULER] switching to pid=%d, ra=%p, sp=%p\n", 
       mycpuid(), p->pid, p->ctx.ra, p->ctx.sp);
```

发现父进程两次被调度时 sp 值不同：

- 第一次调度：`sp=0x0000000000001000`（初始值，kstack=0 导致）
- 第二次调度：`sp=0x0000000000000ec0`（损坏的值）

**Bug 2: 页表释放内存区域错误**

父进程回收子进程时调用 `proc_free()`，在 `uvm_destroy_pgtbl()` 中释放页表：

```c
// 错误的代码
void uvm_destroy_pgtbl(pgtbl_t pgtbl) {
    // ...
    destroy_pgtbl(pgtbl, 2);
  
    // 错误：用户页表从用户区域分配，却用 true（内核区域）释放
    pmem_free((uint64)pgtbl, true);  // 错误
}
```

这导致 `pmem_free()` 检查失败，触发 panic：

```
[PMEM_FREE] page=0x0000000087ffa000, in_kernel=1, region=[0x000000008000e000, 0x000000008040e000)
panic: pmem_free: invalid page or wrong region
```

**解决方案** (commit `0e72f13` 和 `2790c73`)：

**修复 1**: 在 `main.c` 中添加 `proc_init()` 调用

```c
int main() {
    if (cpuid == 0) {
        // ...
        kvm_inithart();
  
        // 初始化进程表
        proc_init();  // 添加此调用
  
        cpu_init();
        // ...
    }
}
```

这确保所有进程的 `kstack` 字段被正确初始化：

```c
void proc_init() {
    // 初始化 wait_lock
    spinlock_init(&wait_lock, "wait_lock");
  
    // 初始化所有进程
    for(p = procs; p < &procs[NPROC]; p++) {
        spinlock_init(&p->lk, "proc");
        p->state = UNUSED;
        p->kstack = KSTACK((int)(p - procs));  // 正确设置 kstack
    }
}
```

**修复 2**: 修正页表释放的内存区域

```c
void uvm_destroy_pgtbl(pgtbl_t pgtbl) {
    // 解除 trampoline 和 trapframe 的映射
    pte_t* pte_trampoline = vm_getpte(pgtbl, TRAMPOLINE, false);
    if (pte_trampoline)
        *pte_trampoline = 0;
  
    pte_t* pte_trapframe = vm_getpte(pgtbl, TRAPFRAME, false);
    if (pte_trapframe)
        *pte_trapframe = 0;
  
    // 递归销毁页表
    destroy_pgtbl(pgtbl, 2);
  
    // 修正：用户页表从用户区域分配，所以用 false 释放
    pmem_free((uint64)pgtbl, false);  // 从 true 改为 false
}
```

**验证结果**：

修复后，系统正常输出：

```
user begin
child: hello
MMAP
HEAP
parent: hello  
```

**修改文件**：

- `whu-oslab-lab6/code/kernel/boot/main.c` - 添加 `proc_init()` 调用
- `whu-oslab-lab6/code/kernel/mem/uvm.c` - 修正 `pmem_free()` 的 `in_kernel` 参数
- `whu-oslab-lab6/code/kernel/proc/proc.c` - 添加大量调试输出（后续已清理）

---

### 问题 6: wait/exit 锁机制同步问题

**问题描述**：
在多进程环境下，wait 和 exit 之间存在竞态条件，可能导致父进程错过子进程的唤醒信号。

**解决方案** (commit `9f5f1d0`)：

引入全局 `wait_lock`，确保 wait 和 exit 的原子性：

```c
// 全局 wait_lock
static spinlock_t wait_lock;

// proc_wait 中使用 wait_lock
int proc_wait(uint64 addr) {
    proc_t* pp;
    int havekids, pid;
    proc_t* p = myproc();
  
    spinlock_acquire(&wait_lock);  // 获取 wait_lock
  
    for(;;) {
        havekids = 0;
        for(pp = procs; pp < &procs[NPROC]; pp++) {
            if(pp->parent == p) {
                spinlock_acquire(&pp->lk);
                havekids = 1;
          
                if(pp->state == ZOMBIE) {
                    // 找到 zombie 子进程，回收资源
                    proc_free(pp);
                    spinlock_release(&pp->lk);
                    spinlock_release(&wait_lock);
                    return pid;
                }
                spinlock_release(&pp->lk);
            }
        }
  
        if(!havekids) {
            spinlock_release(&wait_lock);
            return -1;
        }
  
        // sleep 会原子地释放 wait_lock 并睡眠
        proc_sleep(p, &wait_lock);
    }
}

// proc_exit 中也使用 wait_lock
void proc_exit(int exit_state) {
    proc_t* p = myproc();
  
    spinlock_acquire(&wait_lock);  // 获取 wait_lock
  
    // 重新分配子进程
    proc_reparent(p);
  
    // 唤醒父进程
    proc_wakeup(p->parent);
  
    spinlock_acquire(&p->lk);
    p->exit_state = exit_state;
    p->state = ZOMBIE;
  
    spinlock_release(&wait_lock);  // 释放 wait_lock
  
    // 调度到其他进程
    proc_sched();
}
```

**修改文件**：

- `whu-oslab-lab6/code/kernel/proc/proc.c`

---

## 四、实验结果

![1763964868204](image/doc/1763964868204.png)

## 五、实验总结

### 5.1 关键技术点

1. **进程管理**：实现了完整的进程生命周期管理
2. **内存管理**：正确区分内核页和用户页的分配与释放
3. **同步机制**：使用锁保证 wait/exit 的原子性
4. **上下文切换**：正确保存和恢复进程上下文
5. **调度算法**：实现时间片轮转调度

### 5.2 调试技巧

1. **分层调试**：从系统调用 → 进程管理 → 内存管理 逐层定位
2. **状态跟踪**：通过 printf 跟踪进程状态转换
3. **上下文检查**：打印关键寄存器值（ra、sp）定位问题
4. **内存分析**：检查物理地址是否在正确的内存区域

### 5.3 经验教训

1. **初始化顺序很重要**：`proc_init()` 必须在使用进程前调用
2. **内存区域要匹配**：分配和释放必须使用相同的内存区域（内核/用户）
3. **锁的使用要谨慎**：sleep 和 wakeup 需要额外的锁来防止丢失唤醒
4. **栈指针要正确**：kstack 为 0 会导致栈指针错误，引发难以调试的问题
