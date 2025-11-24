#include "mem/vmem.h"
#include "mem/pmem.h"
#include "memlayout.h"
#include "riscv.h"
#include "proc/cpu.h"
#include "lib/print.h"
#include "lib/str.h"

/*
 * 一个基于 SV39 的简洁页表辅助实现。
 */

// 内核使用的页表根指针
static pgtbl_t kernel_pgtbl;

extern char etext[]; // 链接器导出的符号：内核代码段结束地址

// 分配并初始化一个空的页表页
static pgtbl_t alloc_pgtbl(void)
{
    void *p = pmem_alloc(true); // page-table pages are kernel pages
    if (!p)
        return NULL;
    memset(p, 0, PGSIZE);
    return (pgtbl_t)p;
}

// 遍历页表并返回对应虚拟地址 va 的 PTE 指针。
// 当 alloc 为 true 时，会在需要时分配中间页表页。
pte_t* vm_getpte(pgtbl_t pgtbl, uint64 va, bool alloc)
{
    if (va >= VA_MAX)
        panic("vm_getpte: va >= VA_MAX");

    for (int level = 2; level > 0; level--) {
        pte_t *pte = &pgtbl[VA_TO_VPN(va, level)];
        if (*pte & PTE_V) {
            pgtbl = (pgtbl_t)PTE_TO_PA(*pte);
        } else {
            if (!alloc)
                return NULL;
            pgtbl_t new = alloc_pgtbl();
            if (!new)
                return NULL;
            *pte = PA_TO_PTE((uint64)new) | PTE_V;
            pgtbl = new;
        }
    }
    return &pgtbl[VA_TO_VPN(va, 0)];
}

// 将虚拟地址区间 [va, va+len) 映射到物理地址区间 [pa, pa+len)
// len 以字节为单位，可能未按页对齐。perm 使用 PTE_* 权限位。
// 如果 PTE 已经有效，则更新映射（允许 remap）
void vm_mappages(pgtbl_t pgtbl, uint64 va, uint64 pa, uint64 len, int perm)
{
    if (len == 0)
        panic("vm_mappages: len == 0");

    uint64 a = PG_ROUND_DOWN(va);
    uint64 last = PG_ROUND_DOWN(va + len - 1);

    for (;;) {
        pte_t *pte = vm_getpte(pgtbl, a, true);
        if (!pte)
            panic("vm_mappages: out of memory");
        
        // 允许更新已有映射（remap），不再 panic
        // if (*pte & PTE_V)
        //     panic("vm_mappages: remap");
        
        *pte = PA_TO_PTE(pa) | perm | PTE_V;
        if (a == last)
            break;
        a += PGSIZE;
        pa += PGSIZE;
    }
}

// 解除从 va 开始的 len 字节范围内的映射。如果 freeit 为 true，则同时释放对应物理页。
void vm_unmappages(pgtbl_t pgtbl, uint64 va, uint64 len, bool freeit)
{
    if ((va % PGSIZE) != 0)
        panic("vm_unmappages: not aligned");

    uint64 a;
    for (a = va; a < va + len; a += PGSIZE) {
        pte_t *pte = vm_getpte(pgtbl, a, false);
        if (!pte)
            panic("vm_unmappages: walk");
        if (!(*pte & PTE_V))
            panic("vm_unmappages: not mapped");
        if (PTE_FLAGS(*pte) == PTE_V)
            panic("vm_unmappages: not a leaf");
        if (freeit) {
            uint64 pa = PTE_TO_PA(*pte);
            // 自动检测页面属于内核区还是用户区
            uint64 kern_end = (uint64)ALLOC_BEGIN + KERNEL_PAGES * PGSIZE;
            bool in_kernel = (pa >= (uint64)ALLOC_BEGIN && pa < kern_end);
            pmem_free(pa, in_kernel);
        }
        *pte = 0;
    }
}

// 创建内核页表并映射必要的设备与内核区域
void kvm_init()
{
    kernel_pgtbl = alloc_pgtbl();
    if (!kernel_pgtbl)
        panic("kvm_init: alloc failed");

    // 映射 UART、virtio、PLIC 等设备的 MMIO 区域（使用 memlayout.h 中定义的地址）
    // 设备区域映射
    vm_mappages(kernel_pgtbl, UART_BASE, UART_BASE, PGSIZE, PTE_R | PTE_W);
    vm_mappages(kernel_pgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);
    vm_mappages(kernel_pgtbl, PLIC_BASE, PLIC_BASE, 0x400000, PTE_R | PTE_W);

    // 内核代码段：只读 + 可执行
    vm_mappages(kernel_pgtbl, KERNEL_BASE, KERNEL_BASE, (uint64)etext - KERNEL_BASE, PTE_R | PTE_X);
    // 内核数据段以及物理内存其余部分：可读 + 可写
    vm_mappages(kernel_pgtbl, (uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext, PTE_R | PTE_W);

    // 映射 trampoline 页（用于用户态和内核态切换）
    extern char trampoline[];  // defined in trampoline.S
    vm_mappages(kernel_pgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

    // 为进程 0 分配并映射内核栈
    // 注意：这里我们为第一个进程预分配内核栈的虚拟地址映射
    // 实际的物理页会在 proc_make_first() 中分配
    char *pa = pmem_alloc(true);
    if(pa == 0)
        panic("kvm_init: kstack alloc failed");
    uint64 va = KSTACK(0);
    vm_mappages(kernel_pgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
}

void kvm_inithart()
{
    sfence_vma();
    w_satp(MAKE_SATP(kernel_pgtbl));
    sfence_vma();
}

// 获取内核页表
pgtbl_t kvm_get_kernel_pgtbl()
{
    return kernel_pgtbl;
}

// 调试用：打印页表中非空的 PTE（简易版）
void vm_print(pgtbl_t pgtbl)
{
    // 遍历顶层页表条目并打印非空项
    for (int i = 0; i < 512; i++) {
        pte_t pte = pgtbl[i];
        if (pte & PTE_V) {
            printf("pte[%d]=%016lx\n", i, (unsigned long)pte);
        }
    }
}
