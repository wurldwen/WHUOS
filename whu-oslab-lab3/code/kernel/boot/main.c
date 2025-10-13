#include "riscv.h"
#include "lib/print.h"
#include "mem/pmem.h"
#include "lib/str.h"

volatile static int started = 0;

volatile static int over_1 = 0, over_2 = 0;

static int* mem[1024];

int main()
{
    int cpuid = r_tp();

    if(cpuid == 0) {

        print_init();
        pmem_init();

        printf("cpu %d is booting!\n", cpuid);
        __sync_synchronize();
        started = 1;

        for(int i = 0; i < 512; i++) {
            mem[i] = pmem_alloc(true);
            memset(mem[i], 1, PGSIZE);
            printf("mem = %p, data = %d\n", mem[i], mem[i][0]);
        }
        printf("cpu %d alloc over\n", cpuid);
        over_1 = 1;
        
        while(over_1 == 0 || over_2 == 0);
        
        for(int i = 0; i < 512; i++)
            pmem_free((uint64)mem[i], true);
        printf("cpu %d free over\n", cpuid);

    } else {

        while(started == 0);
        __sync_synchronize();
        printf("cpu %d is booting!\n", cpuid);
        
        for(int i = 512; i < 1024; i++) {
            mem[i] = pmem_alloc(true);
            memset(mem[i], 1, PGSIZE);
            printf("mem = %p, data = %d\n", mem[i], mem[i][0]);
        }
        printf("cpu %d alloc over\n", cpuid);
        over_2 = 1;

        while(over_1 == 0 || over_2 == 0);

        for(int i = 512; i < 1024; i++)
            pmem_free((uint64)mem[i], true);
        printf("cpu %d free over\n", cpuid);        
 
    }
    while (1);    
}

// #include "riscv.h"
// #include "lib/print.h"
// #include "lib/lock.h"
// #include "proc/proc.h"
// volatile static int started = 0;
// volatile static int sum = 0;
// static spinlock_t sum_lock;

// int main()
// {
//     int cpuid = r_tp();
//     if(cpuid == 0) {
//         cpu_init();
//         spinlock_init(&sum_lock, "sum");
//         print_init();
//         printf("cpu %d is booting!\n", cpuid);    
//         __sync_synchronize();
//         started = 1;
//         for(int i = 0; i < 10000; i++) {
//             spinlock_acquire(&sum_lock);
//             sum++;
//             spinlock_release(&sum_lock);
//         }
//         printf("cpu %d report: sum = %d\n", cpuid, sum);
//     } else {
//         while(started == 0);
//         __sync_synchronize();
//         printf("cpu %d is booting!\n", cpuid);
//         for(int i = 0; i < 10000; i++) {
//             spinlock_acquire(&sum_lock);
//             sum++;
//             spinlock_release(&sum_lock);
//         }
//         printf("cpu %d report: sum = %d\n", cpuid, sum);
//     }   
//     while (1);    
// }  
//为什么放临界区可能没输出，死锁了吗
