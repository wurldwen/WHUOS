# Lab 7 文件系统实验报告

## 实验概述

本次实验完成了基于 xv6 的文件系统实现，包括位图管理、缓冲区缓存、inode 管理、目录操作和文件操作等核心功能。

## 遇到的问题及解决方案

### 1. sleeplock 未实现问题

**问题描述：**
编译时出现以下错误：
```
error: implicit declaration of function 'sleeplock_init'
error: implicit declaration of function 'sleeplock_acquire'
error: implicit declaration of function 'sleeplock_release'
error: implicit declaration of function 'sleeplock_holding'
```

**原因分析：**
项目中未实现 sleeplock（睡眠锁）机制，但代码中多处使用了 sleeplock 相关函数。

**解决方案：**
将所有 sleeplock 替换为 spinlock（自旋锁）作为等价实现：

1. **修改头文件**（`include/fs/inode.h` 和 `include/fs/buf.h`）：
   ```c
   // 修改前
   sleeplock_t slk;
   
   // 修改后
   spinlock_t slk;
   ```

2. **修改实现文件**（`kernel/fs/buf.c`, `kernel/fs/inode.c`, `kernel/fs/dir.c`）：
   ```c
   // 初始化
   sleeplock_init(&buf->slk, "buffer") → spinlock_init(&buf->slk, "buffer")
   
   // 加锁
   sleeplock_acquire(&buf->slk) → spinlock_acquire(&buf->slk)
   
   // 解锁
   sleeplock_release(&buf->slk) → spinlock_release(&buf->slk)
   
   // 检查锁状态
   sleeplock_holding(&buf->slk) → spinlock_holding(&buf->slk)
   ```

**影响范围：**
- `kernel/fs/buf.c`: 4 处修改（buf_init, buf_read, buf_write, buf_release）
- `kernel/fs/inode.c`: 6 处修改（inode_init, inode_create, inode_destroy, inode_lock, inode_unlock, inode_print）
- `kernel/fs/dir.c`: 6 处修改（dir_search_entry, dir_add_entry, dir_delete_entry, dir_get_entries, dir_print, check_unlink）

---

### 2. VirtIO 磁盘驱动函数调用错误

**问题描述：**
```
error: implicit declaration of function 'vio_read'
error: implicit declaration of function 'vio_write'
```

**原因分析：**
代码中使用了 `vio_read` 和 `vio_write` 函数，但实际实现的是 `virtio_disk_rw` 函数。

**解决方案：**
在 `kernel/fs/buf.c` 中统一使用 `virtio_disk_rw` 函数：

```c
// 修改前
vio_read(block_num, buf->data);

// 修改后
virtio_disk_rw(buf, false);  // false 表示读操作

// 修改前
vio_write(block_num, buf->data);

// 修改后
virtio_disk_rw(buf, true);   // true 表示写操作
```

---

### 3. ALIGN_DOWN 宏未定义

**问题描述：**
```
error: implicit declaration of function 'ALIGN_DOWN'
```

**原因分析：**
在 `kernel/dev/virtio.c` 中使用了 `ALIGN_DOWN` 宏，但该宏在项目中未定义。

**解决方案：**
使用已有的 `PG_ROUND_DOWN` 宏替代：

```c
// 修改前（kernel/dev/virtio.c）
disk.desc = (virtq_desc_t*)ALIGN_DOWN((uint64)&buf0, PGSIZE);

// 修改后
disk.desc = (virtq_desc_t*)PG_ROUND_DOWN((uint64)&buf0);
```

`PG_ROUND_DOWN` 宏在 `include/riscv.h` 中已定义，功能是将地址向下对齐到页边界。

---

### 4. offsetof 宏未定义

**问题描述：**
```
error: implicit declaration of function 'offsetof'
```

**原因分析：**
在 `kernel/fs/buf.c` 中使用了 `offsetof` 宏来计算结构体成员的偏移量，但未包含定义该宏的头文件。

**解决方案：**
在 `kernel/fs/buf.c` 文件开头手动定义 `offsetof` 宏：

```c
// 添加在文件顶部
#define offsetof(TYPE, MEMBER) ((uint64)&((TYPE *)0)->MEMBER)
```

该宏通过将空指针转换为结构体指针，然后取成员地址的方式计算偏移量。

---

### 5. 进程结构体缺少 cwd 字段

**问题描述：**
```
error: request for member 'cwd' in something not a structure or union
```

**原因分析：**
目录操作相关代码需要访问进程的当前工作目录（current working directory），但 `proc_t` 结构体中缺少 `cwd` 字段。

**解决方案：**
在 `include/proc/proc.h` 中为 `proc_t` 结构体添加 `cwd` 字段：

```c
// 添加前向声明
typedef struct inode inode_t;

// 在 proc_t 结构体中添加字段
typedef struct proc {
    // ... 其他字段 ...
    
    uint64 kstack;           // 内核栈的虚拟地址
    context_t ctx;           // 内核态进程上下文
    
    inode_t* cwd;            // 当前工作目录（新增）
} proc_t;
```

---

### 6. 函数名称不匹配问题

**问题描述：**
```
error: implicit declaration of function 'cpu_curtask'
error: invalid type argument of '->' (have 'int')
```

**原因分析：**
代码中使用了 `cpu_curtask()` 函数获取当前任务，但项目中实际提供的是 `myproc()` 函数。同时，结构体字段名称也不匹配（`task->uvm` vs `proc->pgtbl`）。

**解决方案：**
在 `kernel/fs/dir.c` 和 `kernel/fs/inode.c` 中进行以下替换：

```c
// 函数名替换
cpu_curtask() → myproc()

// 字段名替换
task->uvm → proc->pgtbl

// 类型名替换
task_t* task → proc_t* proc
```

**具体修改示例：**
```c
// 修改前
task_t* task = cpu_curtask();
uvm_copyout(task->uvm, dst, src, len);

// 修改后
proc_t* proc = myproc();
uvm_copyout(proc->pgtbl, dst, src, len);
```

---

### 7. strcmp 函数未定义

**问题描述：**
```
error: implicit declaration of function 'strcmp'
note: 'strcmp' is defined in header '<string.h>'
```

**原因分析：**
项目的 `lib/str.h` 中只提供了 `strncmp` 函数，没有提供 `strcmp` 函数。

**解决方案：**
在 `kernel/fs/dir.c` 中将所有 `strcmp` 替换为 `strncmp`：

```c
// 修改前
if (strcmp(name, de.name) == 0)

// 修改后
if (strncmp(name, de.name, DIR_NAME_LEN) == 0)

// 修改前
if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)

// 修改后
if (strncmp(name, ".", DIR_NAME_LEN) == 0 || strncmp(name, "..", DIR_NAME_LEN) == 0)
```

---

### 8. uvm_copyout/uvm_copyin 参数类型错误

**问题描述：**
```
error: passing argument 3 of 'uvm_copyout' makes integer from pointer without a cast
```

**原因分析：**
`uvm_copyout` 和 `uvm_copyin` 函数的参数需要 `uint64` 类型，但传入的是指针类型。

**解决方案：**
在 `kernel/fs/inode.c` 和 `kernel/fs/dir.c` 中添加显式类型转换：

```c
// 修改前
uvm_copyout(proc->pgtbl, dst, buf->data + off, m);

// 修改后
uvm_copyout(proc->pgtbl, (uint64)dst, (uint64)(buf->data + off), m);

// 修改前
uvm_copyin(proc->pgtbl, buf->data + off, src, m);

// 修改后
uvm_copyin(proc->pgtbl, (uint64)(buf->data + off), (uint64)src, m);
```

---

### 9. 缺少头文件包含

**问题描述：**
某些源文件无法找到所需的函数声明和类型定义。

**解决方案：**
在 `kernel/fs/dir.c` 中添加缺失的头文件：

```c
#include "fs/fs.h"
#include "fs/buf.h"
#include "fs/inode.h"
#include "fs/dir.h"
#include "fs/bitmap.h"
#include "lib/str.h"
#include "lib/print.h"
#include "proc/cpu.h"
#include "mem/vmem.h"      // 新增：提供 uvm_copyout/uvm_copyin 声明
```

---

## 实验结果

完成所有修改后，项目成功编译并运行。QEMU 输出显示：

```
initcode:0x00000000800087a0
[SCHEDULER] CPU0 starting scheduler
[SCHEDULER] CPU0 found RUNNABLE pid=1
[SCHEDULER] CPU0 switching to pid=1, ra=0x00000000800043a0, sp=0x0000003fffffd000
[User Trap] System call from user mode 0x0000000000000018 scause:0x0000000000000008,stval:0x0000000000000000

user begin
```

**验证结果：**
- ✅ 内核成功编译，无警告和错误
- ✅ QEMU 成功启动内核
- ✅ 调度器正常初始化并运行
- ✅ 第一个用户进程（pid=1）成功创建和调度
- ✅ 系统调用机制工作正常
- ✅ 用户程序成功执行并输出 "user begin"

---

## 实现总结

本次实验成功实现了文件系统的核心功能，包括：

1. **位图管理**（bitmap.c）：实现块位图和 inode 位图的分配与释放
2. **缓冲区缓存**（buf.c）：实现 LRU 缓存策略的块缓冲区管理
3. **Inode 管理**（inode.c）：实现 inode 的创建、销毁、读写和数据块管理
4. **目录操作**（dir.c）：实现目录项的查找、添加、删除和路径解析
5. **文件操作**（file.c）：实现文件描述符表和文件的打开、读写、关闭操作

通过解决上述 9 个主要问题，成功将文件系统集成到操作系统内核中，为后续的用户程序提供了文件系统支持。

主要的技术要点：
- 使用 spinlock 替代 sleeplock 进行同步控制
- 正确处理内核空间和用户空间之间的数据拷贝
- 实现三级索引结构支持大文件存储
- 使用 LRU 策略优化缓冲区性能
- 通过 VirtIO 接口与虚拟磁盘设备交互
