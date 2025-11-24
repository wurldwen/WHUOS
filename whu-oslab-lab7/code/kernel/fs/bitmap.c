#include "fs/buf.h"
#include "fs/fs.h"
#include "fs/bitmap.h"
#include "lib/print.h"

extern super_block_t sb;

// 在位图中查找空闲位并设置
// @param bitmap_block 位图块号
// @return 返回分配的位号（从0开始），失败返回0xFFFFFFFF
static uint32 bitmap_search_and_set(uint32 bitmap_block)
{
    uint8 bit_cmp;
    uint32 byte, shift;

    buf_t* buf = buf_read(bitmap_block);
    
    // 遍历位图的每个字节和每个位
    for(byte = 0; byte < BLOCK_SIZE; byte++) {
        bit_cmp = 1;
        for(shift = 0; shift <= 7; shift++) {
            // 如果该位为0，表示空闲
            if((bit_cmp & buf->data[byte]) == 0) {
                buf->data[byte] |= bit_cmp;  // 标记为已使用
                buf_write(buf);              // 写回磁盘
                buf_release(buf);
                return byte * 8 + shift;     // 返回位号
            }
            bit_cmp = bit_cmp << 1;
        }
    }
    
    buf_release(buf);
    return 0xFFFFFFFF;  // 没有空闲位
}

// 清除位图中的某一位
// @param bitmap_block 位图块号
// @param num 要清除的位号
static void bitmap_unset(uint32 bitmap_block, uint32 num)
{
    uint32 byte = num / 8;
    uint32 shift = num % 8;
    uint8 bit_cmp = 1 << shift;
    
    buf_t* buf = buf_read(bitmap_block);
    
    // 检查该位是否已经是0
    if((buf->data[byte] & bit_cmp) == 0) {
        printf("bitmap_unset: freeing free bit %d\n", num);
        buf_release(buf);
        return;
    }
    
    // 清除该位
    buf->data[byte] &= ~bit_cmp;
    buf_write(buf);
    buf_release(buf);
}

// 分配一个数据块
// @return 返回分配的块号，失败返回0
uint32 bitmap_alloc_block()
{
    uint32 bit = bitmap_search_and_set(sb.data_bitmap_start);
    if(bit == 0xFFFFFFFF) {
        printf("bitmap_alloc_block: out of blocks\n");
        return 0;
    }
    return bit + sb.data_start;  // 块号 = 位号 + 数据区起始块号
}

// 释放一个数据块
// @param block_num 要释放的块号
void bitmap_free_block(uint32 block_num)
{
    if(block_num < sb.data_start) {
        printf("bitmap_free_block: invalid block_num %d\n", block_num);
        return;
    }
    uint32 bit = block_num - sb.data_start;
    bitmap_unset(sb.data_bitmap_start, bit);
}

// 分配一个inode
// @return 返回分配的inode编号，失败返回0xFFFF
uint16 bitmap_alloc_inode()
{
    uint32 bit = bitmap_search_and_set(sb.inode_bitmap_start);
    if(bit == 0xFFFFFFFF) {
        printf("bitmap_alloc_inode: out of inodes\n");
        return 0xFFFF;
    }
    return (uint16)bit;  // inode编号就是位号
}

// 释放一个inode
// @param inode_num 要释放的inode编号
void bitmap_free_inode(uint16 inode_num)
{
    bitmap_unset(sb.inode_bitmap_start, inode_num);
}

// 打印所有已经分配出去的bit序号(序号从0开始)
// for debug
void bitmap_print(uint32 bitmap_block_num)
{
    uint8 bit_cmp;
    uint32 byte, shift;

    printf("\nbitmap:\n");

    buf_t* buf = buf_read(bitmap_block_num);
    for(byte = 0; byte < BLOCK_SIZE; byte++) {
        bit_cmp = 1;
        for(shift = 0; shift <= 7; shift++) {
            if(bit_cmp & buf->data[byte])
               printf("bit %d is alloced\n", byte * 8 + shift);
            bit_cmp = bit_cmp << 1;
        }
    }
    printf("over\n");
    buf_release(buf);
}