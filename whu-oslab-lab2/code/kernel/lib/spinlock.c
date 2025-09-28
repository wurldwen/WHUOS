#include "lib/lock.h"
#include "lib/print.h"
#include "proc/proc.h"
#include "riscv.h"

// 带层数叠加的关中断
void push_off(void)
{
  int old = intr_get();

  intr_off();
  if(mycpu()->noff == 0)
    mycpu()->origin = old;
  mycpu()->noff += 1;
}

// 带层数叠加的开中断
void pop_off(void)
{
    cpu_t *c = mycpu();
    if(intr_get())
        panic("pop_off - interruptible");
    if(c->noff < 1)
        panic("pop_off");
    c->noff -= 1;
    if(c->noff == 0 && c->origin)
        intr_on();
}

// 是否持有自旋锁
// 中断应当是关闭的
bool spinlock_holding(spinlock_t *lk)
{
  int r;
  r = (lk->locked && lk->cpuid == mycpuid());
  return r;
}

// 自旋锁初始化
void spinlock_init(spinlock_t *lk, char *name)
{
  lk->name = name;
  lk->locked = 0;
  lk->cpuid = 0;
}

// 获取自旋锁
void spinlock_acquire(spinlock_t *lk)
{    
  push_off(); // 禁用中断以避免死锁。
  if(spinlock_holding(lk))
    panic("acquire");

  // 在 RISC-V 上，sync_lock_test_and_set 转换为原子交换：
  //   a5 = 1
  //   s1 = &lk->locked
  //   amoswap.w.aq a5, a5, (s1)
  while(__sync_lock_test_and_set(&lk->locked, 1) != 0)
    ;

  // 告诉 C 编译器和处理器不要移动加载或存储
  // 超过此点，以确保临界区的内存
  // 引用严格在锁获取后发生。
  // 在 RISC-V 上，这会发出一个 fence 指令。
  __sync_synchronize();

  // 记录锁获取的信息，用于 holding() 和调试。
  lk->cpuid = mycpuid();
} 

// 释放自旋锁
void spinlock_release(spinlock_t *lk)
{
  if(!spinlock_holding(lk))
    panic("release");

  lk->cpuid = 0;

  // 告诉 C 编译器和 CPU 不要移动加载或存储
  // 超过此点，以确保临界区中的所有存储
  // 在锁释放之前对其他 CPU 可见，
  // 并且临界区中的加载严格在
  // 锁释放之前发生。
  // 在 RISC-V 上，这会发出一个 fence 指令。
  __sync_synchronize();

  // 释放锁，等价于 lk->locked = 0。
  // 此代码不使用 C 赋值，因为 C 标准
  // 暗示赋值可能使用
  // 多条存储指令实现。
  // 在 RISC-V 上，sync_lock_release 转换为原子交换：
  //   s1 = &lk->locked
  //   amoswap.w zero, zero, (s1)
  __sync_lock_release(&lk->locked);

  pop_off();
}