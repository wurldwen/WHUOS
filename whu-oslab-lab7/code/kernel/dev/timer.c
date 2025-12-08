#include "lib/lock.h"
#include "lib/print.h"
#include "proc/proc.h"
#include "dev/timer.h"
#include "memlayout.h"
#include "riscv.h"

/*-------------------- 工作在M-mode --------------------*/

// in trap.S M-mode时钟中断处理流程()
extern void timer_vector();

// 每个CPU在时钟中断中需要的临时空间
static uint64 mscratch[NCPU][5];

// 时钟初始化
void timer_init()
{
    // 获取当前cpuid
    int hartid = r_tp();

    // 一开始设置 cmp_time  = cur_time + time interval
    // 之后每触发一次时钟中断 有 cmp_time += time interval
    *(uint64*)CLINT_MTIMECMP(hartid) = *(uint64*)CLINT_MTIME + INTERVAL;

    // 指向当前CPU的mscratch, 与trap.S里的timer_vector密切配合
    uint64* scratch = mscratch[hartid];
    scratch[3] = CLINT_MTIMECMP(hartid);
    scratch[4] = INTERVAL;
    w_mscratch((uint64)scratch);

    // 设置M-mode时钟中断处理函数
    w_mtvec((uint64)timer_vector);

    // M-mode中断使能(总开关)
    w_mstatus(r_mstatus() | MSTATUS_MIE);

    // M-mode中断使能(时钟中断开关)
    w_mie(r_mie() | MIE_MTIE);
}



/*--------------------- 工作在S-mode --------------------*/

// 系统时钟
timer_t sys_timer;

// 时钟创建
void timer_create()
{
    sys_timer.ticks = 0;
    spinlock_init(&sys_timer.lk, "sys_timer");
}

// 时钟更新
void timer_update()
{
    spinlock_acquire(&sys_timer.lk);
    sys_timer.ticks++;
    proc_wakeup(&sys_timer.ticks);
    // printf("ticks = %d\n", sys_timer.ticks);
    spinlock_release(&sys_timer.lk);
}

uint64 timer_get_ticks()
{
    spinlock_acquire(&sys_timer.lk);
    uint64 ret = sys_timer.ticks; 
    spinlock_release(&sys_timer.lk);
    return ret;
}