#include "riscv.h"
#include "lib/print.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "proc/cpu.h"
#include "proc/proc.h"
#include "trap/trap.h"

volatile static int started = 0;

int main()
{
    int cpuid = r_tp();

    if(cpuid == 0) {
        // CPU 0: 主核心初始化
        print_init();

        printf("\n=== WHU OS Lab 4: First User Process ===\n");
        printf("Initializing system...\n\n");

        // 初始化物理内存管理器
        pmem_init();
        printf("Physical memory initialized\n");
        
        // 初始化内核虚拟内存（页表）
        kvm_init();
        printf("Kernel virtual memory initialized\n");
        
        // 初始化当前 hart 的虚拟内存
        kvm_inithart();
        printf("Kernel VM enabled for hart %d\n", cpuid);
        
        // 初始化 CPU 结构
        cpu_init();
        printf("CPU structures initialized\n");
        
        // 初始化内核trap系统
        trap_kernel_init();
        trap_kernel_inithart();
        printf("Trap system initialized\n");
        
        printf("\nSystem initialization complete.\n");
        printf("Creating first user process (proczero)...\n\n");
        
        __sync_synchronize();
        started = 1;  // 允许其他CPU继续启动
        
        // 创建并切换到第一个用户进程
        // 注意：这个函数不会返回，它会直接切换到用户态
        proc_make_fisrt();

    } else {
        // 其他CPU核心初始化
        while(started == 0);
        __sync_synchronize();
        
        // 其他CPU核心初始化虚拟内存和trap
        kvm_inithart();
        trap_kernel_inithart();
        
        printf("CPU %d is ready!\n", cpuid);
    }

    // 其他CPU的主循环
    while (1) {
        // 空循环，等待调度
    }
}
