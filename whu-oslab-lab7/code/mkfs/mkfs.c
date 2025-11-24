/*
 * mkfs.c - 文件系统镜像创建工具
 * 
 * 功能：创建一个符合自定义文件系统格式的磁盘镜像文件
 * 用于在主机上构建文件系统，然后在操作系统中挂载使用
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>

// 磁盘布局: [ 超级块 | inode位图 | inode块区域 | 数据位图 | 数据块区域 ]
// disk layout: [ super block | inode bitmap | inode blocks | data bitmap | data blocks ]

#define FS_MAGIC 0x12345678  // 文件系统魔数，用于识别文件系统类型

// 超级块结构体
// 存储文件系统的元数据信息
typedef struct super_block {
    unsigned int magic;               // 魔数，标识文件系统类型
    unsigned int block_size;          // 块大小（字节数）

    unsigned int inode_bitmap_start;  // inode位图起始块号
    unsigned int inode_start;         // inode区域起始块号
    unsigned int data_bitmap_start;   // 数据位图起始块号
    unsigned int data_start;          // 数据块区域起始块号

    unsigned int inode_blocks;        // inode区域占用的块数
    unsigned int data_blocks;         // 数据块区域占用的块数
    unsigned int total_blocks;        // 总块数
} super_block_t;

// inode磁盘结构体 (64字节)
// 存储文件/目录的元数据和数据块索引
typedef struct inode_disk {
    short type;              // 文件类型：目录/普通文件/设备文件
    short major;             // 设备主设备号
    short minor;             // 设备次设备号
    short nlink;             // 硬链接数
    unsigned int size;       // 文件大小（字节数）
    unsigned int addrs[13];  // 数据块地址数组（前10个直接索引，后2个一级间接，最后1个二级间接）
} inode_disk_t;

// 目录项结构体 (32字节)
// 存储目录中文件名与inode号的映射关系
typedef struct dirent {
    unsigned short inode_num;  // inode编号
    char name[30];             // 文件名（最长29字符+'\0'）
} dirent_t;

// 文件类型常量定义
#define FT_UNUSED 0  // 未使用
#define FT_DIR    1  // 目录
#define FT_FILE   2  // 普通文件
#define FT_DEVICE 3  // 设备文件 


// ============ 常量定义 ============
#define BLOCK_SIZE       1024 // 每个块占1024字节
#define N_DATA_BLOCK     8192 // 数据块数量（1个块的位图可管理8192个块）
#define N_INODE_BLOCK    128  // inode块数量（支持2048个文件）
#define N_BLOCK          (N_DATA_BLOCK + N_INODE_BLOCK + 3)  // 总块数：数据块+inode块+超级块+2个位图块
#define INODE_PER_BLOCK  (BLOCK_SIZE / sizeof(inode_disk_t)) // 每个块可容纳的inode数量（1024/64=16）
#define N_INODE          (N_INODE_BLOCK * INODE_PER_BLOCK)   // inode总数（128*16=2048）

// ============ inode数据块索引相关常量 ============
#define ENTRY_PER_BLOCK (BLOCK_SIZE / sizeof(unsigned int))  // 每个间接索引块可容纳的条目数（1024/4=256）
#define N_ADDRS_1 10  // 直接索引数量（前10个addrs直接指向数据块）
#define N_ADDRS_2 2   // 一级间接索引数量（接下来2个addrs指向一级间接块）
#define N_ADDRS_3 1   // 二级间接索引数量（最后1个addrs指向二级间接块）
#define N_ADDRS (N_ADDRS_1 + N_ADDRS_2 + N_ADDRS_3)  // addrs数组总长度（10+2+1=13）

// 根据inode编号计算其所在的inode块号
#define INODE_LOCATE_BLOCK(inum, sb)  ((inum) / INODE_PER_BLOCK + sb.inode_start)

int fsfd;              // 文件系统镜像文件描述符
super_block_t sb;      // 超级块全局变量

/*
 * 大小端转换函数 - short类型
 * 将16位整数转换为小端格式
 */
unsigned short xshort(unsigned short x)
{
    unsigned short y;
    unsigned char* a = (unsigned char*)&y;
    a[0] = x;
    a[1] = x >> 8;
    return y;
}

/*
 * 大小端转换函数 - int类型
 * 将32位整数转换为小端格式
 */
unsigned int xint(unsigned int x)
{
    unsigned int y;
    unsigned char* a = (unsigned char*)&y;
    a[0] = x;
    a[1] = x >> 8;
    a[2] = x >> 16;
    a[3] = x >> 24;
    return y;
}

/*
 * 向磁盘写入一个块
 * @param block_num 块号
 * @param buf 要写入的数据缓冲区（大小为BLOCK_SIZE）
 */
void block_write(unsigned int block_num, void* buf)
{
    if(lseek(fsfd, BLOCK_SIZE * block_num, 0) != BLOCK_SIZE * block_num) {
        perror("lsek");
        exit(1);
    }
    if(write(fsfd, buf, BLOCK_SIZE) != BLOCK_SIZE) {
        perror("write");
        exit(1);
    }
}

/*
 * 从磁盘读取一个块
 * @param block_num 块号
 * @param buf 读取数据的缓冲区（大小为BLOCK_SIZE）
 */
void block_read(unsigned int block_num, void* buf)
{
    if(lseek(fsfd, BLOCK_SIZE * block_num, 0) != BLOCK_SIZE * block_num) {
        perror("lsek");
        exit(1);
    }
    if(read(fsfd, buf, BLOCK_SIZE) != BLOCK_SIZE) {
        perror("read");
        exit(1);
    }
}

/*
 * 分配一个数据块
 * 在数据位图中查找空闲位，标记为已使用，并返回对应的块号
 * @return 分配的数据块号
 */
unsigned int block_alloc()
{
    char buf[BLOCK_SIZE];
    unsigned int byte, shift;
    unsigned char bit_cmp;

    // 读取数据位图块
    block_read(sb.data_bitmap_start, buf);
    
    // 遍历位图的每个字节和每个位
    for(byte = 0; byte < BLOCK_SIZE; byte++) {
        bit_cmp = 1;
        for(shift = 0; shift <= 7; shift++) {
            // 如果该位为0，表示该块空闲
            if((bit_cmp & buf[byte]) == 0) {
                buf[byte] |= bit_cmp;  // 标记为已使用
                goto find;
            }
            bit_cmp = bit_cmp << 1;
        }
    }
    // 没有空闲块了
    printf("block_alloc: no bit left\n");
    while(1);
find:
    // 写回位图
    block_write(sb.data_bitmap_start, buf);
    // 计算并返回块号：位图中的位偏移 + 数据块起始位置
    return byte * 8 + shift + sb.data_start;
}

/*
 * 分配一个inode
 * 在inode位图中查找空闲位，标记为已使用，并返回对应的inode编号
 * @return 分配的inode编号
 */
unsigned short inode_alloc()
{
    char buf[BLOCK_SIZE];
    unsigned int byte, shift;
    unsigned char bit_cmp;

    // 读取inode位图块
    block_read(sb.inode_bitmap_start, buf);
    
    // 遍历位图的每个字节和每个位
    for(byte = 0; byte < BLOCK_SIZE; byte++) {
        bit_cmp = 1;
        for(shift = 0; shift <= 7; shift++) {
            // 如果该位为0，表示该inode空闲
            if((bit_cmp & buf[byte]) == 0) {
                buf[byte] |= bit_cmp;  // 标记为已使用
                goto find;
            }
            bit_cmp = bit_cmp << 1;
        }
    }
    // 没有空闲inode了
    printf("inode_alloc: no bit left\n");
    while(1);
find:
    // 写回位图
    block_write(sb.inode_bitmap_start, buf);
    // 计算并返回inode编号：位图中的位偏移
    return (unsigned short)(byte * 8 + shift);
}

/*
 * 从磁盘读取一个inode
 * @param inode_num inode编号
 * @param ip 输出参数，存储读取的inode结构
 */
void inode_read(unsigned int inode_num, inode_disk_t* ip)
{
    char buf[BLOCK_SIZE];
    inode_disk_t* dip;

    // 计算inode所在的块号
    unsigned int block_num = INODE_LOCATE_BLOCK(inode_num, sb);
    block_read(block_num, buf);
    
    // 定位到块内的具体inode位置
    dip = ((inode_disk_t*)buf) + (inode_num % INODE_PER_BLOCK);
    *ip = *dip;
}

/*
 * 向磁盘写入一个inode
 * @param inode_num inode编号
 * @param ip 要写入的inode结构
 */
void inode_write(unsigned short inode_num, inode_disk_t* ip)
{
    char buf[BLOCK_SIZE];
    inode_disk_t* dip;

    // 计算inode所在的块号
    unsigned int block_num = INODE_LOCATE_BLOCK(inode_num, sb);
    block_read(block_num, buf);
    
    // 定位到块内的具体inode位置并写入
    dip = ((inode_disk_t*)buf) + (inode_num % INODE_PER_BLOCK);
    *dip = *ip;
    block_write(block_num, buf);
}

/*
 * 创建并初始化一个inode
 * @param inode inode结构指针
 * @param inode_num inode编号
 * @param type 文件类型（FT_DIR/FT_FILE/FT_DEVICE）
 */
void inode_create(inode_disk_t* inode, unsigned short inode_num, unsigned short type)
{
    inode->type = xshort(type);
    inode->major = xshort(0);
    inode->minor = xshort(0);
    inode->nlink = xshort(1);
    inode->size = xint(0);
    // 初始化所有数据块地址为0
    for(int i = 0; i < N_ADDRS; i++)
        inode->addrs[i] = xint(0);
    inode_write(inode_num, inode);
}

// 用于dirent_create函数的缓冲区
char dir_buf[BLOCK_SIZE];

/*
 * 在目录中添加一个目录项
 * @param dir_block 目录数据块号
 * @param offset 在块内的偏移量
 * @param name 文件名
 * @param inode_num 对应的inode编号
 * @return 下一个目录项的偏移量
 */
unsigned int dirent_create(unsigned int dir_block, unsigned int offset, char* name, unsigned short inode_num)
{
    dirent_t de;
    de.inode_num = xint(inode_num);
    assert(strlen(name) < 30);  // 确保文件名不超过29字符
    strcpy(de.name, name);

    // 读取目录块，写入目录项，再写回
    block_read(dir_block, dir_buf);
    memmove(dir_buf + offset, &de, sizeof(de));
    block_write(dir_block, dir_buf);

    return offset + sizeof(dirent_t);
}

/*
 * locate_block的辅助递归函数
 * 用于处理间接索引，递归查询或创建数据块
 * @param entry 当前层级的索引项指针
 * @param bn 要查找的块号（在当前层级的相对编号）
 * @param size 当前层级管理的块数量
 * @return 实际的数据块号
 */
static unsigned int locate_block(unsigned int* entry, unsigned int bn, unsigned int size)
{
    // 如果该索引项为空，分配一个新块
    if(*entry == 0)
        *entry = block_alloc();

    // 如果size==1，说明这已经是直接索引，直接返回
    if(size == 1)
        return *entry;    

    // 处理间接索引
    unsigned int* next_entry;
    unsigned int next_size = size / ENTRY_PER_BLOCK;  // 下一层管理的块数
    unsigned int next_bn = bn % next_size;            // 在下一层的相对位置
    unsigned int ret = 0;

    // 读取间接块
    char buf[BLOCK_SIZE];
    block_read(*entry, buf);
    next_entry = (unsigned int*)(buf) + bn / next_size;
    
    // 递归查找下一层
    ret = locate_block(next_entry, next_bn, next_size);

    return ret;
}

/*
 * 确定inode中第bn个数据块的实际块号
 * 如果该数据块不存在则分配一个新块
 * 
 * inode->addrs数组的结构：
 * - addrs[0-9]: 直接索引，直接指向数据块（可索引10个块）
 * - addrs[10-11]: 一级间接索引，指向间接块，间接块内包含数据块号（每个可索引256个块）
 * - addrs[12]: 二级间接索引，指向一级间接块（可索引256*256个块）
 * 
 * @param ip inode指针
 * @param bn 逻辑块号（从0开始）
 * @return 实际的物理块号
 */
static unsigned int inode_locate_block(inode_disk_t* ip, unsigned int bn)
{
    // 情况1：在直接索引区域（0-9）
    if(bn < N_ADDRS_1)
        return locate_block(&ip->addrs[bn], bn, 1);

    // 情况2：在一级间接索引区域（10-11）
    bn -= N_ADDRS_1;
    if(bn < N_ADDRS_2 * ENTRY_PER_BLOCK)
    {
        unsigned int size = ENTRY_PER_BLOCK;
        unsigned int idx = bn / size;   // 确定使用addrs[10]还是addrs[11]
        unsigned int b = bn % size;     // 在间接块内的偏移
        return locate_block(&ip->addrs[N_ADDRS_1 + idx], b, size);
    }

    // 情况3：在二级间接索引区域（12）
    bn -= N_ADDRS_2 * ENTRY_PER_BLOCK;
    if(bn < N_ADDRS_3 * ENTRY_PER_BLOCK * ENTRY_PER_BLOCK)
    {
        unsigned int size = ENTRY_PER_BLOCK * ENTRY_PER_BLOCK;
        unsigned int idx = bn / size;   // 确定使用addrs[12]
        unsigned int b = bn % size;     // 在二级间接结构内的偏移
        return locate_block(&ip->addrs[N_ADDRS_1 + N_ADDRS_2 + idx], b, size);
    }

    // 超出文件大小限制
    printf("inode_locate_block: overflow\n");
    while(1);

    return 0;
}

/*
 * 主函数 - 创建文件系统镜像
 * 
 * 使用方法: ./mkfs <镜像文件名> <用户程序1> <用户程序2> ...
 * 例如: ./mkfs fs.img ./user/_init ./user/_sh ./user/_cat
 * 
 * 创建的文件系统包含：
 * 1. 超级块（块0）
 * 2. inode位图（块1）
 * 3. inode块区域（块2-129，共128块）
 * 4. 数据位图（块130）
 * 5. 数据块区域（块131开始）
 * 6. 根目录（inode 0）包含所有传入的用户程序
 */
int main(int argc, char* argv[])
{
    // 确保inode_disk_t大小是块大小的整数因子
    assert(BLOCK_SIZE % sizeof(inode_disk_t) == 0);
    
    // 创建磁盘镜像文件
    fsfd = open(argv[1], O_RDWR | O_CREAT | O_TRUNC, 0666);
    if(fsfd < 0) {
        perror(argv[1]);
        exit(1);
    }

    // ============ 填充超级块 ============
    sb.magic = FS_MAGIC;
    sb.block_size = xint(BLOCK_SIZE);
    sb.inode_blocks = xint(N_INODE_BLOCK);
    sb.data_blocks = xint(N_DATA_BLOCK);
    sb.total_blocks = xint(N_BLOCK);
    sb.inode_bitmap_start = xint(1);  // 块1是inode位图
    sb.inode_start = xint(1 + 1);     // 块2开始是inode区域
    sb.data_bitmap_start = xint(1 + 1 + N_INODE_BLOCK);  // inode区域后是数据位图
    sb.data_start = xint(1 + 1 + N_INODE_BLOCK + 1);     // 数据位图后是数据块区域

    // ============ 初始化磁盘镜像 ============
    char buf[BLOCK_SIZE];
    memset(buf, 0, sizeof(buf));

    // 写入全0的磁盘镜像
    for(int i = 0; i < N_BLOCK; i++)
        block_write(i, buf);

    // 写入超级块到块0
    memmove(buf, &sb, sizeof(sb));
    block_write(0, buf);

    // ============ 创建根目录 ============
    inode_disk_t rooti;
    unsigned short root_inum = inode_alloc();  // 应该返回0
    unsigned int rooti_block = block_alloc();  // 为根目录分配数据块
    
    // 确保根目录的inode编号是0
    if(root_inum != 0) {
        printf("rooti = %d\n", root_inum);
        while(1);
    }
    inode_create(&rooti, root_inum, FT_DIR);

    // 添加 . 和 .. 目录项（都指向根目录自己）
    unsigned int offset = 0;
    offset = dirent_create(rooti_block, offset, ".\0", root_inum);
    offset = dirent_create(rooti_block, offset, "..\0", root_inum);

    // ============ 将用户程序写入文件系统 ============
    // 命令行参数格式: ./user/_程序名
    char* shortname;
    int fd, read_len;
    inode_disk_t inode;
    unsigned short inum;
    unsigned int bn = 0, block_num = 0;

    // 遍历所有命令行参数中的用户程序
    for(int i = 2; i < argc; i++)
    {
        // 从路径中提取短文件名
        // 例如: "./user/_init" -> "init"
        shortname = argv[i] + 7;  // 跳过 "./user/_" 前缀
        assert(*shortname == '_');
        assert(index(shortname, '/') == 0);  // 确保不包含路径分隔符
        shortname++;  // 跳过下划线

        // 为文件分配inode并创建目录项
        inum = inode_alloc();
        inode_create(&inode, inum, FT_FILE);
        offset = dirent_create(rooti_block, offset, shortname, inum);
        
        // 打开主机上的用户程序文件
        fd = open(argv[i], 0);
        if(fd < 0) {
            perror(argv[i]);
            exit(1);
        }
        
        // 读取文件内容并写入文件系统
        while(1) {
            read_len = read(fd, buf, BLOCK_SIZE);
            block_num = inode_locate_block(&inode, bn++);  // 获取/分配数据块
            block_write(block_num, buf);
            inode.size += read_len;
            if(read_len < BLOCK_SIZE) break;  // 读取完毕
        }
        
        // 关闭主机文件
        close(fd);

        // 更新inode并写回磁盘
        // 注意：需要将addrs数组转换为小端格式
        for(int j = 0; j < N_ADDRS; j++)
            inode.addrs[j] = xint(inode.addrs[j]);
        inode.size = xint(inode.size);
        inode_write(inum, &inode);
    }

    // ============ 更新并写回根目录inode ============
    rooti.addrs[0] = xint(rooti_block);  // 设置根目录的数据块
    rooti.size = xint(sizeof(dirent_t) * argc);  // 设置根目录大小
    inode_write(root_inum, &rooti);

    return 0;
}