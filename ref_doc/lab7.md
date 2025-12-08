实验七:文件系统

QEMU模拟的硬件环境

RISC-V体系结构规定了三种特权模式:用户模式(User mode, U-mode)、监管者模式(Supervisor mode, S-mode)、机器模式(Machine mode, M-mode)。

实验目标:理解现代文件系统的核心概念和实现原理，实现一个简单的文件系统。

1. 理解文件系统的磁盘布局

   学习如何将磁盘划分为:引导块、超级块、inode位图、数据位图、inode区、数据区

   掌握磁盘块分配和回收的机制

   理解元数据(如超级块)的作用
2. 掌握文件系统的基本抽象

   ■ 文件:作为字节序列的抽象

   ■ 目录:作为文件名到inode编号映射的特殊文件

   inode:理解其作为文件元数据核心载体的作用(权限、大小、数据块指针等)
3. 实现关键系统调用

   ■ 文件操作:open, read, write, close

   ■ 目录操作:mkdir, link, unlink

   ■ 文件描述符管理
4. 理解路径解析机制

   实现从路径名到inode的查找过程

   处理绝对路径和相对路径

   理解当前工作目录的概念

OS实验七:文件系统-底层设备

syscall about file

file

中层抽象

inode+dirent+path

pipe console

buf+ bitmap

磁盘驱动

底层设备

QEMU虚拟磁盘

首先要在根目录下makefile文件*QEMUOPTS*中添加文件系统映像和磁盘块设备的选项。

QEMUOPTS+=-drive file=$(FS_IMG),if=none,format=raw,id=x0

QEMUOPTS+=-device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

它会把一个名为 *$(FS_IMG)* 的文件作为操作系统的文件系统映像装入QEMU模拟的虚拟磁盘。

任务一:理解磁盘映像的制作过程

磁盘在文件系统角度可以理解为一个以*block*为读写单位的大数组...

disk layout:[ super block | inode bitmap | inode blocks | data bitmap | data blocks ]

* super block包括磁盘和磁盘上文件系统的重要元数据
* inode bitmap标记inode blocks区域里各个inode的分配情况(0:可分配 1:已分配)
* inode blocks这个区域由若干连续的inode组成
* data bitmap标记data blocks区域里各个block的分配情况(0:可分配 1:已分配)
* data blocks这个区域由若干连续的 block组成

  ■ kfs.c会填写super block中的信息，其他四个区域会全部填写为 0这个非常简单的磁盘映像就制作完毕了

任务二:磁盘驱动

在 QEMU为我们提供了一套读写虚拟磁盘的方法，磁盘驱动的实现涉及三个部分：

■ virtio.c定义了磁盘的数据结构（由于非常复杂且涉及硬件规范，已经给出完整代码，只需要了解它为上层提供的接口即可）

■ kvm.c和 PLIC、CLINT、UART一样，由于使用了内存映射寄存器，需要在内核页表中加入virtio的映射

■ trap_kernel.c和时钟中断、UART中断一样，virtio也需要在中断发生时调用中断响应函数

需要修改的文件：

memlayout.h中增加定义：

// virtio相关

```c++
# define VIRTIO_BASE 0x10001000ul
# define VIRTIO_IRQ 1
```

common.h中增加定义：

```c++
define BLOCK_SIZE 1024 // 磁盘的block大小
```

缓冲区(buffer)

读写磁盘的过程：

1. 主机向磁盘发送一个读请求，包括要读取的block序号和内存存放位置
2. 磁盘响应请求完成数据传递
3. 主机查看内存中数据并进行修改（如有）
4. 如果修改，需将数据写回磁盘

buf的组织结构采用双向循环链表，以head_buf为头节点，buf_cache里的节点作为可分配回收的资源节点。

typedef struct buf {

spinlock_t slk;

uint32 block_num;

uint8 data[BLOCK_SIZE];

uint32 buf_ref;

bool disk;

} buf_t;

双向循环链表将buffer的申请和释放转换成链表的插入和删除操作。采用LRU策略和懒惰写回策略优化buffer与磁盘的协作。

super block的读取及文件系统初始化(fs.c)分为两个步骤：使用buf层函数读取磁盘s并填写到内存super_block_t结构体。由于buf层操作涉及锁，文件系统初始化在第一个进程的fork_return()时机进行。

bitmap的管理(bitmap.c)使用两块bitmap各占1个block。申请/释放block或inode时对应bit置1/0。

inode的管理：

```c++
typedef struct inode {
// 磁盘信息（由slk保护）
uint16 type;
uint16 major;
uint16 minor;
uint16 nlink;
uint32 size;
uint32 addrs[N_ADDRS];
// 内存信息
uint16 inode_num;
uint32 ref;
bool valid;
sleeplock_t slk;
} inode_t;
```

inode管理函数包括：

* inode_init()
* inode_rw()
* inode_alloc()
* inode_create()
* inode_free()
* inode_lock()/unlock()/unlock_free()

目录管理：

```c++
typedef struct dirent {
uint16 inode_num;
char name[DIR_NAME_LEN];
} dirent_t
```

文件路径解析通过path_to_inode()和path_to_pinode()实现，search_inode()提供路径解析能力。

上层接口将文件统一为四类（普通文件、目录、设备文件、管道文件），通过sysfile.c实现系统调用。最终实现proc_exec()函数执行ELF文件。
