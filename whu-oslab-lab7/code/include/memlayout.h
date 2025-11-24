/* memory leyout */
#ifndef __MEMLAYOUT_H__
#define __MEMLAYOUT_H__

// 内核基地址
#define KERNEL_BASE 0x80000000ul
#define PHYSTOP (KERNEL_BASE + 128*1024*1024)

// UART 相关
#define UART_BASE  0x10000000ul
#define UART_IRQ   10

// UART
#define VIRTIO0 0x10001000
#define VIRTIO0_IRQ 1

// platform-level interrupt controller(PLIC)
#define PLIC_BASE 0x0c000000ul
#define PLIC_PRIORITY(id) (PLIC_BASE + (id) * 4)
#define PLIC_PENDING (PLIC_BASE + 0x1000)
#define PLIC_MENABLE(hart) (PLIC_BASE + 0x2000 + (hart)*0x100)
#define PLIC_SENABLE(hart) (PLIC_BASE + 0x2080 + (hart)*0x100)
#define PLIC_MPRIORITY(hart) (PLIC_BASE + 0x200000 + (hart)*0x2000)
#define PLIC_SPRIORITY(hart) (PLIC_BASE + 0x201000 + (hart)*0x2000)
#define PLIC_MCLAIM(hart) (PLIC_BASE + 0x200004 + (hart)*0x2000)
#define PLIC_SCLAIM(hart) (PLIC_BASE + 0x201004 + (hart)*0x2000)

// core local interruptor(CLINT)
#define CLINT_BASE 0x2000000ul
#define CLINT_MSIP(hartid) (CLINT_BASE + 4 * (hartid))
#define CLINT_MTIMECMP(hartid) (CLINT_BASE + 0x4000 + 8 * (hartid))
#define CLINT_MTIME (CLINT_BASE + 0xBFF8)

// 用户地址空间布局
// one beyond the highest possible virtual address.
// MAXVA is actually one bit less than the max allowed by
// Sv39, to avoid having to sign-extend virtual addresses
// that have the high bit set.
#define MAXVA (1L << (9 + 9 + 9 + 12 - 1))

// map the trampoline page to the highest address,
// in both user and kernel space.
#define TRAMPOLINE (MAXVA - PGSIZE)

// map the trapframe page just below TRAMPOLINE, for trampoline.S
#define TRAPFRAME (TRAMPOLINE - PGSIZE)

// map kernel stacks beneath the trapframe, each surrounded by invalid guard pages
// KSTACK(p) computes the virtual address of process p's kernel stack
// For simplicity in Lab 4, we only support process 0, so we can use a fixed address
#define KSTACK(p) (TRAPFRAME - ((p)+1)* 2*PGSIZE)

#define VIRTIO_BASE 0x10001000ul
#define VIRTIO_IRQ 1

#endif
