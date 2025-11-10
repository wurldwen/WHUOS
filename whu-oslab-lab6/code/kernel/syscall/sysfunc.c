#include "proc/cpu.h"
#include "proc/proc.h"
#include "mem/vmem.h"
#include "mem/pmem.h"
#include "mem/mmap.h"
#include "lib/str.h"
#include "lib/print.h"
#include "dev/timer.h"
#include "syscall/sysfunc.h"
#include "syscall/syscall.h"
#include "riscv.h"

// 堆伸缩
// uint64 new_heap_top 新的堆顶 (如果是0代表查询, 返回旧的堆顶)
// 成功返回新的堆顶 失败返回-1
uint64 sys_brk()
{
    uint64 new_heap_top;
    arg_uint64(0, &new_heap_top);
    
    proc_t* p = myproc();
    uint64 old_heap_top = p->heap_top;
    
    // 如果new_heap_top为0，返回当前堆顶
    if(new_heap_top == 0) {
        return old_heap_top;
    }
    
    // 堆增长
    if(new_heap_top > old_heap_top) {
        uint64 result = uvm_heap_grow(p->pgtbl, old_heap_top, new_heap_top - old_heap_top);
        if(result == 0) {
            return -1;
        }
        p->heap_top = result;
        return result;
    }
    // 堆缩小
    else if(new_heap_top < old_heap_top) {
        uint64 result = uvm_heap_ungrow(p->pgtbl, old_heap_top, old_heap_top - new_heap_top);
        p->heap_top = result;
        return result;
    }
    
    // 相等，直接返回
    return old_heap_top;
}

// 内存映射
// uint64 start 起始地址 (如果为0则由内核自主选择一个合适的起点)
// uint32 len   范围(字节, 检查是否是page-aligned)
// 成功返回映射空间的起始地址, 失败返回-1
uint64 sys_mmap()
{
    uint64 start;
    uint32 len;
    
    arg_uint64(0, &start);
    arg_uint32(1, &len);
    
    // 检查是否是页对齐
    if(len % PGSIZE != 0) {
        return -1;
    }
    
    uvm_mmap(start, len / PGSIZE, PTE_R | PTE_W | PTE_U);
    
    // 如果start为0，uvm_mmap会选择合适的地址
    // 这里简化处理，返回start或成功标志
    return start;
}

// 取消内存映射
// uint64 start 起始地址
// uint32 len   范围(字节, 检查是否是page-aligned)
// 成功返回0 失败返回-1
uint64 sys_munmap()
{
    uint64 start;
    uint32 len;
    
    arg_uint64(0, &start);
    arg_uint32(1, &len);
    
    // 检查是否是页对齐
    if(len % PGSIZE != 0) {
        return -1;
    }
    
    uvm_munmap(start, len / PGSIZE);
    
    return 0;
}

// 打印字符
// uint64 addr
uint64 sys_print()
{
    uint64 addr;
    arg_uint64(0, &addr);
    
    proc_t* p = myproc();
    char buf[256];
    
    // 从用户空间复制字符串
    uvm_copyin_str(p->pgtbl, (uint64)buf, addr, sizeof(buf));
    
    // 打印字符串
    printf("%s", buf);
    
    return 0;
}

// 进程复制
uint64 sys_fork()
{
    return proc_fork();
}

// 进程等待
// uint64 addr  子进程退出时的exit_state需要放到这里 
uint64 sys_wait()
{
    uint64 addr;
    arg_uint64(0, &addr);
    
    return proc_wait(addr);
}

// 进程退出
// int exit_state
uint64 sys_exit()
{
    uint32 exit_state;
    arg_uint32(0, &exit_state);
    
    proc_exit(exit_state);
    
    return 0;  // 不会到达这里
}

extern timer_t sys_timer;

// 进程睡眠一段时间
// uint32 second 睡眠时间
// 成功返回0, 失败返回-1
uint64 sys_sleep()
{
    uint32 seconds;
    arg_uint32(0, &seconds);
    
    uint64 target_tick = sys_timer.ticks + seconds;
    
    // 睡眠直到目标时刻
    while(sys_timer.ticks < target_tick) {
        proc_sleep(&sys_timer, &sys_timer.lk);
    }
    
    return 0;
}