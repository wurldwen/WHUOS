#include "fs/fs.h"
#include "fs/buf.h"
#include "fs/inode.h"
#include "fs/dir.h"
#include "fs/bitmap.h"
#include "lib/str.h"
#include "lib/print.h"
#include "proc/cpu.h"
#include "mem/vmem.h"

// 对目录文件的简化性假设: 每个目录文件只包括一个block
// 也就是每个目录下最多 BLOCK_SIZE / sizeof(dirent_t) = 32 个目录项

// 查询一个目录项是否在目录里
// 成功返回这个目录项的inode_num
// 失败返回INODE_NUM_UNUSED
// ps: 调用者需持有pip的锁
uint16 dir_search_entry(inode_t *pip, char *name)
{
    assert(spinlock_holding(&pip->slk), "dir_search_entry: lock");
    assert(pip->type == FT_DIR, "dir_search_entry: not dir");
    
    dirent_t de;
    
    // 遍历目录中的所有目录项
    for(uint32 offset = 0; offset < pip->size; offset += sizeof(dirent_t)) {
        // 读取目录项
        if(inode_read_data(pip, offset, sizeof(de), &de, false) != sizeof(de)) {
            panic("dir_search_entry: read failed");
        }
        
        // 检查名字是否匹配
        if(de.name[0] != 0 && strncmp(name, de.name, DIR_NAME_LEN) == 0) {
            return de.inode_num;
        }
    }
    
    return INODE_NUM_UNUSED;
}

// 在pip目录下添加一个目录项
// 成功返回这个目录项的偏移量 (同时更新pip->size)
// 失败返回BLOCK_SIZE (没有空间 或 发生重名)
// ps: 调用者需持有pip的锁
uint32 dir_add_entry(inode_t *pip, uint16 inode_num, char *name)
{
    assert(spinlock_holding(&pip->slk), "dir_add_entry: lock");
    assert(pip->type == FT_DIR, "dir_add_entry: not dir");
    
    // 检查名字长度
    if(strlen(name) >= DIR_NAME_LEN) {
        printf("dir_add_entry: name too long\n");
        return BLOCK_SIZE;
    }
    
    // 检查是否重名
    if(dir_search_entry(pip, name) != INODE_NUM_UNUSED) {
        printf("dir_add_entry: name exists\n");
        return BLOCK_SIZE;
    }
    
    // 查找空闲目录项或在末尾添加
    dirent_t de;
    uint32 offset;
    
    for(offset = 0; offset < BLOCK_SIZE; offset += sizeof(dirent_t)) {
        if(offset < pip->size) {
            // 读取现有目录项
            if(inode_read_data(pip, offset, sizeof(de), &de, false) != sizeof(de)) {
                panic("dir_add_entry: read failed");
            }
            
            // 找到空闲位置
            if(de.name[0] == 0) {
                break;
            }
        } else {
            // 已到达文件末尾
            break;
        }
    }
    
    // 检查是否超出目录大小限制
    if(offset >= BLOCK_SIZE) {
        printf("dir_add_entry: directory full\n");
        return BLOCK_SIZE;
    }
    
    // 创建新的目录项
    de.inode_num = inode_num;
    strncpy(de.name, name, DIR_NAME_LEN - 1);
    de.name[DIR_NAME_LEN - 1] = 0;
    
    // 写入目录项
    if(inode_write_data(pip, offset, sizeof(de), &de, false) != sizeof(de)) {
        panic("dir_add_entry: write failed");
    }
    
    return offset;
}

// 在pip目录下删除一个目录项
// 成功返回这个目录项的inode_num
// 失败返回INODE_NUM_UNUSED
// ps: 调用者需持有pip的锁
uint16 dir_delete_entry(inode_t *pip, char *name)
{
    assert(spinlock_holding(&pip->slk), "dir_delete_entry: lock");
    assert(pip->type == FT_DIR, "dir_delete_entry: not dir");
    
    dirent_t de;
    
    // 遍历目录项查找要删除的项
    for(uint32 offset = 0; offset < pip->size; offset += sizeof(dirent_t)) {
        // 读取目录项
        if(inode_read_data(pip, offset, sizeof(de), &de, false) != sizeof(de)) {
            panic("dir_delete_entry: read failed");
        }
        
        // 找到匹配的目录项
        if(de.name[0] != 0 && strncmp(name, de.name, DIR_NAME_LEN) == 0) {
            uint16 inum = de.inode_num;
            
            // 清空目录项
            memset(&de, 0, sizeof(de));
            
            // 写回
            if(inode_write_data(pip, offset, sizeof(de), &de, false) != sizeof(de)) {
                panic("dir_delete_entry: write failed");
            }
            
            return inum;
        }
    }
    
    return INODE_NUM_UNUSED;
}

// 把目录下的有效目录项复制到dst (dst区域长度为len)
// 返回读到的字节数 (sizeof(dirent_t)*n)
// 调用者需要持有pip的锁
uint32 dir_get_entries(inode_t* pip, uint32 len, void* dst, bool user)
{
    assert(spinlock_holding(&pip->slk), "dir_get_entries: lock");
    assert(pip->type == FT_DIR, "dir_get_entries: not dir");
    
    dirent_t de;
    uint32 total = 0;
    
    // 遍历目录项
    for(uint32 offset = 0; offset < pip->size && total + sizeof(de) <= len; offset += sizeof(dirent_t)) {
        // 读取目录项
        if(inode_read_data(pip, offset, sizeof(de), &de, false) != sizeof(de)) {
            break;
        }
        
        // 只复制有效目录项（非空）
        if(de.name[0] != 0) {
            if(user) {
                // 复制到用户空间
                uvm_copyout(myproc()->pgtbl, (uint64)dst + total, (uint64)(char*)&de, sizeof(de));
            } else {
                // 复制到内核空间
                memmove((char*)dst + total, &de, sizeof(de));
            }
            total += sizeof(de);
        }
    }
    
    return total;
}

// 改变进程里存储的当前目录
// 成功返回0 失败返回-1
uint32 dir_change(char* path)
{
    // 获取路径对应的inode
    inode_t* ip = path_to_inode(path);
    if(ip == 0) {
        return -1;
    }
    
    inode_lock(ip);
    
    // 检查是否为目录
    if(ip->type != FT_DIR) {
        inode_unlock_free(ip);
        return -1;
    }
    
    inode_unlock(ip);
    
    // 更新当前进程的工作目录
    proc_t* proc = myproc();
    if(proc->cwd) {
        inode_free(proc->cwd);
    }
    proc->cwd = ip;
    
    return 0;
}

// 输出一个目录下的所有有效目录项
// for debug
// ps: 调用者需持有pip的锁
void dir_print(inode_t *pip)
{
    assert(spinlock_holding(&pip->slk), "dir_print: lock");

    printf("\ninode_num = %d dirents:\n", pip->inode_num);

    dirent_t *de;
    buf_t *buf = buf_read(pip->addrs[0]);
    for (uint32 offset = 0; offset < BLOCK_SIZE; offset += sizeof(dirent_t))
    {
        de = (dirent_t *)(buf->data + offset);
        if (de->name[0] != 0)
            printf("inum = %d dirent = %s\n", de->inode_num, de->name);
    }
    buf_release(buf);
}

/*----------------------- 路径(一串目录和文件) -------------------------*/

// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
static char *skip_element(char *path, char *name)
{
    while(*path == '/') path++;
    if(*path == 0) return 0;

    char *s = path;
    while (*path != '/' && *path != 0)
        path++;

    int len = path - s;
    if (len >= DIR_NAME_LEN) {
        memmove(name, s, DIR_NAME_LEN);
    } else {
        memmove(name, s, len);
        name[len] = 0;
    }
    while (*path == '/')
        path++;
    return path;
}

// 查找路径path对应的inode (find_parent = false)
// 查找路径path对应的inode的父节点 (find_parent = true)
// 供两个上层函数使用
// 失败返回NULL
static inode_t* search_inode(char* path, char* name, bool find_parent)
{
    inode_t* ip;
    inode_t* next;
    
    // 确定起始inode
    if(path[0] == '/') {
        // 绝对路径，从根目录开始
        ip = inode_alloc(INODE_ROOT);
    } else {
        // 相对路径，从当前目录开始
        proc_t* proc = myproc();
        ip = inode_dup(proc->cwd);
    }
    
    // 逐级解析路径
    while((path = skip_element(path, name)) != 0) {
        inode_lock(ip);
        
        // 检查是否为目录
        if(ip->type != FT_DIR) {
            inode_unlock_free(ip);
            return 0;
        }
        
        // 如果需要父节点且这是最后一级，返回当前节点
        if(find_parent && *path == '\0') {
            inode_unlock(ip);
            return ip;
        }
        
        // 在当前目录中查找下一级
        uint16 inum = dir_search_entry(ip, name);
        if(inum == INODE_NUM_UNUSED) {
            inode_unlock_free(ip);
            return 0;
        }
        
        inode_unlock(ip);
        
        // 获取下一级inode
        next = inode_alloc(inum);
        inode_free(ip);
        ip = next;
    }
    
    // 如果需要父节点但路径只有一级
    if(find_parent) {
        inode_free(ip);
        return 0;
    }
    
    return ip;
}

// 找到path对应的inode
inode_t* path_to_inode(char* path)
{
    char name[DIR_NAME_LEN];
    return search_inode(path, name, false);
}

// 找到path对应的inode的父节点
// path最后的目录名放入name指向的空间
inode_t* path_to_pinode(char* path, char* name)
{
    return search_inode(path, name, true);
}

// 如果path对应的inode存在则返回inode
// 如果path对应的inode不存在则创建inode
// 失败返回NULL
inode_t* path_create_inode(char* path, uint16 type, uint16 major, uint16 minor)
{
    char name[DIR_NAME_LEN];
    
    // 获取父目录
    inode_t* pip = path_to_pinode(path, name);
    if(pip == 0) {
        return 0;
    }
    
    inode_lock(pip);
    
    // 检查是否已存在
    uint16 inum = dir_search_entry(pip, name);
    if(inum != INODE_NUM_UNUSED) {
        inode_unlock(pip);
        inode_free(pip);
        
        // 已存在，返回现有inode
        return inode_alloc(inum);
    }
    
    // 创建新inode
    inode_t* ip = inode_create(type, major, minor);
    if(ip == 0) {
        inode_unlock_free(pip);
        return 0;
    }
    
    inode_lock(ip);
    
    // 如果是目录，添加 . 和 .. 目录项
    if(type == FT_DIR) {
        ip->nlink++;  // 为 . 增加链接
        inode_rw(ip, true);
        
        // 添加 . 目录项
        if(dir_add_entry(ip, ip->inode_num, ".") == BLOCK_SIZE) {
            panic("path_create_inode: . failed");
        }
        
        // 添加 .. 目录项
        if(dir_add_entry(ip, pip->inode_num, "..") == BLOCK_SIZE) {
            panic("path_create_inode: .. failed");
        }
    }
    
    // 在父目录中添加目录项
    if(dir_add_entry(pip, ip->inode_num, name) == BLOCK_SIZE) {
        // 添加失败，清理
        ip->nlink = 0;
        inode_unlock_free(ip);
        inode_unlock_free(pip);
        return 0;
    }
    
    // 如果创建的是目录，父目录的链接数+1（因为新目录的..指向父目录）
    if(type == FT_DIR) {
        pip->nlink++;
        inode_rw(pip, true);
    }
    
    inode_unlock_free(pip);
    inode_unlock(ip);
    
    return ip;
}

// 文件链接(目录不能被链接)
// 本质是创建一个目录项, 这个目录项的inode_num是存在的而不用申请
// 成功返回0 失败返回-1
uint32 path_link(char* old_path, char* new_path)
{
    char name[DIR_NAME_LEN];
    
    // 获取旧文件的inode
    inode_t* ip = path_to_inode(old_path);
    if(ip == 0) {
        return -1;
    }
    
    inode_lock(ip);
    
    // 目录不能被链接
    if(ip->type == FT_DIR) {
        inode_unlock_free(ip);
        return -1;
    }
    
    // 增加链接计数
    ip->nlink++;
    inode_rw(ip, true);
    inode_unlock(ip);
    
    // 获取新路径的父目录
    inode_t* pip = path_to_pinode(new_path, name);
    if(pip == 0) {
        goto bad;
    }
    
    inode_lock(pip);
    
    // 在父目录中添加新的目录项
    if(dir_add_entry(pip, ip->inode_num, name) == BLOCK_SIZE) {
        inode_unlock_free(pip);
        goto bad;
    }
    
    inode_unlock_free(pip);
    inode_free(ip);
    
    return 0;

bad:
    inode_lock(ip);
    ip->nlink--;
    inode_rw(ip, true);
    inode_unlock_free(ip);
    return -1;
}

// 检查一个unlink操作是否合理
// 调用者需要持有ip的锁
// 在path_unlink()中调用
static bool check_unlink(inode_t* ip)
{
    assert(spinlock_holding(&ip->slk), "check_unlink: slk");

    uint8 tmp[sizeof(dirent_t) * 3];
    uint32 read_len;
    
    read_len = dir_get_entries(ip, sizeof(dirent_t) * 3, tmp, false);
    
    if(read_len == sizeof(dirent_t) * 3) {
        return false;
    } else if(read_len == sizeof(dirent_t) * 2) {
        return true;
    } else {
        panic("check_unlink: read_len");
        return false;
    }
}

// 文件删除链接
uint32 path_unlink(char* path)
{
    char name[DIR_NAME_LEN];
    
    // 获取父目录
    inode_t* pip = path_to_pinode(path, name);
    if(pip == 0) {
        return -1;
    }
    
    inode_lock(pip);
    
    // 不能删除 . 和 ..
    if(strncmp(name, ".", DIR_NAME_LEN) == 0 || strncmp(name, "..", DIR_NAME_LEN) == 0) {
        inode_unlock_free(pip);
        return -1;
    }
    
    // 查找目录项
    uint16 inum = dir_search_entry(pip, name);
    if(inum == INODE_NUM_UNUSED) {
        inode_unlock_free(pip);
        return -1;
    }
    
    // 获取要删除的inode
    inode_t* ip = inode_alloc(inum);
    inode_lock(ip);
    
    // 检查是否为空目录
    if(ip->type == FT_DIR) {
        if(!check_unlink(ip)) {
            // 目录非空
            inode_unlock_free(ip);
            inode_unlock_free(pip);
            return -1;
        }
        
        // 删除目录，父目录链接数减1
        pip->nlink--;
        inode_rw(pip, true);
    }
    
    // 从父目录中删除目录项
    if(dir_delete_entry(pip, name) == INODE_NUM_UNUSED) {
        panic("path_unlink: delete_entry failed");
    }
    
    inode_unlock_free(pip);
    
    // 减少链接计数
    ip->nlink--;
    inode_rw(ip, true);
    inode_unlock_free(ip);
    
    return 0;
}