#include "proc/cpu.h"
#include "riscv.h"

static cpu_t cpus[NCPU];

void cpu_init(void)
{
    // 显式初始化所有 CPU 结构
    for(int i = 0; i < NCPU; i++) {
        cpus[i].noff = 0;
        cpus[i].origin = 0;
    }
}

cpu_t* mycpu(void)
{
    int id = r_tp();
    return &cpus[id];
}

int mycpuid(void) 
{
    return r_tp();
}

proc_t* myproc(void)
{
    push_off();
    cpu_t* c = mycpu();
    proc_t* p = c->proc;
    pop_off();
    return p;
}
}