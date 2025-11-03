#include "sys.h"

// start() is the entry point for the first user process
// The linker script will set this as the entry point at address 0x1000
void start()
{
    syscall(SYS_print);
    syscall(SYS_print);
    while(1);
}