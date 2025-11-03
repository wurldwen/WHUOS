#include "riscv.h"
#include "lib/print.h"
#include "dev/uart.h"
#include "dev/plic.h"
#include "trap/trap.h"

volatile static int started = 0;
int main()
{
    int cpuid = r_tp();

    if(cpuid == 0) {
        // CPU 0: 主核心初始化
        print_init();

        printf("\n=== WHU OS Lab 3: External Interrupt Test ===\n");
        printf("Testing UART external interrupts...\n\n");

        // 初始化中断系统（但还不使能UART中断）
        plic_init();              // 初始化PLIC中断控制器
        trap_kernel_init();       // 初始化内核trap系统
        trap_kernel_inithart();   // 初始化当前核心的trap
        plic_inithart();          // 初始化当前核心的PLIC

        printf("PLIC initialized\n");
        printf("Trap system initialized\n");

        // 使能系统中断
        intr_on();
        printf("System interrupts enabled\n");

        // 在系统中断使能后再初始化UART（避免中断堆积）
        uart_init();              // 初始化UART串口并使能UART中断
        printf("UART initialized\n\n");

        printf("CPU %d is ready!\n", cpuid);
        printf("=== UART External Interrupt Test ===\n");
        printf("Please type characters to test UART interrupt.\n");
        printf("Each character you type will trigger an external interrupt.\n");
        printf("Press Ctrl+A then X to exit QEMU.\n\n");
        
        __sync_synchronize();
        started = 1;  // 允许其他CPU继续启动

    } else {
        // 其他CPU核心初始化
        while(started == 0);
        __sync_synchronize();
        
        // 其他CPU核心也需要初始化trap和plic
        trap_kernel_inithart();   // 初始化当前核心的trap
        plic_inithart();          // 初始化当前核心的PLIC
        
        // 使能中断
        intr_on();
        
        printf("CPU %d is ready!\n", cpuid);
    }

    // 主循环：等待中断
    while (1) {
        // 可以在这里添加其他测试代码
        // 中断会自动被处理
    }
}
