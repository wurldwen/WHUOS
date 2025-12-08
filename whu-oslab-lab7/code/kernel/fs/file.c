#include "fs/fs.h"
#include "fs/buf.h"
#include "fs/dir.h"
#include "fs/bitmap.h"
#include "fs/inode.h"
#include "fs/file.h"
#include "mem/vmem.h"
#include "proc/cpu.h"
#include "lib/print.h"

// 设备列表(读写接口)
dev_t devlist[N_DEV];

// ftable + 保护它的锁
#define N_FILE 32
file_t ftable[N_FILE];
spinlock_t lk_ftable;

// ftable初始化 + devlist初始化
void file_init()
{
    spinlock_init(&lk_ftable, "ftable");
    
    for(int i = 0; i < N_FILE; i++) {
        ftable[i].ref = 0;
        ftable[i].type = FD_UNUSED;
    }
    
    for(int i = 0; i < N_DEV; i++) {
        devlist[i].read = 0;
        devlist[i].write = 0;
    }
}

// alloc file_t in ftable
// 失败则panic
file_t* file_alloc()
{
    spinlock_acquire(&lk_ftable);
    
    for(int i = 0; i < N_FILE; i++) {
        if(ftable[i].ref == 0) {
            ftable[i].ref = 1;
            spinlock_release(&lk_ftable);
            return &ftable[i];
        }
    }
    
    spinlock_release(&lk_ftable);
    panic("file_alloc: no free file");
    return 0;
}

// 创建设备文件(供proczero创建console)
file_t* file_create_dev(char* path, uint16 major, uint16 minor)
{
    // 创建设备inode
    inode_t* ip = path_create_inode(path, FT_DEVICE, major, minor);
    if(ip == 0) {
        return 0;
    }
    
    // 分配file结构
    file_t* file = file_alloc();
    file->type = FD_DEVICE;
    file->readable = true;
    file->writable = true;
    file->major = major;
    file->offset = 0;
    file->ip = ip;
    
    return file;
}

// 打开一个文件
file_t* file_open(char* path, uint32 open_mode)
{
    inode_t* ip;
    
    // 如果需要创建文件
    if(open_mode & MODE_CREATE) {
        ip = path_create_inode(path, FT_FILE, 0, 0);
    } else {
        ip = path_to_inode(path);
    }
    
    if(ip == 0) {
        return 0;
    }
    
    inode_lock(ip);
    
    // 分配file结构
    file_t* file = file_alloc();
    
    // 根据inode类型设置file类型
    if(ip->type == FT_DIR) {
        file->type = FD_DIR;
    } else if(ip->type == FT_FILE) {
        file->type = FD_FILE;
    } else if(ip->type == FT_DEVICE) {
        file->type = FD_DEVICE;
        file->major = ip->major;
    } else {
        panic("file_open: unknown type");
    }
    
    // 设置读写权限
    file->readable = (open_mode & MODE_READ) != 0;
    file->writable = (open_mode & MODE_WRITE) != 0;
    file->offset = 0;
    file->ip = ip;
    
    inode_unlock(ip);
    
    return file;
}

// 释放一个file
void file_close(file_t* file)
{
    spinlock_acquire(&lk_ftable);
    
    if(file->ref < 1) {
        panic("file_close: ref < 1");
    }
    
    file->ref--;
    
    if(file->ref == 0) {
        // 保存类型和inode，因为后面会清空file
        uint16 type = file->type;
        inode_t* ip = file->ip;
        
        // 清空file结构
        file->type = FD_UNUSED;
        file->readable = false;
        file->writable = false;
        file->offset = 0;
        file->ip = 0;
        
        spinlock_release(&lk_ftable);
        
        // 释放inode
        if(type == FD_FILE || type == FD_DIR || type == FD_DEVICE) {
            inode_free(ip);
        }
    } else {
        spinlock_release(&lk_ftable);
    }
}

// 文件内容读取
// 返回读取到的字节数
uint32 file_read(file_t* file, uint32 len, uint64 dst, bool user)
{
    if(!file->readable) {
        return -1;
    }
    
    if(file->type == FD_DEVICE) {
        // 设备文件，调用设备的读函数
        if(file->major >= N_DEV || devlist[file->major].read == 0) {
            return -1;
        }
        return devlist[file->major].read(len, dst, user);
    }
    
    if(file->type == FD_FILE) {
        // 普通文件
        inode_lock(file->ip);
        uint32 r = inode_read_data(file->ip, file->offset, len, (void*)dst, user);
        file->offset += r;
        inode_unlock(file->ip);
        return r;
    }
    
    if(file->type == FD_DIR) {
        // 目录文件
        inode_lock(file->ip);
        uint32 r = dir_get_entries(file->ip, len, (void*)dst, user);
        inode_unlock(file->ip);
        return r;
    }
    
    return -1;
}

// 文件内容写入
// 返回写入的字节数
uint32 file_write(file_t* file, uint32 len, uint64 src, bool user)
{
    if(!file->writable) {
        return -1;
    }
    
    if(file->type == FD_DEVICE) {
        // 设备文件，调用设备的写函数
        if(file->major >= N_DEV || devlist[file->major].write == 0) {
            return -1;
        }
        return devlist[file->major].write(len, src, user);
    }
    
    if(file->type == FD_FILE) {
        // 普通文件
        inode_lock(file->ip);
        uint32 w = inode_write_data(file->ip, file->offset, len, (void*)src, user);
        file->offset += w;
        inode_unlock(file->ip);
        return w;
    }
    
    // 目录文件不支持写入
    return -1;
}

// flags 可能取值
#define LSEEK_SET 0  // file->offset = offset
#define LSEEK_ADD 1  // file->offset += offset
#define LSEEK_SUB 2  // file->offset -= offset

// 修改file->offset (只针对FD_FILE类型的文件)
uint32 file_lseek(file_t* file, uint32 offset, int flags)
{
    if(file->type != FD_FILE) {
        return -1;
    }
    
    inode_lock(file->ip);
    
    uint32 new_offset;
    
    switch(flags) {
        case LSEEK_SET:
            new_offset = offset;
            break;
        case LSEEK_ADD:
            new_offset = file->offset + offset;
            break;
        case LSEEK_SUB:
            if(file->offset < offset) {
                new_offset = 0;
            } else {
                new_offset = file->offset - offset;
            }
            break;
        default:
            inode_unlock(file->ip);
            return -1;
    }
    
    // 检查新偏移是否超出文件大小
    if(new_offset > file->ip->size) {
        inode_unlock(file->ip);
        return -1;
    }
    
    file->offset = new_offset;
    
    inode_unlock(file->ip);
    
    return 0;
}

// file->ref++ with lock
file_t* file_dup(file_t* file)
{
    spinlock_acquire(&lk_ftable);
    assert(file->ref > 0, "file_dup: ref");
    file->ref++;
    spinlock_release(&lk_ftable);
    return file;
}

// 获取文件状态
int file_stat(file_t* file, uint64 addr)
{
    file_state_t state;
    if(file->type == FD_FILE || file->type == FD_DIR)
    {
        inode_lock(file->ip);
        state.type = file->ip->type;
        state.inode_num = file->ip->inode_num;
        state.nlink = file->ip->nlink;
        state.size = file->ip->size;
        inode_unlock(file->ip);

        uvm_copyout(myproc()->pgtbl, addr, (uint64)&state, sizeof(file_state_t));
    }
    return -1;
}