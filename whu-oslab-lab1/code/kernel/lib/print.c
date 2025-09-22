// 标准输出和报错机制
#include <stdarg.h>
#include "lib/print.h"
#include "lib/lock.h"
#include "dev/uart.h"

// 标记系统是否已进入恐慌状态，用于冻结其他 CPU 的 UART 输出
volatile int panicked = 0;

// 打印锁，用于保护打印操作的线程安全
static spinlock_t print_lk;

// 数字字符数组，用于将数字转换为字符
static char digits[] = "0123456789abcdef";

// 初始化打印模块
void print_init(void)
{
    uart_init();
    spinlock_init(&print_lk, "print");
}

// 格式化打印函数，仅支持 %d, %x, %p, %s 格式
void printf(const char *fmt, ...)
{
    va_list ap;  // 可变参数列表
    int i, c;    // 循环变量和当前字符
    char *s;     // 字符串指针

    va_start(ap, fmt);  // 初始化可变参数
    spinlock_acquire(&print_lk);  // 获取打印锁

    // 遍历格式字符串
    for (i = 0; (c = fmt[i] & 0xff) != 0; i++) {
        if (c != '%') {
            if (c == '\n') {
                uart_putc_sync('\r');
            }
            uart_putc_sync(c);  // 输出普通字符
            continue;
        }
        c = fmt[++i] & 0xff;  // 获取格式字符
        if (c == 0)
            break;
        switch (c) {
        case 'd': {  // 十进制整数
            int xx = va_arg(ap, int);
            int base = 10, sign = 1;  // 基数和符号标志
            char buf[16];  // 缓冲区
            int j;
            uint32 x;

            if (sign && (sign = xx < 0))  // 处理负数
                x = -xx;
            else
                x = xx;

            j = 0;
            do {
                buf[j++] = digits[x % base];  // 转换为字符
            } while ((x /= base) != 0);

            if (sign)
                buf[j++] = '-';  // 添加负号

            while (--j >= 0)
                uart_putc_sync(buf[j]);  // 输出缓冲区内容
            break;
        }
        case 'x': {  // 十六进制整数
            int xx = va_arg(ap, int);
            int base = 16;  // 无符号
            char buf[16];
            int j;
            uint32 x = xx;

            j = 0;
            do {
                buf[j++] = digits[x % base];
            } while ((x /= base) != 0);

            while (--j >= 0)
                uart_putc_sync(buf[j]);
            break;
        }
        case 'p': {  // 指针
            uint64 x = va_arg(ap, uint64);
            int j;
            uart_putc_sync('0');
            uart_putc_sync('x');
            for (j = 0; j < (sizeof(uint64) * 2); j++, x <<= 4)
                uart_putc_sync(digits[x >> (sizeof(uint64) * 8 - 4)]);
            break;
        }
        case 's':  // 字符串
            if ((s = va_arg(ap, char*)) == 0)
                s = "(null)";  // 空指针处理
            for (; *s; s++)
                uart_putc_sync(*s);  // 输出字符串
            break;
        case '%':  // 百分号转义
            uart_putc_sync('%');
            break;
        default:
            // 输出未知格式序列以引起注意
            uart_putc_sync('%');
            uart_putc_sync(c);
            break;
        }
    }

    spinlock_release(&print_lk);  // 释放打印锁
    va_end(ap);  // 结束可变参数
}

// 恐慌函数，输出错误信息并进入死循环
void panic(const char *s)
{
    spinlock_acquire(&print_lk);
    printf("panic: ");
    printf(s);
    printf("\n");
    panicked = 1; // 冻结其他 CPU 的 UART 输出
    for (;;)
        ;  // 死循环
}

// 断言函数，如果条件不满足则触发恐慌
void assert(bool condition, const char* warning)
{
    if (!condition) {
        panic(warning);
    }
}