#include "sys.h"

int main()
{
    syscall(SYS_print, "Hello from user mode!\n");
    syscall(SYS_print, "User process is running!\n");
    
    // 简单的死循环，避免退出
    while(1);
    
    return 0;
}
