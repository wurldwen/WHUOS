// 物理内存分配器：使用两个 alloc_region_t 池（内核 / 用户）。
// 每个空闲的物理页在页首存放一个 page_node_t，用于链表连接，
// 每个区域维护一个哨兵链表头。

#include "mem/pmem.h"
#include "lib/lock.h"
#include "lib/str.h"
#include "lib/print.h"
#include "riscv.h"
// 物理页节点
typedef struct page_node {
  struct page_node* next;
} page_node_t;

// 若干物理页组成一个可分配区域
typedef struct alloc_region {
  uint64 begin; // 起始物理地址（包含）
  uint64 end;   // 终止物理地址（不包含）
  spinlock_t lk; // 自旋锁，保护下面的链表和计数器
  uint32 allocable;   // 可分配页面计数
  page_node_t list_head; // 哨兵链表头节点（list_head.next 指向第一个可用页）
} alloc_region_t;

// 内核和用户可分配的物理页分开管理
static alloc_region_t kern_region, user_region;

// 辅助函数：将一页放入区域的空闲链表（假定 pa 已按 PGSIZE 对齐）
static void
region_free_page(alloc_region_t *r, uint64 pa)
{
  page_node_t *n = (page_node_t*)pa;

  // 基本校验：对齐且在区域范围内
  if ((pa % PGSIZE) != 0 || pa < r->begin || pa >= r->end)
    panic("region_free_page: bad page\n");

  // 用垃圾数据填充页面，帮助捕捉悬空引用
  //memset((void*)pa, 1, PGSIZE);

  // 将页加入链表头（临界区）
  spinlock_acquire(&r->lk);
  n->next = r->list_head.next;
  r->list_head.next = n;
  r->allocable++;
  spinlock_release(&r->lk);
}

// 辅助函数：从区域空闲链表弹出一页，若无可分配页返回 0
static uint64
region_alloc_page(alloc_region_t *r)
{
  page_node_t *n;
  uint64 pa = 0;

  // 临界区：从链表头取出节点
  spinlock_acquire(&r->lk);
  n = r->list_head.next;
  if (n) {
    r->list_head.next = n->next;
    r->allocable--;
    pa = (uint64)n;
  }
  spinlock_release(&r->lk);

  // 分配时用另一个模式填充页面，便于调试
  //if (pa)
    //memset((void*)pa, 5, PGSIZE);
  return pa;
}

// 初始化区域结构
static void
init_region(alloc_region_t *r, uint64 b, uint64 e, const char *name)
{
  r->begin = b;
  r->end = e;
  // 初始化自旋锁并置空链表/计数器
  spinlock_init(&r->lk, (char*)name);
  r->allocable = 0;
  r->list_head.next = NULL;
}

// 将 [pa_start, pa_end) 范围内按页对齐的页面加入区域空闲链表
static void
freerange_region(alloc_region_t *r, uint64 pa_start, uint64 pa_end)
{
  uint64 p = PG_ROUND_UP(pa_start);
  for (; p + PGSIZE <= pa_end; p += PGSIZE)
    region_free_page(r, p);
}

// 公共接口
void
pmem_init(void)
{
  uint64 a = (uint64)ALLOC_BEGIN;
  uint64 b = (uint64)0x88000000;

  if (a >= b)
    panic("pmem_init: bad ALLOC range\n");

  // 按 KERNEL_PAGES 划分：前 KERNEL_PAGES 页给内核，其余给用户
  uint64 total_bytes = b - a;
  if (total_bytes % PGSIZE != 0)
    panic("pmem_init: ALLOC range not page-aligned\n");

  uint64 total_pages = total_bytes / PGSIZE;
  if (total_pages < (uint64)KERNEL_PAGES)
    panic("pmem_init: ALLOC range too small for kernel pages\n");

  uint64 kern_pages = (uint64)KERNEL_PAGES;
  uint64 kern_bytes = kern_pages * PGSIZE;
  uint64 mid = a + kern_bytes; // [a, mid) -> kernel, [mid, b) -> user

  init_region(&kern_region, a, mid, "pmem_k");
  init_region(&user_region, mid, b, "pmem_u");

  // 将每个区域的页面加入对应空闲链表
  freerange_region(&kern_region, kern_region.begin, kern_region.end);
  freerange_region(&user_region, user_region.begin, user_region.end);
}

// 分配一页，in_kernel=true 表示从内核区分配，否则从用户区分配
void*
pmem_alloc(bool in_kernel)
{
  uint64 pa = in_kernel ? region_alloc_page(&kern_region)
                        : region_alloc_page(&user_region);
  return (void*)pa;
}

// 释放一页到指定区域（由 in_kernel 指定）
void
pmem_free(uint64 page, bool in_kernel)
{
  alloc_region_t *r = in_kernel ? &kern_region : &user_region;

  // 基本校验：对齐且属于该区域
  if ((page % PGSIZE) != 0 || page < r->begin || page >= r->end) {
    panic("pmem_free: invalid page or wrong region\n");
  }

  region_free_page(r, page);
}
