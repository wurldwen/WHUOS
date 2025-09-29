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
        printf("ALLOC_BEGIN: %p, ALLOC_END: %p\n", ALLOC_BEGIN, ALLOC_END);

        printf("cpu %d is booting!\n", cpuid);
        __sync_synchronize();
        started = 1;

        for(int i = 0; i < 512; i++) {
            mem[i] = pmem_alloc(true);
            //memset(mem[i], 1, PGSIZE);
            //printf("mem = %p, data = %d\n", mem[i], mem[i][0]);
        }
        printf("cpu %d alloc over\n", cpuid);
        
    //    over_1 = 1;
        
    //    while(over_1 == 0 || over_2 == 0);
        
        for(int i = 0; i < 512; i++) {
            pmem_free((uint64)mem[i], true);
            //printf("cpuid=%d free mem[%d] = %p\n", cpuid, i, mem[i]);
            
        }
            
        printf("cpu %d free over\n", cpuid);

    } else {

        while(started == 0);
        __sync_synchronize();
        printf("cpu %d is booting!\n", cpuid);
        
        for(int i = 512; i < 1024; i++) {
            mem[i] = pmem_alloc(true);
            //memset(mem[i], 1, PGSIZE);
            //printf("mem = %p, data = %d\n", mem[i], mem[i][0]);
        }
        printf("cpu %d alloc over\n", cpuid);
        
    //    over_2 = 1;

    //    while(over_1 == 0 || over_2 == 0);

        for(int i = 512; i < 1024; i++)
            pmem_free((uint64)mem[i], true);
        printf("cpu %d free over\n", cpuid);        
 
    }
    while (1);    
}

// #include "riscv.h"
// #include "lib/print.h"
// #include "lib/lock.h"
// //volatile static int started = 0;
// volatile static int sum = 0;
// static spinlock_t sum_lock;

// int main()
// {
//     int cpuid = r_tp();
//     spinlock_init(&sum_lock, "sum");
//     if(cpuid == 0) {
//         print_init();
//         printf("cpu %d is booting!\n", cpuid);    
//         __sync_synchronize();
//         for(int i = 0; i < 10000; i++) {
//             /* 在临界区保护对全局 sum 的更新（注释版） */
//             spinlock_acquire(&sum_lock);
//             sum++;
//             spinlock_release(&sum_lock);
//         }
//         printf("cpu %d report: sum = %d\n", cpuid, sum);
//         //started = 1;
//     } else {
//         //while(started == 0);
//         __sync_synchronize();
//         printf("cpu %d is booting!\n", cpuid);
//         //spinlock_acquire(&sum_lock);
//         for(int i = 0; i < 10000; i++) {
//             spinlock_acquire(&sum_lock);
//             sum++;
//             spinlock_release(&sum_lock);
//         }
//         //spinlock_release(&sum_lock);
//         printf("cpu %d report: sum = %d\n", cpuid, sum);
//     }   
//     while (1);    
// }  
//为什么放临界区可能没输出，死锁了吗
