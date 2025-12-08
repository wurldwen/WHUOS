#include "riscv.h"
#include "lib/print.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "mem/mmap.h"
#include "proc/cpu.h"
#include "proc/proc.h"
#include "trap/trap.h"
#include "dev/vio.h"   // 添加VirtIO设备头文件
#include "dev/plic.h"  // 添加PLIC头文件
#include "fs/fs.h"     // 添加文件系统头文件

volatile static int started = 0;

int main()
{
    int cpuid = r_tp();

    if(cpuid == 0) {
          print_init();
        // 初始化物理内存管理器
        pmem_init();
        // 初始化mmap区域管理器
        mmap_init();
       // 初始化内核虚拟内存（页表）
        kvm_init();
       // 初始化当前 hart 的虚拟内存
        kvm_inithart();
       // 初始化进程表
        proc_init();     
        // 初始化 CPU 结构
        cpu_init();
        // 初始化内核trap系统
        trap_kernel_init();
        trap_kernel_inithart();
        
        // 初始化PLIC (Platform-Level Interrupt Controller)
        plic_init();
        plic_inithart();  // Enable interrupts for this hart
        
        // 初始化VirtIO磁盘设备（必须在文件系统之前）
        virtio_disk_init();
        
        // 注意：文件系统初始化(fs_init)会在第一个进程上下文中执行
        // 因为它需要调用sleep，必须在进程环境中运行
        
        __sync_synchronize();
        started = 1;  // 允许其他CPU继续启动
        
        // 创建并切换到第一个用户进程
        // 注意：这个函数不会返回，它会直接切换到用户态
        proc_make_first();

    } else {
        // 其他CPU核心初始化
        while(started == 0);
        __sync_synchronize();
        
        // 其他CPU核心初始化虚拟内存和trap
        kvm_inithart();
        trap_kernel_inithart();
        
//        printf("CPU %d is ready!\n", cpuid);
    }

    // 其他CPU的主循环
    while (1) {
        // 空循环，等待调度
    }
}
