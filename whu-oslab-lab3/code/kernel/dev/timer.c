#include "lib/lock.h"
#include "lib/print.h"
#include "dev/timer.h"
#include "memlayout.h"
#include "riscv.h"

/*-------------------- 工作在M-mode --------------------*/

// in trap.S M-mode时钟中断处理流程()
extern void timer_vector();

// 每个CPU在时钟中断中需要的临时空间(考虑为什么可以这么写)
static uint64 mscratch[NCPU][5];

// 时钟初始化
// called in start.c
void timer_init()
{
    // 获取当前CPU的hartid
    int hartid = r_mhartid();

    // 设置第一次时钟中断的时间
    // CLINT_MTIMECMP(hartid) = CLINT_MTIME + INTERVAL
    *(uint64*)CLINT_MTIMECMP(hartid) = *(uint64*)CLINT_MTIME + INTERVAL;

    // 准备timer_vector需要的信息
    // mscratch[0..2]: timer_vector保存寄存器的空间
    // mscratch[3]: CLINT_MTIMECMP(hartid)的地址
    // mscratch[4]: 时钟中断间隔INTERVAL
    uint64 *scratch = &mscratch[hartid][0];
    scratch[3] = CLINT_MTIMECMP(hartid);
    scratch[4] = INTERVAL;
    w_mscratch((uint64)scratch);

    // 设置M-mode trap处理函数为timer_vector
    w_mtvec((uint64)timer_vector);

    // 使能M-mode中断
    w_mstatus(r_mstatus() | MSTATUS_MIE);

    // 使能M-mode时钟中断
    w_mie(r_mie() | MIE_MTIE);
}


/*--------------------- 工作在S-mode --------------------*/

// 系统时钟
static timer_t sys_timer;

// 时钟创建(初始化系统时钟)
void timer_create()
{
    // 初始化系统时钟
    sys_timer.ticks = 0;
    spinlock_init(&sys_timer.lk, "timer");
}

// 时钟更新(ticks++ with lock)
void timer_update()
{
    spinlock_acquire(&sys_timer.lk);
    sys_timer.ticks++;
    spinlock_release(&sys_timer.lk);
}

// 返回系统时钟ticks
uint64 timer_get_ticks()
{
    uint64 ticks;
    spinlock_acquire(&sys_timer.lk);
    ticks = sys_timer.ticks;
    spinlock_release(&sys_timer.lk);
    return ticks;
}