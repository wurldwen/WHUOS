#include "fs/buf.h"
#include "dev/vio.h"
#include "lib/lock.h"
#include "lib/print.h"
#include "lib/str.h"

#define N_BLOCK_BUF 64
#define BLOCK_NUM_UNUSED 0xFFFFFFFF

// offsetof 宏定义
#define offsetof(TYPE, MEMBER) ((uint64)&((TYPE *)0)->MEMBER)

// 将buf包装成双向循环链表的node
typedef struct buf_node {
    buf_t buf;
    struct buf_node* next;
    struct buf_node* prev;
} buf_node_t;

// buf cache
static buf_node_t buf_cache[N_BLOCK_BUF];
static buf_node_t head_buf; // ->next 已分配 ->prev 可分配
static spinlock_t lk_buf_cache; // 这个锁负责保护 链式结构 + buf_ref + block_num

// 链表操作
static void insert_head(buf_node_t* buf_node, bool head_next)
{
    // 离开
    if(buf_node->next && buf_node->prev) {
        buf_node->next->prev = buf_node->prev;
        buf_node->prev->next = buf_node->next;
    }

    // 插入
    if(head_next) { // 插入 head->next
        buf_node->prev = &head_buf;
        buf_node->next = head_buf.next;
        head_buf.next->prev = buf_node;
        head_buf.next = buf_node;        
    } else { // 插入 head->prev
        buf_node->next = &head_buf;
        buf_node->prev = head_buf.prev;
        head_buf.prev->next = buf_node;
        head_buf.prev = buf_node;
    }
}

// 初始化缓冲区缓存
void buf_init()
{
    spinlock_init(&lk_buf_cache, "buf_cache");
    
    // 初始化head节点（双向循环链表）
    head_buf.next = &head_buf;
    head_buf.prev = &head_buf;
    
    // 初始化所有缓冲区节点
    for(int i = 0; i < N_BLOCK_BUF; i++) {
        buf_node_t* node = &buf_cache[i];
        buf_t* buf = &node->buf;
        
        // 初始化自旋锁
        spinlock_init(&buf->slk, "buffer");
        
        // 初始化buf字段
        buf->block_num = BLOCK_NUM_UNUSED;
        buf->buf_ref = 0;
        buf->disk = false;
        
        // 将node插入链表尾部（head->prev位置，表示可分配）
        insert_head(node, false);
    }
}

/*
    首先假设这个block_num对应的block在内存中有备份, 找到它并上锁返回
    如果找不到, 尝试申请一个无人使用的buf, 去磁盘读取对应block并上锁返回
    如果没有空闲buf, panic报错
    (建议合并xv6的bget())
*/
buf_t* buf_read(uint32 block_num)
{
    buf_node_t* node;
    buf_t* buf;
    
    spinlock_acquire(&lk_buf_cache);
    
    // 第一步：查找是否已经在缓存中
    for(node = head_buf.next; node != &head_buf; node = node->next) {
        buf = &node->buf;
        if(buf->block_num == block_num) {
            // 找到了，增加引用计数
            buf->buf_ref++;
            spinlock_release(&lk_buf_cache);
            
            // 获取自旋锁
            spinlock_acquire(&buf->slk);
            return buf;
        }
    }
    
    // 第二步：未找到，需要从空闲缓冲区分配
    // 从链表尾部（head->prev）开始查找，这些是最少使用的
    for(node = head_buf.prev; node != &head_buf; node = node->prev) {
        buf = &node->buf;
        if(buf->buf_ref == 0) {
            // 找到一个空闲缓冲区
            buf->block_num = block_num;
            buf->buf_ref = 1;
            buf->disk = false;  // 标记为未同步
            spinlock_release(&lk_buf_cache);
            
            // 获取自旋锁
            spinlock_acquire(&buf->slk);
            
            // 从磁盘读取数据
            virtio_disk_rw(buf, false);  // false表示读操作
            
            return buf;
        }
    }
    
    // 没有可用的缓冲区
    spinlock_release(&lk_buf_cache);
    panic("buf_read: no buffers");
    return 0;
}

// 写函数 (强制磁盘和内存保持一致)
void buf_write(buf_t* buf)
{
    // 调用者应该持有自旋锁
    if(!spinlock_holding(&buf->slk)) {
        panic("buf_write: not holding lock");
    }
    
    // 写入磁盘
    virtio_disk_rw(buf, true);  // true表示写操作
}

// buf 释放
void buf_release(buf_t* buf)
{
    // 调用者应该持有自旋锁
    if(!spinlock_holding(&buf->slk)) {
        panic("buf_release: not holding lock");
    }
    
    // 释放自旋锁
    spinlock_release(&buf->slk);
    
    spinlock_acquire(&lk_buf_cache);
    buf->buf_ref--;
    
    if(buf->buf_ref == 0) {
        // 没有人引用了，移动到链表头部（最近使用）
        // 通过buf指针获取包含它的node
        buf_node_t* node = (buf_node_t*)((char*)buf - offsetof(buf_node_t, buf));
        insert_head(node, true);
    }
    
    spinlock_release(&lk_buf_cache);
}

// 输出buf_cache的情况
void buf_print()
{
    printf("\nbuf_cache:\n");
    buf_node_t* buf = head_buf.next;
    spinlock_acquire(&lk_buf_cache);
    while(buf != &head_buf)
    {
        buf_t* b = &buf->buf;
        printf("buf %d: ref = %d, block_num = %d\n", (int)(buf-buf_cache), b->buf_ref, b->block_num);
        for(int i = 0; i < 8; i++)
            printf("%d ",b->data[i]);
        printf("\n");
        buf = buf->next;
    }
    spinlock_release(&lk_buf_cache);
}