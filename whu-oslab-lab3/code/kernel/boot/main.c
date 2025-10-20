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

        printf("\n=== WHU OS Lab 3: Interrupt Test ===\n");
        printf("Initializing devices and interrupt system...\n\n");

        // 初始化设备和中断系统
        uart_init();              // 初始化UART串口
        plic_init();              // 初始化PLIC中断控制器
        trap_kernel_init();       // 初始化内核trap系统
        trap_kernel_inithart();   // 初始化当前核心的trap
        plic_inithart();          // 初始化当前核心的PLIC
        
        printf("UART initialized\n");
        printf("PLIC initialized\n");
        printf("Trap system initialized\n");

        // 使能中断
        intr_on();
        printf("Interrupts enabled\n\n");

        printf("CPU %d is ready!\n", cpuid);
        printf("Waiting for timer interrupts...\n");
        printf("(You can also type characters to test UART interrupt)\n\n");
        
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
