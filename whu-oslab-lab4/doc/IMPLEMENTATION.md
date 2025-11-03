# Lab-4 实现总结

## 实现的功能

### 1. 进程管理 (proc)

#### `kernel/proc/proc.c`
- **`proc_pgtbl_init()`**: 初始化用户进程页表
  - 创建空的页表
  - 映射 trampoline 页（用户态/内核态切换代码）
  - 映射 trapframe 页（保存用户态寄存器）

- **`proc_make_first()`**: 创建第一个用户态进程 proczero
  - 显式初始化 proczero 结构为 0（避免奇怪错误）
  - 分配并映射 trapframe
  - 创建用户页表（包括 trampoline 和 trapframe 映射）
  - 分配并映射用户栈（1页）
  - 分配并映射用户代码页（从 PGSIZE 开始，避开最低 4KB）
  - 将 initcode 复制到代码页
  - 设置 trapframe（epc、sp、kernel_satp等）
  - 分配内核栈
  - 设置进程上下文，准备第一次调度
  - 通过 swtch() 切换到用户进程

#### `kernel/proc/cpu.c`
- **`myproc()`**: 获取当前 CPU 上运行的进程
  - 通过 mycpu() 获取当前 CPU 结构
  - 返回 CPU 上的进程指针

### 2. 中断处理 (trap)

#### `kernel/trap/trap_user.c`
- **`trap_user_handler()`**: 用户态 trap 处理函数
  - 检查 trap 来源（必须是 U-mode）
  - 判断是中断还是异常
  - 处理系统调用（ecall from U-mode）
    - 输出系统调用信息
    - 将 epc + 4 跳过 ecall 指令
  - 处理其他异常（输出错误信息）
  - 返回用户态

- **`trap_user_return()`**: 从内核态返回用户态
  - 关中断
  - 设置 stvec 指向用户态 trap 向量
  - 更新 trapframe 中的内核信息
  - 切换到用户页表
  - 调用 trampoline.S 中的 user_return 恢复用户态寄存器并执行 sret

### 3. 用户程序 (user)

#### `user/initcode.c`
- 第一个用户进程的代码
- 执行两个简单的系统调用（SYS_print）
- 进入死循环

### 4. 内存布局更新

#### `include/memlayout.h`
- 添加 `MAXVA`: 用户地址空间最大值
- 添加 `TRAMPOLINE`: trampoline 页的虚拟地址
- 添加 `TRAPFRAME`: trapframe 页的虚拟地址

### 5. 启动流程更新

#### `kernel/boot/main.c`
- 初始化物理内存管理器
- 初始化内核虚拟内存
- 初始化 CPU 结构
- 初始化 trap 系统
- CPU 0 调用 `proc_make_first()` 创建并切换到第一个用户进程
- 其他 CPU 进入空循环等待

## 预期行为

1. 系统启动后，CPU 0 完成初始化
2. 创建 proczero 进程
3. 通过 swtch() 切换到 proczero 的上下文
4. trap_user_return() 切换到用户态
5. 用户程序执行两次系统调用（触发 trap，进入 trap_user_handler）
6. 系统调用处理完成后返回用户态
7. 用户程序进入死循环
8. CPU 0 陷在用户态死循环中
9. 其他 CPU 陷在 main() 末尾的死循环中

## 注意事项

1. **全局数据初始化为 0**: proczero 结构使用 memset 显式初始化为 0
2. **不实现的功能**:
   - 进程状态管理
   - 多进程
   - 时间片调度
   - 有实际功能的系统调用（只是简单输出）
   - 文件系统

## 编译和运行

```bash
cd whu-oslab-lab4/code
make clean
make
make qemu
```

预期看到系统初始化信息，然后看到两次系统调用的输出，最后系统停在用户态死循环中。
