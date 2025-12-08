#include "proc/cpu.h"
#include "mem/vmem.h"
#include "mem/pmem.h"
#include "mem/mmap.h"
#include "lib/str.h"
#include "lib/print.h"
#include "dev/timer.h"
#include "syscall/sysfunc.h"
#include "syscall/syscall.h"
#include "riscv.h"
#include "fs/dir.h"
#include "fs/inode.h"
#include "fs/elf.h"

// 将ELF段标志转换为页表权限
static int flags2perm(int flags)
{
    int perm = 0;
    if(flags & ELF_PROG_FLAG_EXEC)
        perm = PTE_X;
    if(flags & ELF_PROG_FLAG_WRITE)
        perm |= PTE_W;
    if(flags & ELF_PROG_FLAG_READ)
        perm |= PTE_R;
    return perm;
}

// 加载一个程序段到页表的虚拟地址va处
// va必须页对齐，且从va到va+sz的页面必须已经映射
// 成功返回0，失败返回-1
static int loadseg(pgtbl_t pgtbl, uint64 va, inode_t* ip, uint32 offset, uint32 sz)
{
    uint32 i, n;
    uint64 pa;
    
    for(i = 0; i < sz; i += PGSIZE) {
        // 获取虚拟地址对应的物理地址
        pte_t* pte = vm_getpte(pgtbl, va + i, false);
        if(pte == NULL || (*pte & PTE_V) == 0)
            return -1;
        pa = PTE_TO_PA(*pte);
        
        // 计算本次要读取的字节数
        if(sz - i < PGSIZE)
            n = sz - i;
        else
            n = PGSIZE;
        
        // 从inode读取数据到物理地址
        if(inode_read_data(ip, offset + i, n, (void*)pa, false) != n)
            return -1;
    }
    
    return 0;
}

// 执行一个ELF文件
// char* path: 可执行文件路径
// char** argv: 参数数组
// 成功返回argc，失败返回-1
uint64 sys_exec()
{
    char path[DIR_PATH_LEN];
    uint64 argv_addr;
    char* argv[ELF_MAXARGS];
    uint64 ustack[ELF_MAXARGS];
    int i;
    elfhdr_t elf;
    inode_t* ip = NULL;
    proghdr_t ph;
    pgtbl_t pgtbl = NULL, oldpgtbl;
    uint64 sz = 0, sp, stackbase;
    int argc = 0;  // 初始化为0
    proc_t* p = myproc();
    
    // 获取参数：路径和argv数组指针
    arg_str(0, path, DIR_PATH_LEN);
    arg_uint64(1, &argv_addr);
    
    // 从用户空间复制argv数组
    for(i = 0; i < ELF_MAXARGS; i++) {
        uint64 arg_ptr;
        uvm_copyin(p->pgtbl, (uint64)&arg_ptr, argv_addr + i * sizeof(uint64), sizeof(uint64));
        if(arg_ptr == 0)
            break;
        argv[i] = (char*)pmem_alloc(false);
        if(argv[i] == NULL)
            goto bad;
        uvm_copyin_str(p->pgtbl, (uint64)argv[i], arg_ptr, PGSIZE);
    }
    argc = i;
    
    // 打开可执行文件
    ip = path_to_inode(path);
    if(ip == NULL)
        goto bad;
    
    inode_lock(ip);
    
    // 检查ELF头
    if(inode_read_data(ip, 0, sizeof(elf), &elf, false) != sizeof(elf))
        goto bad;
    
    if(elf.magic != ELF_MAGIC)
        goto bad;
    
    // 创建新的页表
    pgtbl = proc_pgtbl_init((uint64)p->tf);
    if(pgtbl == NULL)
        goto bad;
    
    // 加载程序段到内存
    for(i = 0; i < elf.phnum; i++) {
        uint32 off = elf.phoff + i * sizeof(ph);
        if(inode_read_data(ip, off, sizeof(ph), &ph, false) != sizeof(ph))
            goto bad;
        
        if(ph.type != ELF_PROG_LOAD)
            continue;
        
        if(ph.memsz < ph.filesz)
            goto bad;
        if(ph.vaddr + ph.memsz < ph.vaddr)
            goto bad;
        if(ph.vaddr % PGSIZE != 0)
            goto bad;
        
        // 分配内存并映射
        uint64 vaddr = ph.vaddr;
        uint64 end = PG_ROUND_UP(ph.vaddr + ph.memsz);
        
        for(uint64 va = PG_ROUND_DOWN(vaddr); va < end; va += PGSIZE) {
            void* pa = pmem_alloc(false);
            if(pa == NULL)
                goto bad;
            memset(pa, 0, PGSIZE);
            vm_mappages(pgtbl, va, (uint64)pa, PGSIZE, flags2perm(ph.flags) | PTE_U);
        }
        
        sz = end;
        
        // 加载段内容
        if(loadseg(pgtbl, ph.vaddr, ip, ph.off, ph.filesz) < 0)
            goto bad;
    }
    
    inode_unlock_free(ip);
    ip = NULL;
    
    // 分配用户栈（2页：第一页作为保护页，第二页作为栈）
    sz = PG_ROUND_UP(sz);
    for(i = 0; i < 2; i++) {
        void* pa = pmem_alloc(false);
        if(pa == NULL)
            goto bad;
        memset(pa, 0, PGSIZE);
        vm_mappages(pgtbl, sz + i * PGSIZE, (uint64)pa, PGSIZE, PTE_W | PTE_R | PTE_U);
    }
    
    sp = sz + 2 * PGSIZE;
    stackbase = sz + PGSIZE;
    
    // 将参数字符串压入栈
    for(i = argc - 1; i >= 0; i--) {
        sp -= strlen(argv[i]) + 1;
        sp -= sp % 16;  // RISC-V栈必须16字节对齐
        if(sp < stackbase)
            goto bad;
        uvm_copyout(pgtbl, sp, (uint64)argv[i], strlen(argv[i]) + 1);
        ustack[i] = sp;
    }
    
    // 压入argv指针数组
    ustack[argc] = 0;
    sp -= (argc + 1) * sizeof(uint64);
    sp -= sp % 16;
    if(sp < stackbase)
        goto bad;
    uvm_copyout(pgtbl, sp, (uint64)ustack, (argc + 1) * sizeof(uint64));
    
    // 释放临时分配的内存
    for(i = 0; i < argc; i++)
        pmem_free((uint64)argv[i], false);
    
    // 设置参数到寄存器
    // argc 通过返回值传递（a0）
    p->tf->a1 = sp;  // argv指针数组的地址
    
    // 提交到用户镜像
    oldpgtbl = p->pgtbl;
    p->pgtbl = pgtbl;
    p->heap_top = sz;
    p->ustack_base = stackbase;
    p->ustack_pages = 1;
    p->tf->epc = elf.entry;  // 初始程序计数器 = main
    p->tf->sp = sp;           // 初始栈指针
    
    // 释放旧页表
    uvm_destroy_pgtbl(oldpgtbl);
    
    return argc;  // 返回值会到a0，即main的第一个参数argc
    
bad:
    if(pgtbl)
        uvm_destroy_pgtbl(pgtbl);
    if(ip) {
        inode_unlock_free(ip);
    }
    for(i = 0; i < argc; i++)
        pmem_free((uint64)argv[i], false);
    return -1;
}