#include "lib/print.h"
#include "lib/str.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "mem/mmap.h"
#include "proc/cpu.h"
#include "proc/initcode.h"
#include "memlayout.h"
#include "riscv.h"

/*----------------外部空间------------------*/

// in trampoline.S
extern char trampoline[];

// in swtch.S
extern void swtch(context_t* old, context_t* new);

// in trap_user.c
extern void trap_user_return();

/*----------------本地变量------------------*/

// 进程数组
static proc_t procs[NPROC];

// 第一个进程的指针
static proc_t* proczero;

// 全局的pid和保护它的锁 
static int global_pid = 1;
static spinlock_t lk_pid;

// wait/exit协调锁
static spinlock_t wait_lock;


// 申请一个pid(锁保护)
static int alloc_pid()
{
    int tmp = 0;
    spinlock_acquire(&lk_pid);
    assert(global_pid >= 0, "alloc_pid: overflow");
    tmp = global_pid++;
    spinlock_release(&lk_pid);
    return tmp;
}

// 释放锁 + 调用 trap_user_return
static void fork_return()
{
    // 由于调度器中上了锁，所以这里需要解锁
    proc_t* p = myproc();
    spinlock_release(&p->lk);
    trap_user_return();
}

// 返回一个未使用的进程空间
// 设置pid + 设置上下文中的ra和sp
// 申请tf和pgtbl使用的物理页
proc_t* proc_alloc()
{
    proc_t* p;
    
    // 遍历进程数组寻找未使用的进程
    for(p = procs; p < &procs[NPROC]; p++) {
        spinlock_acquire(&p->lk);
        if(p->state == UNUSED) {
            goto found;
        } else {
            spinlock_release(&p->lk);
        }
    }
    return NULL;

found:
    // 分配pid
    p->pid = alloc_pid();
    p->state = RUNNABLE;
    
    // 分配trapframe页 (从内核区域分配)
    if((p->tf = (trapframe_t*)pmem_alloc(true)) == 0) {
        proc_free(p);
        spinlock_release(&p->lk);
        return NULL;
    }
    
    // 创建页表
    p->pgtbl = proc_pgtbl_init((uint64)p->tf);
    if(p->pgtbl == 0) {
        proc_free(p);
        spinlock_release(&p->lk);
        return NULL;
    }
    
    // 为内核栈分配物理页并在内核页表中建立映射
    char *kstack_pa = pmem_alloc(true);
    if(kstack_pa == 0) {
        proc_free(p);
        spinlock_release(&p->lk);
        return NULL;
    }
    pgtbl_t kernel_pgtbl = kvm_get_kernel_pgtbl();
    vm_mappages(kernel_pgtbl, p->kstack, (uint64)kstack_pa, PGSIZE, PTE_R | PTE_W);
    
    // 设置上下文: 返回地址设为fork_return，栈指针设为内核栈顶
    memset(&p->ctx, 0, sizeof(p->ctx));
    p->ctx.ra = (uint64)fork_return;
    p->ctx.sp = p->kstack + PGSIZE;
    
    return p;
}

// 释放一个进程空间
// 释放pgtbl的整个地址空间
// 释放mmap_region到仓库
// 设置其余各个字段为合适初始值
// tips: 调用者需持有p->lk
void proc_free(proc_t* p)
{
    // 释放trapframe (从内核区域释放)
    if(p->tf) {
        pmem_free((uint64)p->tf, true);
    }
    p->tf = 0;
    
    // 释放页表
    if(p->pgtbl) {
        uvm_destroy_pgtbl(p->pgtbl);
    }
    p->pgtbl = 0;
    
    // 释放内核栈的映射和物理页
    if(p->kstack) {
        pgtbl_t kernel_pgtbl = kvm_get_kernel_pgtbl();
        vm_unmappages(kernel_pgtbl, p->kstack, PGSIZE, true);
    }
    
    // 释放mmap区域
    if(p->mmap) {
        mmap_region_free(p->mmap);
    }
    p->mmap = 0;
    
    // 重置字段
    p->pid = 0;
    p->parent = 0;
    p->exit_state = 0;
    p->sleep_space = 0;
    p->heap_top = 0;
    p->ustack_base = 0;
    p->ustack_pages = 0;
    p->state = UNUSED;
}

// 进程模块初始化
void proc_init()
{
    proc_t* p;
    
    // 初始化pid锁
    spinlock_init(&lk_pid, "nextpid");
    
    // 初始化wait_lock
    spinlock_init(&wait_lock, "wait_lock");
    
    // 初始化所有进程
    for(p = procs; p < &procs[NPROC]; p++) {
        spinlock_init(&p->lk, "proc");
        p->state = UNUSED;
        p->kstack = KSTACK((int)(p - procs));
    }
}

// 获得一个初始化过的用户页表
// 完成了trapframe 和 trampoline 的映射
pgtbl_t proc_pgtbl_init(uint64 trapframe)
{
    pgtbl_t pgtbl;
    
    // 创建空页表
    pgtbl = (pgtbl_t)pmem_alloc(false);
    if(pgtbl == 0)
        return 0;
    memset(pgtbl, 0, PGSIZE);
    
    // 映射trampoline页 (不设置PTE_U，因为只有supervisor模式使用)
    if(vm_getpte(pgtbl, TRAMPOLINE, true) == 0) {
        uvm_destroy_pgtbl(pgtbl);
        return 0;
    }
    vm_mappages(pgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
    
    // 映射trapframe页
    if(vm_getpte(pgtbl, TRAPFRAME, true) == 0) {
        vm_unmappages(pgtbl, TRAMPOLINE, PGSIZE, false);
        uvm_destroy_pgtbl(pgtbl);
        return 0;
    }
    vm_mappages(pgtbl, TRAPFRAME, trapframe, PGSIZE, PTE_R | PTE_W);
    
    return pgtbl;
}

/*
    第一个用户态进程的创建
    它的代码和数据位于initcode.h的initcode数组

    第一个进程的用户地址空间布局:
    trapoline   (1 page)
    trapframe   (1 page)
    ustack      (1 page)
    .......
                        <--heap_top
    code + data (1 page)
    empty space (1 page) 最低的4096字节 不分配物理页，同时不可访问

    UNUSED -> RUNNABLE
*/
void proc_make_first()
{
    proc_t* p;
    
    // 分配进程
    p = proc_alloc();
    if(p == 0)
        panic("proc_make_first: proc_alloc");
    
    proczero = p;
    
    // 第一个进程需要特殊处理：让它返回到 proc_forkret
    // proc_forkret 会释放调度器持有的锁，然后初始化文件系统
    extern void proc_forkret();
    p->ctx.ra = (uint64)proc_forkret;
    
    // 分配一个物理页并映射initcode
    uint64 pa = (uint64)pmem_alloc(false);
    if(pa == 0)
        panic("proc_make_first: pmem_alloc");
    
    // 映射initcode到虚拟地址PGSIZE
    memset((void*)pa, 0, PGSIZE);
    vm_mappages(p->pgtbl, 0, pa, PGSIZE, PTE_R | PTE_W | PTE_X | PTE_U);
    
    // 复制initcode到物理页
    memmove((void*)pa, initcode, initcode_len);
    
    // 设置heap_top: 堆顶初始在第二页之后，为堆预留增长空间
    // 第一页(0-PGSIZE)是代码，第二页及之后可用于堆
    p->heap_top = PGSIZE;
    
    // 分配用户栈 (1页) - 映射到较高地址，避免与堆冲突
    // 栈基址设置为一个较高的地址，为堆预留足够增长空间
    p->ustack_base = 0x10000;  // 64KB，给堆留出空间
    p->ustack_pages = 1;
    pa = (uint64)pmem_alloc(false);
    if(pa == 0)
        panic("proc_make_first: pmem_alloc for stack");
    memset((void*)pa, 0, PGSIZE);
    vm_mappages(p->pgtbl, p->ustack_base, pa, PGSIZE, PTE_R | PTE_W | PTE_U);

    printf("initcode:%p\n", initcode);
    // 准备trapframe，设置返回到用户态的初始状态
    p->tf->epc = 0;  // 用户程序计数器 - 指向地址0处的initcode开始
    p->tf->sp = p->ustack_base + PGSIZE;  // 用户栈指针 (栈顶)
    
    // 设置进程状态为RUNNABLE
    p->state = RUNNABLE;
    
    // 释放锁
    spinlock_release(&p->lk);
    
    // 切换到调度器
    proc_scheduler();
}

// 进程复制
// UNUSED -> RUNNABLE
int proc_fork()
{
    int pid;
    proc_t* np;
    proc_t* p = myproc();
    
    // 分配新进程
    if((np = proc_alloc()) == 0) {
        return -1;
    }
    
    // 复制页表和内存
    uvm_copy_pgtbl(p->pgtbl, np->pgtbl, p->heap_top, p->ustack_base, p->ustack_pages, p->mmap);
    np->heap_top = p->heap_top;
    np->ustack_base = p->ustack_base;
    np->ustack_pages = p->ustack_pages;
    
    // 复制mmap区域列表
    if(p->mmap) {
        // 这里需要复制mmap链表结构
        mmap_region_t* src = p->mmap;
        mmap_region_t* dst_head = NULL;
        mmap_region_t* dst_tail = NULL;
        
        while(src) {
            mmap_region_t* new_region = mmap_region_alloc();
            if(new_region == 0) {
                // 释放已分配的mmap区域 - 需要释放整个链表
                while(dst_head) {
                    mmap_region_t* next = dst_head->next;
                    mmap_region_free(dst_head);
                    dst_head = next;
                }
                proc_free(np);
                spinlock_release(&np->lk);
                return -1;
            }
            new_region->begin = src->begin;
            new_region->npages = src->npages;
            new_region->next = NULL;
            
            if(dst_head == NULL) {
                dst_head = new_region;
                dst_tail = new_region;
            } else {
                dst_tail->next = new_region;
                dst_tail = new_region;
            }
            src = src->next;
        }
        np->mmap = dst_head;
    } else {
        np->mmap = NULL;
    }
    
    // 复制trapframe
    *(np->tf) = *(p->tf);
    
    // 子进程fork返回0
    np->tf->a0 = 0;
    
    // 设置父进程
    np->parent = p;
    
    pid = np->pid;
    
    // 设置状态为RUNNABLE
    np->state = RUNNABLE;
    
    spinlock_release(&np->lk);
    
    return pid;
}

// 进程放弃CPU的控制权
// RUNNING -> RUNNABLE
void proc_yield()
{
    proc_t* p = myproc();
    spinlock_acquire(&p->lk);
    p->state = RUNNABLE;
    proc_sched();
    spinlock_release(&p->lk);
}

// 将退出的子进程的exit_state放入用户给的地址 addr
// 成功返回子进程pid，失败返回-1
int proc_wait(uint64 addr)
{
    proc_t* pp;
    int havekids, pid;
    proc_t* p = myproc();
    
    printf("[WAIT] pid=%d entering wait\n", p->pid);
    
    // 获取wait_lock
    spinlock_acquire(&wait_lock);
    
    for(;;) {
        // 扫描进程表寻找退出的子进程
        havekids = 0;
        for(pp = procs; pp < &procs[NPROC]; pp++) {
            // 确保是当前进程的子进程
            if(pp->parent == p) {
                // 确保子进程不在exit()或swtch()中
                spinlock_acquire(&pp->lk);
                
                havekids = 1;
                printf("[WAIT] pid=%d found child pid=%d, state=%d\n", p->pid, pp->pid, pp->state);
                
                if(pp->state == ZOMBIE) {
                    // 找到一个僵尸子进程
                    pid = pp->pid;
                    
                    printf("[WAIT] pid=%d found zombie child pid=%d\n", p->pid, pid);
                    
                    // 将exit_state复制到用户地址
                    if(addr != 0) {
                        uvm_copyout(p->pgtbl, addr, (uint64)&pp->exit_state, sizeof(pp->exit_state));
                    }
                    
                    // 释放子进程资源
                    proc_free(pp);
                    spinlock_release(&pp->lk);
                    spinlock_release(&wait_lock);
                    
                    printf("[WAIT] pid=%d returning with child pid=%d\n", p->pid, pid);
                    return pid;
                }
                spinlock_release(&pp->lk);
            }
        }
        
        // 如果没有子进程，返回-1
        if(!havekids) {
            printf("[WAIT] pid=%d no children, returning -1\n", p->pid);
            spinlock_release(&wait_lock);
            return -1;
        }
        
        printf("[WAIT] pid=%d going to sleep\n", p->pid);
        
        // 等待子进程退出
        // sleep会释放wait_lock并原子地让进程睡眠
        proc_sleep(p, &wait_lock);
        
        printf("[WAIT] pid=%d woke up, checking again\n", p->pid);
    }
}

// 父进程退出，子进程认proczero做父，因为它永不退出
static void proc_reparent(proc_t* parent)
{
    proc_t* pp;
    
    for(pp = procs; pp < &procs[NPROC]; pp++) {
        if(pp->parent == parent) {
            pp->parent = proczero;
            // 唤醒proczero以便它可以回收这些子进程
            proc_wakeup(proczero);
        }
    }
}

// 进程退出
void proc_exit(int exit_state)
{
    proc_t* p = myproc();
    
    assert(p != proczero, "proc_exit: proczero exit");
    
    // 获取wait_lock
    spinlock_acquire(&wait_lock);
    
    // 将子进程交给proczero
    proc_reparent(p);
    
    // 唤醒父进程（可能在wait中睡眠）
    proc_wakeup(p->parent);
    
    // 获取进程锁
    spinlock_acquire(&p->lk);
    
    p->exit_state = exit_state;
    p->state = ZOMBIE;
    
    // 释放wait_lock
    spinlock_release(&wait_lock);
    
    // 调度器不会再调度到这个进程
    // 跳入调度器，永不返回
    proc_sched();
    
    panic("proc_exit: zombie exit");
}

// 第一个进程第一次被调度后会执行这个函数
// 类似 xv6 的 forkret
void proc_forkret()
{
    static int first = 1;
    
    // 调度器仍然持有 p->lock，需要先释放它
    spinlock_release(&myproc()->lk);
    
    if (first) {
        // 文件系统初始化必须在进程上下文中运行（因为会调用sleep）
        // 所以不能在main()中运行
        first = 0;
        extern void fs_init(void);
        fs_init();
    }
    
    // 返回用户态
    extern void trap_user_return();
    trap_user_return();
}

// 进程切换到调度器
// ps: 调用者保证持有当前进程的锁
void proc_sched()
{
    proc_t* p = myproc();
    
    // 检查是否持有锁
    assert(spinlock_holding(&p->lk), "proc_sched: lock");
    
    // 检查中断是否关闭
    cpu_t* c = mycpu();
    assert(c->noff > 0, "proc_sched: interrupts enabled");
    
    // 检查状态不是RUNNING
    assert(p->state != RUNNING, "proc_sched: running");
    
    printf("[SCHED] pid=%d calling swtch, kstack=%p, sp=%p\n", p->pid, p->kstack, p->ctx.sp);
    
    // 切换到调度器上下文
    swtch(&p->ctx, &mycpu()->ctx);
    
    printf("[SCHED] pid=%d returned from swtch\n", p->pid);
}

// 调度器
void proc_scheduler()
{
    proc_t* p;
    cpu_t* c = mycpu();
    
    c->proc = 0;
    
    printf("[SCHEDULER] CPU%d starting scheduler\n", mycpuid());
    
    for(;;) {
        // 避免死锁：确保设备可以中断
        // 启用中断以便 VirtIO 等设备的中断处理程序可以唤醒睡眠的进程
        intr_on();
        
        // 遍历进程表寻找可运行的进程
        for(p = procs; p < &procs[NPROC]; p++) {
            spinlock_acquire(&p->lk);
            
            if(p->state == RUNNABLE) {
                printf("[SCHEDULER] CPU%d found RUNNABLE pid=%d\n", mycpuid(), p->pid);
                
                // 找到一个可运行的进程，切换到它
                p->state = RUNNING;
                c->proc = p;
                
                printf("[SCHEDULER] CPU%d switching to pid=%d, ra=%p, sp=%p\n", 
                       mycpuid(), p->pid, p->ctx.ra, p->ctx.sp);
                
                // 切换到进程上下文
                // 注意：进程会在某个时刻返回到这里，那时它仍然持有p->lk
                // 进程需要自己释放这个锁
                swtch(&c->ctx, &p->ctx);
                
                printf("[SCHEDULER] CPU%d returned from pid=%d\n", mycpuid(), p->pid);
                
                // 进程返回后，清除CPU的进程指针
                c->proc = 0;
            }
            
            spinlock_release(&p->lk);
        }
    }
}

// 进程睡眠在sleep_space
// 调用者必须持有lk锁，sleep会原子地释放lk并使进程睡眠
// 唤醒后重新获取lk
void proc_sleep(void* sleep_space, spinlock_t* lk)
{
    proc_t* p = myproc();
    
    printf("[SLEEP] pid=%d sleeping on %p\n", p->pid, sleep_space);
    
    // 必须获取p->lock才能修改p->state
    // 一旦我们持有p->lock，我们可以保证不会错过任何唤醒
    // （wakeup会锁p->lock），所以可以安全地释放lk
    spinlock_acquire(&p->lk);
    spinlock_release(lk);
    
    // 进入睡眠
    p->sleep_space = sleep_space;
    p->state = SLEEPING;
    
    proc_sched();
    
    int test_var = 42;
    printf("[SLEEP] pid=%d test_var=%d\n", p ? p->pid : -1, test_var);
    
    // 醒来后清理
    p->sleep_space = 0;
    
    printf("[SLEEP] pid=%d releasing p->lk\n", p->pid);
    
    // 重新获取原来的锁
    spinlock_release(&p->lk);
    
    printf("[SLEEP] pid=%d acquiring lk\n", p->pid);
    spinlock_acquire(lk);
    
    printf("[SLEEP] pid=%d done\n", p->pid);
}

// 唤醒所有在sleep_space沉睡的进程
// 必须在没有持有任何p->lock的情况下调用
void proc_wakeup(void* sleep_space)
{
    proc_t* p;
    
    // 获取返回地址以便追踪调用者
    uint64 ra;
    asm volatile("mv %0, ra" : "=r" (ra));
    printf("[WAKEUP] waking up processes on %p, caller=%p\n", sleep_space, ra);
    
    for(p = procs; p < &procs[NPROC]; p++) {
        if(p != myproc()) {
            spinlock_acquire(&p->lk);
            if(p->state == SLEEPING && p->sleep_space == sleep_space) {
                printf("[WAKEUP] waking up pid=%d\n", p->pid);
                p->state = RUNNABLE;
            }
            spinlock_release(&p->lk);
        }
    }
}