#include "lib/print.h"
#include "lib/str.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "proc/cpu.h"
#include "proc/initcode.h"
#include "memlayout.h"

// in trampoline.S
extern char trampoline[];

// in swtch.S
extern void swtch(context_t* old, context_t* new);

// in trap_user.c
extern void trap_user_return();


// 第一个进程
static proc_t proczero;

// 获得一个初始化过的用户页表
// 完成了trapframe 和 trampoline 的映射
pgtbl_t proc_pgtbl_init(uint64 trapframe)
{
    pgtbl_t pgtbl;
    uint64 page;
    
    // 分配一个空的页表
    page = pmem_alloc();
    if(page == 0) {
        return 0;
    }
    pgtbl = (pgtbl_t)page;
    memset((void*)pgtbl, 0, PGSIZE);
    
    // 映射 trampoline 页（用户态和内核态共享的跳板代码）
    // 映射到用户地址空间的最高处，不设置 PTE_U（用户不可访问）
    vm_mappages(pgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
    
    // 映射 trapframe 页（用于保存用户态寄存器）
    // 映射到 TRAMPOLINE 下面一页
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
*/
void proc_make_fisrt()
{
    uint64 page;
    
    // 显式初始化 proczero 结构为 0（重要！）
    memset(&proczero, 0, sizeof(proc_t));
    
    // pid 设置
    proczero.pid = 0;
    
    // 分配 trapframe 物理页
    page = pmem_alloc();
    assert(page != 0, "proc_make_first: trapframe alloc failed\n");
    proczero.tf = (trapframe_t*)page;
    memset(proczero.tf, 0, PGSIZE);
    
    // pagetable 初始化（包括 trampoline 和 trapframe 的映射）
    proczero.pgtbl = proc_pgtbl_init((uint64)proczero.tf);
    assert(proczero.pgtbl != 0, "proc_make_first: pgtbl init failed\n");
    
    // ustack 映射 + 设置 ustack_pages
    page = pmem_alloc();
    assert(page != 0, "proc_make_first: ustack alloc failed\n");
    memset((void*)page, 0, PGSIZE);
    // 用户栈从 PGSIZE 处开始向上增长（地址 PGSIZE 到 2*PGSIZE）
    vm_mappages(proczero.pgtbl, PGSIZE, page, PGSIZE, PTE_R | PTE_W | PTE_U);
    proczero.ustack_pages = 1;
    
    // data + code 映射（从地址 PGSIZE 开始，避开最低的 4096 字节）
    assert(initcode_len <= PGSIZE, "proc_make_first: initcode too big\n");
    page = pmem_alloc();
    assert(page != 0, "proc_make_first: code page alloc failed\n");
    memset((void*)page, 0, PGSIZE);
    // 将 initcode 复制到这个页面
    memmove((void*)page, (void*)initcode, initcode_len);
    // 映射代码页，从虚拟地址 PGSIZE 开始
    vm_mappages(proczero.pgtbl, PGSIZE, page, PGSIZE, PTE_R | PTE_W | PTE_X | PTE_U);
    
    // 设置 heap_top（代码页之后）
    proczero.heap_top = 2 * PGSIZE;
    
    // tf 字段设置
    proczero.tf->epc = PGSIZE;  // 用户程序从 PGSIZE 处开始执行
    proczero.tf->sp = 2 * PGSIZE;  // 用户栈指针指向栈底（向下增长）
    
    // 内核字段设置
    proczero.kstack = pmem_alloc();  // 分配内核栈
    assert(proczero.kstack != 0, "proc_make_first: kstack alloc failed\n");
    memset((void*)proczero.kstack, 0, PGSIZE);
    
    proczero.tf->kernel_satp = r_satp();  // 内核页表
    proczero.tf->kernel_sp = proczero.kstack + PGSIZE;  // 内核栈顶
    proczero.tf->kernel_trap = (uint64)trap_user_handler;  // 用户态 trap 处理函数
    proczero.tf->kernel_hartid = r_tp();  // 当前 CPU ID
    
    // 设置进程上下文，准备第一次调度
    memset(&proczero.ctx, 0, sizeof(context_t));
    proczero.ctx.ra = (uint64)trap_user_return;  // 返回到用户态
    proczero.ctx.sp = proczero.kstack + PGSIZE;  // 内核栈顶
    
    // 设置当前 CPU 的进程指针
    mycpu()->proc = &proczero;
    
    // 上下文切换：从内核调度器切换到第一个用户进程
    printf("Switching to proczero (first user process)...\n");
    swtch(&(mycpu()->ctx), &(proczero.ctx));
}
