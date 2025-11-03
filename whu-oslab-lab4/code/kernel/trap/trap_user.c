#include "lib/print.h"
#include "trap/trap.h"
#include "proc/cpu.h"
#include "mem/vmem.h"
#include "memlayout.h"
#include "riscv.h"

// in trampoline.S
extern char trampoline[];      // 内核和用户切换的代码
extern char user_vector[];     // 用户触发trap进入内核
extern char user_return[];     // trap处理完毕返回用户

// in trap.S
extern char kernel_vector[];   // 内核态trap处理流程

// in trap_kernel.c
extern char* interrupt_info[16]; // 中断错误信息
extern char* exception_info[16]; // 异常错误信息

// 在user_vector()里面调用
// 用户态trap处理的核心逻辑
void trap_user_handler()
{
    uint64 sepc = r_sepc();          // 记录了发生异常时的pc值
    uint64 sstatus = r_sstatus();    // 与特权模式和中断相关的状态信息
    uint64 scause = r_scause();      // 引发trap的原因
    uint64 stval = r_stval();        // 发生trap时保存的附加信息(不同trap不一样)
    proc_t* p = myproc();

    // 确认trap来自U-mode
    assert((sstatus & SSTATUS_SPP) == 0, "trap_user_handler: not from u-mode");

    // 判断是中断还是异常
    if(scause & (1UL << 63)) {
        // 中断
        uint64 cause = scause & 0xFF;
        printf("[User Trap] Interrupt: %s\n", interrupt_info[cause]);
    } else {
        // 异常
        uint64 cause = scause & 0xFF;
        
        // 处理系统调用 (ecall from U-mode)
        if(cause == 8) {
            // 系统调用
            printf("[User Trap] System call from user mode\n");
            // sepc 指向 ecall 指令，需要跳过它（4字节）
            p->tf->epc += 4;
        } else {
            // 其他异常
            printf("[User Trap] Exception: %s\n", exception_info[cause]);
            printf("  sepc = 0x%lx, stval = 0x%lx\n", sepc, stval);
        }
    }
    
    // 返回用户态
    trap_user_return();
}

// 调用user_return()
// 内核态返回用户态
void trap_user_return()
{
    proc_t* p = myproc();
    
    // 关中断，避免在切换页表时被打断
    intr_off();
    
    // 设置 stvec 指向 trampoline 中的 user_vector
    // TRAMPOLINE 是用户页表中 trampoline 代码的虚拟地址
    w_stvec(TRAMPOLINE + ((uint64)user_vector - (uint64)trampoline));
    
    // 设置 trapframe 的值，准备返回用户态
    p->tf->kernel_satp = r_satp();              // 保存内核页表
    p->tf->kernel_sp = p->kstack + PGSIZE;      // 保存内核栈指针
    p->tf->kernel_trap = (uint64)trap_user_handler;  // 保存trap处理函数
    p->tf->kernel_hartid = r_tp();              // 保存CPU ID
    
    // 切换到用户页表
    uint64 satp = MAKE_SATP(p->pgtbl);
    
    // 调用 trampoline.S 中的 user_return
    // 它会恢复用户态寄存器并执行 sret 返回用户态
    // 参数：用户页表的 SATP 值，trapframe 的虚拟地址
    ((void (*)(uint64, uint64))((uint64)user_return - (uint64)trampoline + TRAMPOLINE))
        (satp, TRAPFRAME);
}
