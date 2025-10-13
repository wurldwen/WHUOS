#include "riscv.h"
#include "lib/print.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "lib/str.h"
volatile static int started = 0;
int main()
{
    int cpuid = r_tp();

    if(cpuid == 0) {

        print_init();
        pmem_init();
        kvm_init();
        kvm_inithart();

        printf("cpu %d is booting!\n", cpuid);
        __sync_synchronize();
        // started = 1;

        pgtbl_t test_pgtbl = pmem_alloc(true);
        uint64 mem[5];
        for(int i = 0; i < 5; i++) {
            mem[i] = (uint64)pmem_alloc(false);
            printf("mem[%d] = %p\n", i, (void*)mem[i]);
        }

        printf("\ntest-1\n\n");    
        vm_mappages(test_pgtbl, 0, mem[0], PGSIZE, PTE_R);
        vm_mappages(test_pgtbl, PGSIZE * 10, mem[1], PGSIZE / 2, PTE_R | PTE_W);
        vm_mappages(test_pgtbl, PGSIZE * 512, mem[2], PGSIZE - 1, PTE_R | PTE_X);
        vm_mappages(test_pgtbl, PGSIZE * 512 * 512, mem[2], PGSIZE, PTE_R | PTE_X);
        vm_mappages(test_pgtbl, VA_MAX - PGSIZE, mem[4], PGSIZE, PTE_W);
        vm_print(test_pgtbl);

        printf("\ntest-2\n\n");    
        vm_mappages(test_pgtbl, 0, mem[0], PGSIZE, PTE_W);
        vm_unmappages(test_pgtbl, PGSIZE * 10, PGSIZE, true);
        vm_unmappages(test_pgtbl, PGSIZE * 512, PGSIZE, true);
        vm_print(test_pgtbl);

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
