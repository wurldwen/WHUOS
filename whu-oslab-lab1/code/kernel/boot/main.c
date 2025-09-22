#include "riscv.h"
#include "lib/print.h"

volatile static int started = 0;

int main()
{
    int cpuid = r_tp();
    if(cpuid == 0) {
        print_init();
        printf("cpu %d is booting!\n", cpuid);        
        __sync_synchronize();
        started = 1;
    } else {
        while(started == 0);
        __sync_synchronize();
        printf("cpu %d is booting!\n", cpuid);
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
//         for(int i = 0; i < 900; i++) {
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
//         for(int i = 0; i < 900; i++) {
//             spinlock_acquire(&sum_lock);
//             sum++;
//             spinlock_release(&sum_lock);
//         }
//         //spinlock_release(&sum_lock);
//         printf("cpu %d report: sum = %d\n", cpuid, sum);
//     }   
//     while (1);    
// }  

