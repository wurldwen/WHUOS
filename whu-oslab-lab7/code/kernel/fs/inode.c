#include "fs/buf.h"
#include "fs/bitmap.h"
#include "fs/inode.h"
#include "fs/fs.h"
#include "mem/vmem.h"
#include "proc/cpu.h"
#include "lib/print.h"
#include "lib/str.h"

extern super_block_t sb;

// 内存中的inode资源 + 保护它的锁
#define N_INODE 32
static inode_t icache[N_INODE];
static spinlock_t lk_icache;

// icache初始化
void inode_init()
{
    spinlock_init(&lk_icache, "icache");
    
    for(int i = 0; i < N_INODE; i++) {
        spinlock_init(&icache[i].slk, "inode");
        icache[i].inode_num = INODE_NUM_UNUSED;
        icache[i].ref = 0;
        icache[i].valid = false;
    }
}

/*---------------------- 与inode本身相关 -------------------*/

// 使用磁盘里的inode更新内存里的inode (write = false)
// 或 使用内存里的inode更新磁盘里的inode (write = true)
// 调用者需要设置inode_num并持有睡眠锁
void inode_rw(inode_t* ip, bool write)
{
    // 计算inode所在的块号
    uint32 block_num = ip->inode_num / INODE_PER_BLOCK + sb.inode_start;
    
    // 读取包含该inode的块
    buf_t* buf = buf_read(block_num);
    
    // 定位到块内的具体inode位置
    // inode在块内的偏移
    uint32 offset = (ip->inode_num % INODE_PER_BLOCK) * INODE_DISK_SIZE;
    
    if(write) {
        // 内存 -> 磁盘：将内存中的inode写入磁盘
        memmove(buf->data + offset, &ip->type, INODE_DISK_SIZE);
        buf_write(buf);
    } else {
        // 磁盘 -> 内存：从磁盘读取inode到内存
        memmove(&ip->type, buf->data + offset, INODE_DISK_SIZE);
    }
    
    buf_release(buf);
}

// 在icache里查询inode
// 如果没有查询到则申请一个空闲inode
// 如果icache没有空闲inode则报错
// 注意: 获得的inode没有上锁
inode_t* inode_alloc(uint16 inode_num)
{    
    spinlock_acquire(&lk_icache);
    
    // 第一步：查找是否已经在缓存中
    inode_t* ip = 0;
    inode_t* empty = 0;
    
    for(int i = 0; i < N_INODE; i++) {
        if(icache[i].inode_num == inode_num) {
            // 找到了，增加引用计数
            icache[i].ref++;
            spinlock_release(&lk_icache);
            return &icache[i];
        }
        
        // 记录第一个空闲位置
        if(empty == 0 && icache[i].ref == 0) {
            empty = &icache[i];
        }
    }
    
    // 第二步：未找到，使用空闲的inode
    if(empty == 0) {
        spinlock_release(&lk_icache);
        panic("inode_alloc: no inodes");
    }
    
    ip = empty;
    ip->inode_num = inode_num;
    ip->ref = 1;
    ip->valid = false;  // 需要后续从磁盘读取
    //printf("inode_alloc: allocated inode %d\n", inode_num);
    spinlock_release(&lk_icache);
    return ip;
}

// 在磁盘里申请一个inode (操作bitmap, 返回inode_num)
// 向icache申请一个inode数据结构
// 填写内存里的inode并以此更新磁盘里的inode
// 注意: 获得的inode没有上锁
inode_t* inode_create(uint16 type, uint16 major, uint16 minor)
{
    // 从位图分配inode
    uint16 inum = bitmap_alloc_inode();
    if(inum == 0xFFFF) {
        return 0;
    }
    
    // 获取inode结构
    inode_t* ip = inode_alloc(inum);
    
    // 需要上锁才能修改inode内容
    spinlock_acquire(&ip->slk);
    
    // 初始化inode内容
    ip->type = type;
    ip->major = major;
    ip->minor = minor;
    ip->nlink = 1;
    ip->size = 0;
    for(int i = 0; i < N_ADDRS; i++) {
        ip->addrs[i] = 0;
    }
    ip->valid = true;
    
    // 写回磁盘
    inode_rw(ip, true);
    
    spinlock_release(&ip->slk);
    
    return ip;
}

// 供inode_free调用
// 在磁盘上删除一个inode及其管理的文件 (修改inode bitmap + block bitmap)
// 调用者需要持有lk_icache, 但不应该持有slk
static void inode_destroy(inode_t* ip)
{
    // 上锁以访问inode内容
    spinlock_acquire(&ip->slk);
    
    // 释放inode管理的所有数据块
    inode_free_data(ip);
    
    // 清空inode内容
    ip->type = FT_UNUSED;
    ip->size = 0;
    ip->nlink = 0;
    
    // 写回磁盘
    inode_rw(ip, true);
    
    spinlock_release(&ip->slk);
    
    // 释放inode位图
    bitmap_free_inode(ip->inode_num);
    
    // 清空内存中的inode
    ip->inode_num = INODE_NUM_UNUSED;
    ip->valid = false;
}

// 向icache里归还inode
// inode->ref--
// 调用者不应该持有slk
void inode_free(inode_t* ip)
{
    spinlock_acquire(&lk_icache);
    
    ip->ref--;
    
    // 如果引用计数降为0且链接数为0，则销毁inode
    if(ip->ref == 0 && ip->nlink == 0) {
        inode_destroy(ip);
    }
    
    spinlock_release(&lk_icache);
}

// ip->ref++ with lock
inode_t* inode_dup(inode_t* ip)
{
    spinlock_acquire(&lk_icache);
    ip->ref++;
    spinlock_release(&lk_icache);
    return ip;
}

// 给inode上锁
// 如果valid失效则从磁盘中读入
void inode_lock(inode_t* ip)
{
    spinlock_acquire(&ip->slk);
    
    // 如果inode内容无效，从磁盘读取
    if(!ip->valid) {
        inode_rw(ip, false);
        ip->valid = true;
    }
}

// 给inode解锁
void inode_unlock(inode_t* ip)
{
    spinlock_release(&ip->slk);
}

// 连招: 解锁 + 释放
void inode_unlock_free(inode_t* ip)
{
    inode_unlock(ip);
    inode_free(ip);
}

/*---------------------------- 与inode管理的data相关 --------------------------*/

// 辅助 inode_locate_block
// 递归查询或创建block
static uint32 locate_block(uint32* entry, uint32 bn, uint32 size)
{
    if(*entry == 0)
        *entry = bitmap_alloc_block();

    if(size == 1)
        return *entry;    

    uint32* next_entry;
    uint32 next_size = size / ENTRY_PER_BLOCK;
    uint32 next_bn = bn % next_size;
    uint32 ret = 0;

    buf_t* buf = buf_read(*entry);
    next_entry = (uint32*)(buf->data) + bn / next_size;
    ret = locate_block(next_entry, next_bn, next_size);
    buf_release(buf);

    return ret;
}

// 确定inode里第bn块data block的block_num
// 如果不存在第bn块data block则申请一个并返回它的block_num
// 由于inode->addrs的结构, 这个过程比较复杂, 需要单独处理
static uint32 inode_locate_block(inode_t* ip, uint32 bn)
{
    // 情况1：在直接索引区域（0-9）
    if(bn < N_ADDRS_1)
        return locate_block(&ip->addrs[bn], bn, 1);

    // 情况2：在一级间接索引区域（10-11）
    bn -= N_ADDRS_1;
    if(bn < N_ADDRS_2 * ENTRY_PER_BLOCK)
    {
        uint32 size = ENTRY_PER_BLOCK;
        uint32 idx = bn / size;   // 确定使用addrs[10]还是addrs[11]
        uint32 b = bn % size;     // 在间接块内的偏移
        return locate_block(&ip->addrs[N_ADDRS_1 + idx], b, size);
    }

    // 情况3：在二级间接索引区域（12）
    bn -= N_ADDRS_2 * ENTRY_PER_BLOCK;
    if(bn < N_ADDRS_3 * ENTRY_PER_BLOCK * ENTRY_PER_BLOCK)
    {
        uint32 size = ENTRY_PER_BLOCK * ENTRY_PER_BLOCK;
        uint32 idx = bn / size;   // 确定使用addrs[12]
        uint32 b = bn % size;     // 在二级间接结构内的偏移
        return locate_block(&ip->addrs[N_ADDRS_1 + N_ADDRS_2 + idx], b, size);
    }

    // 超出文件大小限制
    panic("inode_locate_block: overflow");
    return 0;
}

// 读取 inode 管理的 data block
// 调用者需要持有 inode 锁
// 成功返回读出的字节数, 失败返回0
uint32 inode_read_data(inode_t* ip, uint32 offset, uint32 len, void* dst, bool user)
{
    // 检查读取范围是否合法
    if(offset > ip->size)
        return 0;
    
    if(offset + len > ip->size)
        len = ip->size - offset;
    
    uint32 total = 0;
    uint32 m;
    
    while(total < len) {
        // 计算当前块号和块内偏移
        uint32 bn = offset / BLOCK_SIZE;
        uint32 off = offset % BLOCK_SIZE;
        
        // 获取实际的块号
        uint32 block_num = inode_locate_block(ip, bn);
        
        // 读取块
        buf_t* buf = buf_read(block_num);
        
        // 计算本次读取的字节数
        m = BLOCK_SIZE - off;
        if(len - total < m)
            m = len - total;
        
        // 复制数据
        if(user) {
            // 复制到用户空间
            uvm_copyout(myproc()->pgtbl, (uint64)dst + total, (uint64)(buf->data + off), m);
        } else {
            // 复制到内核空间
            memmove((char*)dst + total, buf->data + off, m);
        }
        
        buf_release(buf);
        
        total += m;
        offset += m;
    }
    
    return total;
}

// 写入 inode 管理的 data block (可能导致管理的 block 增加)
// 调用者需要持有 inode 锁
// 成功返回写入的字节数, 失败返回0
uint32 inode_write_data(inode_t* ip, uint32 offset, uint32 len, void* src, bool user)
{
    // 检查写入范围是否超出最大文件大小
    if(offset + len > INODE_MAXSIZE)
        return 0;
    
    uint32 total = 0;
    uint32 m;
    
    while(total < len) {
        // 计算当前块号和块内偏移
        uint32 bn = offset / BLOCK_SIZE;
        uint32 off = offset % BLOCK_SIZE;
        
        // 获取或分配块
        uint32 block_num = inode_locate_block(ip, bn);
        
        // 读取块
        buf_t* buf = buf_read(block_num);
        
        // 计算本次写入的字节数
        m = BLOCK_SIZE - off;
        if(len - total < m)
            m = len - total;
        
        // 复制数据
        if(user) {
            // 从用户空间复制
            uvm_copyin(myproc()->pgtbl, (uint64)(buf->data + off), (uint64)src + total, m);
        } else {
            // 从内核空间复制
            memmove(buf->data + off, (char*)src + total, m);
        }
        
        // 写回磁盘
        buf_write(buf);
        buf_release(buf);
        
        total += m;
        offset += m;
    }
    
    // 更新文件大小
    if(offset > ip->size) {
        ip->size = offset;
        inode_rw(ip, true);  // 写回inode元数据
    }
    
    return total;
}

// 辅助 inode_free_data 做递归释放
static void data_free(uint32 block_num, uint32 level)
{  
    assert(block_num != 0, "data_free: block_num = 0");

    // block_num 是 data block
    if(level == 0) goto ret;

    // block_num 是 metadata block
    buf_t* buf = buf_read(block_num);
    for(uint32* addr = (uint32*)buf->data; addr < (uint32*)(buf->data + BLOCK_SIZE); addr++) 
    {
        if(*addr == 0) break;
        data_free(*addr, level - 1);
    }
    buf_release(buf);

ret:
    bitmap_free_block(block_num);
    return;
}

// 释放inode管理的 data block
// ip->addrs被清空 ip->size置0
// 调用者需要持有slk
void inode_free_data(inode_t* ip)
{
    // 释放直接索引块
    for(int i = 0; i < N_ADDRS_1; i++) {
        if(ip->addrs[i]) {
            bitmap_free_block(ip->addrs[i]);
            ip->addrs[i] = 0;
        }
    }
    
    // 释放一级间接索引块
    for(int i = N_ADDRS_1; i < N_ADDRS_1 + N_ADDRS_2; i++) {
        if(ip->addrs[i]) {
            data_free(ip->addrs[i], 1);  // level 1
            ip->addrs[i] = 0;
        }
    }
    
    // 释放二级间接索引块
    for(int i = N_ADDRS_1 + N_ADDRS_2; i < N_ADDRS; i++) {
        if(ip->addrs[i]) {
            data_free(ip->addrs[i], 2);  // level 2
            ip->addrs[i] = 0;
        }
    }
    
    ip->size = 0;
}

static char* inode_types[] = {
    "INODE_UNUSED",
    "INODE_DIR",
    "INODE_FILE",
    "INODE_DEVICE",
};

// 输出inode信息
// for dubug
void inode_print(inode_t* ip)
{
    assert(spinlock_holding(&ip->slk), "inode_print: lk");

    printf("\ninode information:\n");
    printf("num = %d, ref = %d, valid = %d\n", ip->inode_num, ip->ref, ip->valid);
    printf("type = %s, major = %d, minor = %d, nlink = %d\n", inode_types[ip->type], ip->major, ip->minor, ip->nlink);
    printf("size = %d, addrs =", ip->size);
    for(int i = 0; i < N_ADDRS; i++)
        printf(" %d", ip->addrs[i]);
    printf("\n");
}