
# 实验六：进程管理

---

## RISC-V 体系结构

- RISC-V 体系结构规定了三种特权模式：用户模式（User mode，Umode）、监管者模式（Supervisor mode，S-mode）、机器模式（Machine mode，Mmode）。

---

## 实验目标

- 本阶段目标：在上一阶段的基础上，实现进程的多种状态，以及多进程的调度切换。
  - （1）完善进程体定义，实现相关系统调用
  - （2）实现进程的调度
  - （3）实现 sleep 和 wakeup

---

## 任务一: fork + exit + wait

- 首先删去上一次的临时系统调用: sys_copyin0、sys_copyout0、sys_copyinstr0。
- 添加新的系统调用 sys_print0、sys_fork0、sys_exit0、sys_wait0、sys_sleep0。

---

## 进程数组与全局变量

```c
// 进程数组
static proc_t procs[NPROC];
// 第一个进程的指针
static proc_t* proczero;
// 全局的pid和保护它的锁
static int global_pid = 1;
static spinlock_t lk_pid;
```


---

## 进程结构体定义

<pre><div class="relative m-0 rounded-md border border-default bg-default whitespace-break-spaces"><div data-exclude-copy="true" class="sticky top-0 mb-1 flex items-center justify-between gap-1 rounded-t-md border-b border-default bg-default px-3 font-sans select-none"><div class="inline-flex items-center gap-2"><i role="img" aria-label="c icon" class="devicon-c-plain colored"></i><span class="text-xs capitalize">c</span></div><div class="inline-flex items-center gap-1"><button class="disabled:pointer-auto focus-visible:outline-hidden items-center justify-center whitespace-nowrap rounded-md font-normal transition-colors disabled:opacity-50 [&_svg]:size-4 [&_svg]:shrink-0 text-subtle disabled:text-hint hover:bg-subtle my-1 flex h-auto gap-1.5 px-1 py-2 text-xs" type="button" aria-label="Copy code to clipboard"><svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.75" stroke-linecap="round" stroke-linejoin="round" class="lucide lucide-copy size-6 shrink-0" aria-hidden="true"><rect width="14" height="14" x="8" y="8" rx="2" ry="2"></rect><path d="M4 16c-1.1 0-2-.9-2-2V4c0-1.1.9-2 2-2h10c1.1 0 2 .9 2 2"></path></svg> Copy</button></div></div><div class="text-sm text-default select-text"><code class="language-c"><span>typedef</span><span></span>structproc {<span>
</span><span></span><span>spinlock_t</span><span> lk; </span><span>// 自旋锁</span><span>
</span><span></span><span>/* 下面的五个字段需要持有锁才能修改 */</span><span>
</span><span></span><span>int</span><span> pid; </span><span>// 标识符</span><span>
</span><span></span>enumproc_statestate;<span></span><span>// 进程状态</span><span>
</span><span></span>structproc* parent;<span></span><span>// 父进程</span><span>
</span><span></span><span>int</span><span> exit_state; </span><span>// 进程退出时的状态(父进程可能关心)</span><span>
</span><span></span><span>void</span><span>* sleep_space; </span><span>// 睡眠是在等待什么</span><span>
</span><span></span><span>pgtbl_t</span><span> pgtbl; </span><span>// 用户态页表</span><span>
</span><span>    uint64 heap_top; </span><span>// 用户堆顶(以字节为单位)</span><span>
</span><span>    uint64 ustack_pages; </span><span>// 用户栈占用的页面数量</span><span>
</span><span></span><span>mmap_region_t</span><span>* mmap; </span><span>// 用户可映射区域的起始节点</span><span>
</span><span></span><span>trapframe_t</span><span>* tf; </span><span>// 用户态内核态切换时的运行环境暂存空间</span><span>
</span><span>    uint64 kstack; </span><span>// 内核栈的虚拟地址</span><span>
</span><span></span><span>context_t</span><span> ctx; </span><span>// 内核态进程上下文</span><span>
</span><span>} </span><span>proc_t</span><span>;</span></code></div></div></pre>

---

## 进程管理模块

* 首先实现以下三个函数：
  * `proc_init()` 初始化一些全局变量, 设置 kstack 字段并调用 `proc_free()`
  * `proc_alloc()` 从进程数组里申请一个进程, 并申请资源和初始化一些字段，分配和初始化新的进程结构体，包括分配 PID、创建页表、设置内核栈等关键操作。
  * `proc_free()` 向进程数组里归还一个进程, 并释放资源和清空一些字段
  * 这三个函数是进程数组这一全局资源的控制函数, 因此将它们视为一组操作

---

## 实现进程创建函数 `proc_fork()`

1. 调用 `proc_alloc` 分配一个新的进程槽位
2. 复制父进程的用户内存空间
3. 复制父进程的陷阱帧(trapframe)
4. 设置子进程的返回值为 0
5. 复制打开的文件描述符和当前工作目录
6. 设置子进程状态为 RUNNABLE

* 可以发现各个进程会组成一个由 proczero 作为根节点的进程树。
* 这两个进程在用户态怎么区分呢？解决方案是让它们的返回值不同
  * 对于父进程，它收到的返回值是子进程的 pid，通过函数返回做到
  * 对于子进程，它收到的是 0，通过修改 `p->tf->a0` 做到。

---

## 进程管理

* 完成 `proc_fork()` 后，进入下一组函数：
  * `proc_wait()` 等待子进程退出：负责扫描子进程并等待其终止。
  * `proc_free()` 回收子进程：释放 ZOMBIE 进程的所有资源，包括页表、陷阱帧等。
  * `proc_exit()` 进程退出：退出当前进程，并将其父进程唤醒，进入 ZOMBIE 状态。

---

## 进程状态转换

* 这三个函数构成一个常见的组合:
  <pre><div class="relative m-0 rounded-md border border-default bg-default whitespace-break-spaces"><div data-exclude-copy="true" class="sticky top-0 mb-1 flex items-center justify-between gap-1 rounded-t-md border-b border-default bg-default px-3 font-sans select-none"><div class="inline-flex items-center gap-2"><i role="img" aria-label="c icon" class="devicon-c-plain colored"></i><span class="text-xs capitalize">c</span></div><div class="inline-flex items-center gap-1"><button class="disabled:pointer-auto focus-visible:outline-hidden items-center justify-center whitespace-nowrap rounded-md font-normal transition-colors disabled:opacity-50 [&_svg]:size-4 [&_svg]:shrink-0 text-subtle disabled:text-hint hover:bg-subtle my-1 flex h-auto gap-1.5 px-1 py-2 text-xs" type="button" aria-label="Copy code to clipboard"><svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.75" stroke-linecap="round" stroke-linejoin="round" class="lucide lucide-copy size-6 shrink-0" aria-hidden="true"><rect width="14" height="14" x="8" y="8" rx="2" ry="2"></rect><path d="M4 16c-1.1 0-2-.9-2-2V4c0-1.1.9-2 2-2h10c1.1 0 2 .9 2 2"></path></svg> Copy</button></div></div><div class="text-sm text-default select-text"><code class="language-c"><span>int</span><span> pid = fork(i); </span><span>// 分支</span><span>
  </span><span></span><span>if</span><span> (pid == </span>0<span>) { </span><span>// 子进程</span><span>
  </span><span></span><span>do</span><span> something ...
  </span><span></span><span>exit</span><span>(</span>0<span>);
  </span><span>} </span><span>else</span><span> { </span><span>// 父进程</span><span>
  </span><span></span><span>int</span><span> exit_state;
  </span>    wait(&exit_state);
  <span></span><span>do</span><span> something ...
  </span>}</code></div></div></pre>

---

## 进程管理问题

* 由于进程的树形结构, 如果出现父进程退出但子进程没退出的情况该怎么办呢?
  * 在 `proc_exit()` 函数里调用 `proc_reparent()` 将当前进程的孩子托付给 proczero, 因为它是整棵进程树的根, 是不会退出的。
* 由于 `proc_wait()` 函数是一个循环, 所以没等到子进程退出就会一直占用 CPU，所以添加一个新的函数 `proc_yield()`, 进程放弃 CPU 使用权进入调度

---

## 活跃度

* running > runnable > sleeping > zombie > unused

[进程状态转换图](image_url)

---

## 进程状态转换说明

* ⑴ 空闲进程结构体被取出和初始化
* ⑵ 进程调度：从可运行进程中选出一个换掉正在执行的进程
* ⑶ 进程等待某种资源或某个事件，进入睡眠状态
* ⑷ 进程得到所等待的东西后被唤醒
* ⑸ 进程执行完毕，主动杀死自己
* ⑹ 处于可执行状态或睡眠状态的进程被正在执行的进程杀死
* ⑺ 进程的父进程处理后事，释放濒死进程的资源

---

## CPU 调度示意图

* ⑴ 内核完成机器启动，打开调度器
* ⑵ 进程 A 被调度，上 CPU 执行
* ⑶ 进程 A 的执行流被暂停，调度器选择一个可执行进程
* ⑷ 进程 B 被调度，上 CPU 执行
* ⑸ 进程 B 的执行流被暂停，调度器选择一个可执行进程
* ⑹ 进程 A 被调度，上 CPU 执行

---

## 任务二：进程调度

* 采用的进程调度算法是时间片轮转（Round Robin, RR）。算法思路如下：在初始化时为每个进程配置一个时间片；当某个进程处于 running 状态且发生时钟中断时, 这个进程的时间片减 1；当时间片减到 0 时，触发调度，将 CPU 使用权切换到调度器, 同时重置该进程的时间片。

---

## 调度函数

* 调度函数 `proc_sched0` 和 `proc_scheduler0` 是调度的两个阶段:
  * 第一阶段：`proc_sched0` 检查了一些前提条件（锁的状态、中断状态等），然后保存当前中断状态，进行上下文切换到调度器上下文，当再次被调度回来时恢复中断状态。当前 CPU 的用户进程 -> CPU 自己的进程(之前称之为高级进程)
  * 第二阶段：`proc_scheduler0` 需要挑选新的 RUNNABLE 用户进程，这里采用各进程按顺序轮流执行的调度算法。CPU 自己的进程 -> 被选中的新用户进程
  * 当占用 CPU 的进程是 CPU 自己的进程时，`mycpu0->proc = NULL`

---

## 进程切换

* 两个阶段的核心操作都是 `swtch()` 做上下文切换
* `swtch()` 通过 ret 返回进程 kernel 栈，如果该进程第一次调度，即返回到 `fork_return()`，如果不是第一次调度，则返回到内核调用 `proc_sched()` 的地方。
* 当调度器启动后，CPU 自己的进程就被困在调度器里了（死循环），它的上下文就是调度器的运行环境。
* 在 `main()` 的最后，各个 CPU 的进程都会进入调度器，并忠实地留在这里。
* 于是进程切换的过程: proc-1 -> CPU 进程(调度器) -> proc-2

---

## 时间片管理

* 时间片到了，可以利用时钟中断实现这件事：在发生时钟中断后调用 `proc_yield()`：
  * `proc_yield()` 使本进程进入就绪态，并进而调用 `proc_sched()` 让出 CPU。
  * 定时器中断触发进程切换是在 `trap_user_handler()` 和 `trap_kernel_handler()` 中处理。
  * `timer_update()` 调用了 `wakeup()`，唤醒那些上了闹钟的进程。

---

## sleep 和 wakeup

* 自旋锁在获取不到资源时，CPU 会空转直到获取资源，所以就应该有另一种方法，在获取不到时，让别的进程执行，直到某个条件达成时，再返回去执行这个任务。
* XV6 使用一种叫睡眠和唤醒的机制，这允许一个进程睡眠并等待一个事件，当事件发生时另一个进程来唤醒它。睡眠和唤醒通常称为序列协调或条件同步机制。
* `proc_sleep()`：将调用的进程睡眠，释放 CPU 给其他进程工作。标记进程为 SLEEPING，调用 `proc_sched` 来释放 CPU。

---

## 唤醒函数

* 唤醒函数有两种
  * `proc_wakeup_one0` 只被 `proc_exit0` 调用，它唤醒指定的单个进程。
  * `proc_wakeup0` 未来还会在其他地方调用，它唤醒所有睡在 sleep_space 的进程。
* 注意锁的使用：理解 XV6 为什么 sleep 和 wakeup 的锁规则保证了睡眠进程不会丢失唤醒?

---

## 修改 `proc_make_first()`

* 由于可以直接调用 `proc_alloc()`, 一些操作可以删去。
* 由于后面要引入调度器, `proc_make_first()` 无需调用 `swtch()`, 直接返回即可。

---

## 进程状态转换流程图

[进程状态转换流程图](image_url)

---

## 进程状态转换说明

* 进程创建 fork
* 分配进程结构
* USED 状态
* 调度器选中
* RUNNING 状态
  * 等待资源
  * 资源可用
  * 主动放弃 CPU
  * 调度器选中
  * exit 系统调用
  * 父进程 wait
  * SLEEPING 状态
  * RUNNABLE 状态
  * ZOMBIE 状态
    * 父进程 wait
    * 释放资源
    * UNUSED 状态
