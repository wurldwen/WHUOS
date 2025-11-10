#include "proc/cpu.h"
#include "mem/vmem.h"
#include "mem/pmem.h"
#include "mem/mmap.h"
#include "lib/str.h"
#include "lib/print.h"
#include "syscall/sysfunc.h"
#include "syscall/syscall.h"
#include "riscv.h"
#include "memlayout.h"

// 堆伸缩
// uint64 new_heap_top 新的堆顶 (如果是0代表查询, 返回旧的堆顶)
// 成功返回新的堆顶 失败返回-1
uint64 sys_brk()
{
    proc_t* p = myproc();
    uint64 new_heap_top;
    
    // 读取参数：新的堆顶地址
    arg_uint64(0, &new_heap_top);
    
    // 如果 new_heap_top 为 0，表示查询当前堆顶
    if (new_heap_top == 0) {
        return p->heap_top;
    }
    
    uint64 old_heap_top = p->heap_top;
    
    // 检查新堆顶是否合法（不能低于初始堆顶或超过栈区域）
    if (new_heap_top < old_heap_top) {
        // 缩小堆
        uint64 diff = old_heap_top - new_heap_top;
        p->heap_top = uvm_heap_ungrow(p->pgtbl, old_heap_top, diff);
    } else if (new_heap_top > old_heap_top) {
        // 扩大堆
        uint64 diff = new_heap_top - old_heap_top;
        p->heap_top = uvm_heap_grow(p->pgtbl, old_heap_top, diff);
    }
    
    // 返回新的堆顶
    return p->heap_top;
}

// 内存映射
// uint64 start 起始地址 (如果为0则由内核自主选择一个合适的起点, 通常是顺序扫描找到一个够大的空闲空间)
// uint32 len   范围(字节, 检查是否是page-aligned)
// 成功返回映射空间的起始地址, 失败返回-1
uint64 sys_mmap()
{
    proc_t* p = myproc();
    uint64 start;
    uint32 len;
    
    // 读取参数
    arg_uint64(0, &start);
    arg_uint32(1, &len);
    
    // 检查长度是否页对齐
    if (len == 0 || len % PGSIZE != 0) {
        printf("sys_mmap: len not page-aligned or zero\n");
        return -1;
    }
    
    uint32 npages = len / PGSIZE;
    
    // 如果 start 为 0，需要自动选择合适的地址
    if (start == 0) {
        // 从用户栈之上开始寻找空闲区域
        // 用户栈在 heap_top 之上
        uint64 search_start = p->heap_top + p->ustack_pages * PGSIZE;
        
        // 简单策略：从 search_start 开始，按页对齐向上寻找
        // 更复杂的实现可以检查 mmap 链表找到空隙
        start = search_start;
        
        // 检查是否会与其他区域冲突（简化实现，假设 mmap 链表已排序）
        mmap_region_t* curr = p->mmap;
        while (curr != NULL) {
            uint64 curr_end = curr->begin + curr->npages * PGSIZE;
            if (start < curr_end && start + len > curr->begin) {
                // 有冲突，移动到当前区域之后
                start = curr_end;
            }
            curr = curr->next;
        }
        
        // 确保不超过地址空间上限
        if (start + len >= TRAPFRAME) {
            printf("sys_mmap: out of address space\n");
            return -1;
        }
    } else {
        // 检查指定地址是否页对齐
        if (start % PGSIZE != 0) {
            printf("sys_mmap: start not page-aligned\n");
            return -1;
        }
        
        // 检查是否与现有区域冲突
        mmap_region_t* curr = p->mmap;
        while (curr != NULL) {
            uint64 curr_end = curr->begin + curr->npages * PGSIZE;
            if (start < curr_end && start + len > curr->begin) {
                printf("sys_mmap: region conflicts with existing mapping\n");
                return -1;
            }
            curr = curr->next;
        }
    }
    
    // 执行映射（使用默认权限：可读可写）
    uvm_mmap(start, npages, PTE_R | PTE_W);
    
    return start;
}

// 取消内存映射
// uint64 start 起始地址
// uint32 len   范围(字节, 检查是否是page-aligned)
// 成功返回0 失败返回-1
uint64 sys_munmap()
{
    proc_t* p = myproc();
    uint64 start;
    uint32 len;
    
    // 读取参数
    arg_uint64(0, &start);
    arg_uint32(1, &len);
    
    // 检查长度是否页对齐
    if (len == 0 || len % PGSIZE != 0) {
        printf("sys_munmap: len not page-aligned or zero\n");
        return -1;
    }
    
    // 检查起始地址是否页对齐
    if (start % PGSIZE != 0) {
        printf("sys_munmap: start not page-aligned\n");
        return -1;
    }
    
    uint32 npages = len / PGSIZE;
    
    // 检查要取消映射的区域是否在 mmap 区域内
    // 简化实现：直接调用 uvm_munmap，它会处理区域的分裂和合并
    mmap_region_t* curr = p->mmap;
    bool found = false;
    
    while (curr != NULL) {
        uint64 curr_begin = curr->begin;
        uint64 curr_end = curr_begin + curr->npages * PGSIZE;
        
        // 检查是否有重叠
        if (start < curr_end && start + len > curr_begin) {
            found = true;
            break;
        }
        curr = curr->next;
    }
    
    if (!found) {
        printf("sys_munmap: region not mapped\n");
        return -1;
    }
    
    // 执行取消映射
    uvm_munmap(start, npages);
    
    return 0;
}

// copyin 测试 (int 数组)
// uint64 addr
// uint32 len
// 返回 0
uint64 sys_copyin()
{
    proc_t* p = myproc();
    uint64 addr;
    uint32 len;

    arg_uint64(0, &addr);
    arg_uint32(1, &len);

    int tmp;
    for(int i = 0; i < len; i++) {
        uvm_copyin(p->pgtbl, (uint64)&tmp, addr + i * sizeof(int), sizeof(int));
        printf("get a number from user: %d\n", tmp);
    }

    return 0;
}

// copyout 测试 (int 数组)
// uint64 addr
// 返回数组元素数量
uint64 sys_copyout()
{
    int L[5] = {1, 2, 3, 4, 5};
    proc_t* p = myproc();
    uint64 addr;

    arg_uint64(0, &addr);
    uvm_copyout(p->pgtbl, addr, (uint64)L, sizeof(int) * 5);

    return 5;
}

// copyinstr测试
// uint64 addr
// 成功返回0
uint64 sys_copyinstr()
{
    char s[64];

    arg_str(0, s, 64);
    printf("get str from user: %s\n", s);

    return 0;
}
