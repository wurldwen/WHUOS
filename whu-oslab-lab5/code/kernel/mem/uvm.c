#include "mem/mmap.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "proc/cpu.h"
#include "lib/print.h"
#include "lib/str.h"
#include "memlayout.h"
#include "riscv.h"

// 连续虚拟空间的复制(在uvm_copy_pgtbl中使用)
static void copy_range(pgtbl_t old, pgtbl_t new, uint64 begin, uint64 end)
{
    uint64 va, pa, page;
    int flags;
    pte_t* pte;

    for(va = begin; va < end; va += PGSIZE)
    {
        pte = vm_getpte(old, va, false);
        assert(pte != NULL, "uvm_copy_pgtbl: pte == NULL");
        assert((*pte) & PTE_V, "uvm_copy_pgtbl: pte not valid");
        
        pa = (uint64)PTE_TO_PA(*pte);
        flags = (int)PTE_FLAGS(*pte);

        page = (uint64)pmem_alloc(false);
        memmove((char*)page, (const char*)pa, PGSIZE);
        vm_mappages(new, va, page, PGSIZE, flags);
    }
}

// 两个 mmap_region 区域合并
// 保留一个 释放一个 不操作 next 指针
// 在uvm_munmap里使用
static void mmap_merge(mmap_region_t* mmap_1, mmap_region_t* mmap_2, bool keep_mmap_1)
{
    // 确保有效和紧临
    assert(mmap_1 != NULL && mmap_2 != NULL, "mmap_merge: NULL");
    assert(mmap_1->begin + mmap_1->npages * PGSIZE == mmap_2->begin, "mmap_merge: check fail");
    
    // merge
    if(keep_mmap_1) {
        mmap_1->npages += mmap_2->npages;
        mmap_region_free(mmap_2);
    } else {
        mmap_2->begin -= mmap_1->npages * PGSIZE;
        mmap_2->npages += mmap_1->npages;
        mmap_region_free(mmap_1);
    }
}

// 打印以 mmap 为首的 mmap 链
// for debug
void uvm_show_mmaplist(mmap_region_t* mmap)
{
    mmap_region_t* tmp = mmap;
    printf("\nmmap allocable area:\n");
    if(tmp == NULL)
        printf("NULL\n");
    while(tmp != NULL) {
        printf("allocable region: %p ~ %p\n", tmp->begin, tmp->begin + tmp->npages * PGSIZE);
        tmp = tmp->next;
    }
}

// 递归释放 页表占用的物理页 和 页表管理的物理页
// ps: 顶级页表level = 3, level = 0 说明是页表管理的物理页
static void destroy_pgtbl(pgtbl_t pgtbl, uint32 level)
{
    // level = 0: 这是叶子页表项，指向实际的物理页（用户数据）
    // level > 0: 这是页表本身
    
    if (level > 0) {
        // 遍历当前级别页表的所有条目
        for (int i = 0; i < 512; i++) {
            pte_t pte = pgtbl[i];
            
            // 如果页表项有效
            if (pte & PTE_V) {
                // 检查是否是指向下一级页表的指针（不是叶子节点）
                if (PTE_CHECK(pte)) {
                    // 这是指向下一级页表的指针
                    uint64 child = PTE_TO_PA(pte);
                    destroy_pgtbl((pgtbl_t)child, level - 1);
                    // 释放页表本身占用的物理页（页表都是内核页）
                    pmem_free(child, true);
                }
            }
        }
    } else {
        // level = 0: 释放数据页（用户页）
        for (int i = 0; i < 512; i++) {
            pte_t pte = pgtbl[i];
            if ((pte & PTE_V) && !PTE_CHECK(pte)) {
                // 这是叶子节点，释放数据页（用户页）
                uint64 pa = PTE_TO_PA(pte);
                pmem_free(pa, false);
            }
        }
    }
}

// 页表销毁：trapframe 和 trampoline 单独处理
void uvm_destroy_pgtbl(pgtbl_t pgtbl)
{
    // 销毁用户页表，但不包括 trapframe 和 trampoline
    // 因为 trapframe 和 trampoline 是内核管理的特殊页面
    
    // 首先，需要解除 trapframe 和 trampoline 的映射，避免被释放
    // 方法：直接将对应的 PTE 清零
    
    // 解除 trampoline 的映射（不释放物理页）
    pte_t* pte_trampoline = vm_getpte(pgtbl, TRAMPOLINE, false);
    if (pte_trampoline)
        *pte_trampoline = 0;
    
    // 解除 trapframe 的映射（不释放物理页）
    pte_t* pte_trapframe = vm_getpte(pgtbl, TRAPFRAME, false);
    if (pte_trapframe)
        *pte_trapframe = 0;
    
    // 递归销毁页表（从顶级页表开始，level=2）
    destroy_pgtbl(pgtbl, 2);
    
    // 最后释放顶级页表本身（内核页）
    pmem_free((uint64)pgtbl, true);
}

// 拷贝页表 (拷贝并不包括trapframe 和 trampoline)
void uvm_copy_pgtbl(pgtbl_t old, pgtbl_t new, uint64 heap_top, uint32 ustack_pages, mmap_region_t* mmap)
{
    /* step-1: USER_BASE ~ heap_top */
    // 从 PGSIZE 开始（跳过第一个页，避开最低的 4096 字节）
    if (heap_top > PGSIZE) {
        copy_range(old, new, PGSIZE, heap_top);
    }

    /* step-2: ustack */
    // 用户栈在 heap_top 之上
    if (ustack_pages > 0) {
        uint64 ustack_begin = heap_top;
        uint64 ustack_end = heap_top + ustack_pages * PGSIZE;
        copy_range(old, new, ustack_begin, ustack_end);
    }

    /* step-3: mmap_region */
    // 遍历 mmap 链表，复制每个 mmap 区域
    mmap_region_t* tmp = mmap;
    while (tmp != NULL) {
        uint64 begin = tmp->begin;
        uint64 end = begin + tmp->npages * PGSIZE;
        copy_range(old, new, begin, end);
        tmp = tmp->next;
    }
}

// 在用户页表和进程mmap链里 新增mmap区域 [begin, begin + npages * PGSIZE)
// 页面权限为perm
void uvm_mmap(uint64 begin, uint32 npages, int perm)
{
    if(npages == 0) return;
    assert(begin % PGSIZE == 0, "uvm_mmap: begin not aligned");

    // 获取当前进程
    proc_t* p = myproc();
    assert(p != NULL, "uvm_mmap: no current process");

    // 修改 mmap 链 (分情况的链式操作)
    // 需要找到合适的位置插入新的 mmap_region，保持链表按地址排序
    // 或者简单地插入到链表头部
    mmap_region_t* new_region = mmap_region_alloc();
    new_region->begin = begin;
    new_region->npages = npages;
    new_region->next = p->mmap;
    p->mmap = new_region;

    // 修改页表 (物理页申请 + 页表映射)
    for (uint32 i = 0; i < npages; i++) {
        uint64 va = begin + i * PGSIZE;
        uint64 pa = (uint64)pmem_alloc(false);  // 用户页
        assert(pa != 0, "uvm_mmap: out of memory");
        vm_mappages(p->pgtbl, va, pa, PGSIZE, perm | PTE_U);
    }
}

// 在用户页表和进程mmap链里释放mmap区域 [begin, begin + npages * PGSIZE)
void uvm_munmap(uint64 begin, uint32 npages)
{
    if(npages == 0) return;
    assert(begin % PGSIZE == 0, "uvm_munmap: begin not aligned");

    proc_t* p = myproc();
    assert(p != NULL, "uvm_munmap: no current process");
    
    uint64 end = begin + npages * PGSIZE;

    // new mmap_region 的产生
    // 需要在 mmap 链表中找到与 [begin, end) 重叠的区域
    // 然后进行相应的处理：分裂、删除、或调整
    
    mmap_region_t** prev_ptr = &(p->mmap);
    mmap_region_t* curr = p->mmap;
    
    while (curr != NULL) {
        uint64 curr_begin = curr->begin;
        uint64 curr_end = curr_begin + curr->npages * PGSIZE;
        
        // 检查是否有重叠
        if (curr_end <= begin || curr_begin >= end) {
            // 无重叠，继续下一个
            prev_ptr = &(curr->next);
            curr = curr->next;
            continue;
        }
        
        // 有重叠，需要处理
        if (curr_begin >= begin && curr_end <= end) {
            // 当前区域完全在释放范围内，删除整个区域
            *prev_ptr = curr->next;
            mmap_region_free(curr);
            curr = *prev_ptr;
        } else if (curr_begin < begin && curr_end > end) {
            // 当前区域包含释放范围，需要分裂成两个区域
            // [curr_begin, begin) 和 [end, curr_end)
            uint32 npages_before = (begin - curr_begin) / PGSIZE;
            uint32 npages_after = (curr_end - end) / PGSIZE;
            
            // 修改当前区域为前半部分
            curr->npages = npages_before;
            
            // 创建新区域为后半部分
            mmap_region_t* new_region = mmap_region_alloc();
            new_region->begin = end;
            new_region->npages = npages_after;
            new_region->next = curr->next;
            curr->next = new_region;
            
            // 继续处理下一个
            prev_ptr = &(new_region->next);
            curr = new_region->next;
        } else if (curr_begin < begin) {
            // 当前区域的后半部分在释放范围内
            curr->npages = (begin - curr_begin) / PGSIZE;
            prev_ptr = &(curr->next);
            curr = curr->next;
        } else {
            // 当前区域的前半部分在释放范围内
            uint32 npages_keep = (curr_end - end) / PGSIZE;
            curr->begin = end;
            curr->npages = npages_keep;
            prev_ptr = &(curr->next);
            curr = curr->next;
        }
    }

    // 尝试合并 mmap_region
    // 遍历链表，合并相邻的区域
    curr = p->mmap;
    while (curr != NULL && curr->next != NULL) {
        uint64 curr_end = curr->begin + curr->npages * PGSIZE;
        if (curr_end == curr->next->begin) {
            // 相邻，合并
            mmap_merge(curr, curr->next, true);
        } else {
            curr = curr->next;
        }
    }

    // 页表释放
    vm_unmappages(p->pgtbl, begin, npages * PGSIZE, true);
}

// 用户堆空间增加, 返回新的堆顶地址 (注意栈顶最大值限制)
// 在这里无需修正 p->heap_top
uint64 uvm_heap_grow(pgtbl_t pgtbl, uint64 heap_top, uint32 len)
{
    uint64 new_heap_top = heap_top + len;
    
    // 检查是否超过栈的位置（用户栈在 heap_top 之上）
    // 这里简单检查是否超过合理范围
    assert(new_heap_top < TRAPFRAME, "uvm_heap_grow: heap grows too large");
    
    // 计算需要分配的页面
    uint64 old_top_aligned = PG_ROUND_UP(heap_top);
    uint64 new_top_aligned = PG_ROUND_UP(new_heap_top);
    
    // 如果需要新的页面
    if (new_top_aligned > old_top_aligned) {
        uint32 npages = (new_top_aligned - old_top_aligned) / PGSIZE;
        for (uint32 i = 0; i < npages; i++) {
            uint64 va = old_top_aligned + i * PGSIZE;
            uint64 pa = (uint64)pmem_alloc(false);
            assert(pa != 0, "uvm_heap_grow: out of memory");
            memset((void*)pa, 0, PGSIZE);
            vm_mappages(pgtbl, va, pa, PGSIZE, PTE_R | PTE_W | PTE_U);
        }
    }

    return new_heap_top;
}

// 用户堆空间减少, 返回新的堆顶地址
// 在这里无需修正 p->heap_top
uint64 uvm_heap_ungrow(pgtbl_t pgtbl, uint64 heap_top, uint32 len)
{
    uint64 new_heap_top = heap_top - len;
    
    // 确保不会减到负数或低于代码段
    assert(new_heap_top >= PGSIZE, "uvm_heap_ungrow: heap shrinks too much");
    
    // 计算需要释放的页面
    uint64 new_top_aligned = PG_ROUND_UP(new_heap_top);
    uint64 old_top_aligned = PG_ROUND_UP(heap_top);
    
    // 如果有页面需要释放
    if (new_top_aligned < old_top_aligned) {
        uint64 release_size = old_top_aligned - new_top_aligned;
        vm_unmappages(pgtbl, new_top_aligned, release_size, true);
    }

    return new_heap_top;
}

// 用户态地址空间[src, src+len) 拷贝至 内核态地址空间[dst, dst+len)
// 注意: src dst 不一定是 page-aligned
void uvm_copyin(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
    uint64 n, va, pa;
    
    while (len > 0) {
        // 获取当前虚拟地址所在页的起始地址
        va = PG_ROUND_DOWN(src);
        
        // 获取对应的物理地址
        pte_t* pte = vm_getpte(pgtbl, va, false);
        assert(pte != NULL, "uvm_copyin: pte is NULL");
        assert(*pte & PTE_V, "uvm_copyin: page not present");
        
        pa = PTE_TO_PA(*pte);
        
        // 计算在当前页内可以拷贝的字节数
        n = PGSIZE - (src - va);
        if (n > len)
            n = len;
        
        // 从物理地址拷贝到内核地址
        memmove((void*)dst, (void*)(pa + (src - va)), n);
        
        len -= n;
        dst += n;
        src += n;
    }
}

// 内核态地址空间[src, src+len） 拷贝至 用户态地址空间[dst, dst+len)
void uvm_copyout(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
    uint64 n, va, pa;
    
    while (len > 0) {
        // 获取当前虚拟地址所在页的起始地址
        va = PG_ROUND_DOWN(dst);
        
        // 获取对应的物理地址
        pte_t* pte = vm_getpte(pgtbl, va, false);
        assert(pte != NULL, "uvm_copyout: pte is NULL");
        assert(*pte & PTE_V, "uvm_copyout: page not present");
        
        pa = PTE_TO_PA(*pte);
        
        // 计算在当前页内可以拷贝的字节数
        n = PGSIZE - (dst - va);
        if (n > len)
            n = len;
        
        // 从内核地址拷贝到物理地址
        memmove((void*)(pa + (dst - va)), (void*)src, n);
        
        len -= n;
        dst += n;
        src += n;
    }
}

// 用户态字符串拷贝到内核态
// 最多拷贝maxlen字节, 中途遇到'\0'则终止
// 注意: src dst 不一定是 page-aligned
void uvm_copyin_str(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 maxlen)
{
    uint64 n, va, pa;
    char* dst_ptr = (char*)dst;
    bool found_null = false;
    
    while (maxlen > 0 && !found_null) {
        // 获取当前虚拟地址所在页的起始地址
        va = PG_ROUND_DOWN(src);
        
        // 获取对应的物理地址
        pte_t* pte = vm_getpte(pgtbl, va, false);
        assert(pte != NULL, "uvm_copyin_str: pte is NULL");
        assert(*pte & PTE_V, "uvm_copyin_str: page not present");
        
        pa = PTE_TO_PA(*pte);
        
        // 计算在当前页内可以拷贝的字节数
        n = PGSIZE - (src - va);
        if (n > maxlen)
            n = maxlen;
        
        // 逐字节拷贝，直到遇到'\0'或达到n
        char* src_ptr = (char*)(pa + (src - va));
        for (uint64 i = 0; i < n; i++) {
            *dst_ptr = *src_ptr;
            if (*src_ptr == '\0') {
                found_null = true;
                break;
            }
            dst_ptr++;
            src_ptr++;
            maxlen--;
            src++;
        }
        
        if (!found_null && n == (PGSIZE - (src - va))) {
            // 到达页边界但还没找到'\0'，继续下一页
            continue;
        }
    }
    
    // 如果没有遇到'\0'但已经到达maxlen，手动添加'\0'
    if (!found_null && maxlen == 0) {
        *(char*)(dst + maxlen - 1) = '\0';
    }
}