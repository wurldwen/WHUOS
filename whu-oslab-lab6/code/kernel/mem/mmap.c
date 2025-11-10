#include "lib/print.h"
#include "lib/str.h"
#include "lib/lock.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "mem/mmap.h"

// 包装 mmap_region_t 用于仓库组织
typedef struct mmap_region_node {
    mmap_region_t mmap;
    struct mmap_region_node* next;
} mmap_region_node_t;

#define N_MMAP 256

// mmap_region_node_t 仓库(单向链表) + 指向链表头节点的指针 + 保护仓库的锁
static mmap_region_node_t list_mmap_region_node[N_MMAP];
static mmap_region_node_t* list_head;
static spinlock_t list_lk;

// 初始化上述三个数据结构
void mmap_init()
{
    // 初始化自旋锁
    spinlock_init(&list_lk, "mmap_list");
    
    // 初始化链表: 将所有 mmap_region_node 连成一个单向链表
    // list_head 指向第一个节点(索引0), 这个节点被保留，不会被分配出去
    list_head = &list_mmap_region_node[0];
    
    for (int i = 0; i < N_MMAP - 1; i++) {
        list_mmap_region_node[i].next = &list_mmap_region_node[i + 1];
    }
    list_mmap_region_node[N_MMAP - 1].next = NULL;
}

// 从仓库申请一个 mmap_region_t
// 若申请失败则 panic
// 注意: list_head 保留, 不会被申请出去
mmap_region_t* mmap_region_alloc()
{
    spinlock_acquire(&list_lk);
    
    // list_head 保留，从第二个节点开始分配
    if (list_head->next == NULL) {
        spinlock_release(&list_lk);
        panic("mmap_region_alloc: out of mmap regions");
    }
    
    // 取出 list_head 的下一个节点
    mmap_region_node_t* node = list_head->next;
    list_head->next = node->next;
    
    spinlock_release(&list_lk);
    
    // 返回节点内部的 mmap_region_t 部分
    return &(node->mmap);
}

// 向仓库归还一个 mmap_region_t
void mmap_region_free(mmap_region_t* mmap)
{
    // 通过指针偏移，从 mmap_region_t 得到外层的 mmap_region_node_t
    // mmap_region_t 是 mmap_region_node_t 的第一个成员，所以地址相同
    mmap_region_node_t* node = (mmap_region_node_t*)mmap;
    
    spinlock_acquire(&list_lk);
    
    // 将节点插入链表头部（list_head 之后）
    node->next = list_head->next;
    list_head->next = node;
    
    spinlock_release(&list_lk);
}

// 输出仓库里可用的 mmap_region_node_t
// for debug
void mmap_show_mmaplist()
{
    spinlock_acquire(&list_lk);
    
    mmap_region_node_t* tmp = list_head;
    int node = 1, index = 0;
    while (tmp)
    {
        index = tmp - list_head;
        printf("node %d index = %d\n", node++, index);
        tmp = tmp->next;
    }

    spinlock_release(&list_lk);
}