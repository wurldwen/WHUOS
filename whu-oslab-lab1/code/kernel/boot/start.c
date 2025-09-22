#include "riscv.h"

//为每一个核设置初始启动时的C语言栈帧空间
__attribute__ ((aligned (16))) uint8 CPU_stack[4096 * NCPU];

void main();

// M-Mode下时钟中断响应程序
extern void timervec();

// entry.S jumps here in machine mode on stack0.
void
start()
{
  // 设置旧模式为Supervisor Mode，方便mret返回
  unsigned long x = r_mstatus();
  x &= ~MSTATUS_MPP_MASK;
  x |= MSTATUS_MPP_S;
  w_mstatus(x);

  // 为mret指令设置 M Exception Program Counter 寄存器值为 main函数地址
  w_mepc((uint64)main);

  // 禁用分页机制
  w_satp(0);

  // 把中断和异常委派到Supervisor Mode
  w_medeleg(0xffff);
  w_mideleg(0xffff);
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);

  // CPU的hartid存入tp寄存器，方便S-Mode读取
  int id = r_mhartid();
  w_tp(id);

  // mret返回并切换模式到Supervisor Mode，执行main.c：main函数
  asm volatile("mret");
}

