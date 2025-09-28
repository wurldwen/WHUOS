#ifndef __PMEM_H__
#define __PMEM_H__

#include "common.h"

// 来自kernel.ld
extern char KERNEL_DATA[];
extern char ALLOC_BEGIN[];
extern char ALLOC_END[];

void  pmem_init(void);
void* pmem_alloc(bool in_kernel);
void  pmem_free(uint64 page, bool in_kernel);

// 内核占用的物理页数（可通过在编译时用 -DKERNEL_PAGES=N 覆盖）
#ifndef KERNEL_PAGES
#define KERNEL_PAGES 6
#endif

#endif