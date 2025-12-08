#ifndef __ELF_H__
#define __ELF_H__

#include "common.h"

// ELF可执行文件格式

#define ELF_MAGIC 0x464C457FU  // "\x7FELF" 小端序

// ELF文件头
typedef struct elfhdr {
    uint32 magic;      // 必须等于 ELF_MAGIC
    uint8  elf[12];
    uint16 type;
    uint16 machine;
    uint32 version;
    uint64 entry;      // 程序入口点
    uint64 phoff;      // 程序头表偏移
    uint64 shoff;      // 节头表偏移
    uint32 flags;
    uint16 ehsize;
    uint16 phentsize;
    uint16 phnum;      // 程序头表项数
    uint16 shentsize;
    uint16 shnum;
    uint16 shstrndx;
} elfhdr_t;

// 程序段头
typedef struct proghdr {
    uint32 type;
    uint32 flags;
    uint64 off;        // 段在文件中的偏移
    uint64 vaddr;      // 段的虚拟地址
    uint64 paddr;      // 段的物理地址
    uint64 filesz;     // 段在文件中的大小
    uint64 memsz;      // 段在内存中的大小
    uint64 align;      // 对齐
} proghdr_t;

// 程序头类型值
#define ELF_PROG_LOAD           1

// 程序头标志位
#define ELF_PROG_FLAG_EXEC      1
#define ELF_PROG_FLAG_WRITE     2
#define ELF_PROG_FLAG_READ      4

#endif
