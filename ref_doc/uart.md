## UART基本原理
**UART是一种硬件设备，用于实现异步串行通信**。它的核心功能是：

- **并行转串行**：将CPU通过总线传送的并行数据（例如，一个8位的字节）转换为一位一位依次发送的串行数据流。

- **串行转并行**：将接收到的串行数据流转换回并行数据，供CPU读取。

- **异步**：通信双方没有共享的时钟信号，而是依靠预先约定好的波特率（Baud Rate，如9600, 115200）来同步时序。发送端和接收端各自使用自己的时钟，但只要频率相近，就可以正确解码。
#### 数据格式：
一个典型的**UART数据帧**包含：

    1.起始位：一个逻辑低电平，表示一帧数据的开始。

    2.数据位：通常是5-8位的数据（一个字节）。

    3.校验位（可选）：用于简单的错误检测。

    4.停止位：1-2个逻辑高电平，表示一帧数据的结束。

在xv6中，UART最重要的用途是提供控制台（Console）功能。键盘的输入和终端的输出都是通过UART来传输的。

## RISC-V 平台上的 UART 硬件
在RISC-V架构的硬件平台上（无论是真实的芯片如SiFive的，还是QEMU这样的模拟器），UART设备通常是通过内存映射I/O（Memory-Mapped I/O, MMIO）的方式连接到CPU的。

**什么是内存映射I/O（MMIO）？**
- CPU与外部设备（如UART、磁盘控制器等）通信的方式主要有两种：端口I/O和内存映射I/O。

- RISC-V采用内存映射I/O。这意味着，设备控制器（如UART）的寄存器被映射到物理内存地址空间中的特定地址。

- 对程序员来说，访问设备就像读写一段特殊的内存一样。例如，向地址0x10000000写入一个字节，**这个操作并不会改变DRAM内存的内容**，而是由硬件**总线拦截**，并将这个写入操作转发给UART设备，UART会将这个字节通过串口发送出去。

**QEMU的 Virt 机器中的UART**
在xv6默认运行的QEMU virt机器中，NS16550A兼容的UART控制器被映射到了物理地址 **0x10000000**。

这个UART设备有一组寄存器，每个寄存器都有其对应的偏移地址：

| 偏移量 | 寄存器缩写 | 名称 | 作用描述 |
|--------|------------|------|----------|
| 0 | RHR | 接收保持寄存器 | 读操作：从该寄存器读取接收到的字节 |
| 0 | THR | 发送保持寄存器 | 写操作：向该寄存器写入要发送的字节 |
| 1 | IER | 中断使能寄存器 | 控制是否在特定事件（如数据到达）时产生中断 |
| 5 | LSR | 线路状态寄存器 | 最重要的寄存器，提供状态信息，例如bit 5（THRE）表示发送保持寄存器是否为空（可写入新数据），bit 0（DR）表示是否有数据到达（可读取） |

**关键点**：由于寄存器被映射到内存，我们可以通过**指针直接访问**它们。例如，(uint8*)(0x10000000 + 5) 就指向LSR寄存器。

## xv6 中 UART 驱动的实现原理
xv6的UART驱动程序（源码位于 devs/uart.c）就是基于上述MMIO原理，通过读写这些“特殊的内存地址”来控制硬件设备的。

**1. 初始化 (uartinit)** 

在系统启动时，初始化UART硬件：

- 配置波特率（设置时钟分频器寄存器）。

- 配置数据格式（数据位、停止位等）。

- 使能接收中断（IER），这样当键盘有数据输入时，UART会向CPU发出中断请求，CPU就可以及时处理输入，而不需要不断轮询。
```c
void
uartinit(void)
{

  // 关闭中断
  // IER寄存器控制着芯片上所有的中断的使能
  // 这一步相当于关闭了所有UART可能发出的中断
  // IER bit0: 管理receiver ready register中断
  // IER bit1: 管理transmitter empty register中断
  // IER bit2: 管理receiver line status register中断
  // IER bit3: 管理modem status register中断
  // IER bit4-7: 硬连线为0
  WriteReg(IER, 0x00);

  // 进入设置波特率的特殊模式
  // 当向LCR(Line Control Register)最高位(bit7)写入1时
  // 这将会改变地址000和001处两个寄存器的含义
  // 000地址在普通模式下对应RHR和LHR两个寄存器，一个只读、一个只写，因此共用一个地址
  // 001地址在普通模式下对应IER寄存器，就是上面管理中断的寄存器
  // 在设置波特率的模式下，000和001分别对应DLL DLM两个寄存器，用来确定波特率
  WriteReg(LCR, LCR_BAUD_LATCH);

  // 根据查表可知要将DLL、DLM两个寄存器分别设置为0和3
  // 之所以设置为38.4K的波特率，与qemu的具体实现代码有关
  // (请参考qemu/hw/riscv/virt.c/create_fdt_uart函数)，波特率为3686400
  WriteReg(0, 0x03);

  // MSB for baud rate of 38.4K.
  WriteReg(1, 0x00);


  // 离开波特率设置模式
  // 设置传输字长为8bit，不含奇偶校验位
  // LCR的低两位设置为00、01、10、11时，分别对应5、6、7、8bit的字长
  // 这里设置为8bit字长，即一个字节
  WriteReg(LCR, LCR_EIGHT_BITS);

  // 重置并使能IO
  // FCR_FIFO_ENABLE标志用于使能输入输出两个FIFO
  // FCR_FIFO_CLEAR标志用于清空两个FIFO并将其计数逻辑设置为0
  WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);

  // 使能输入输出中断
  // 一旦同时使能了输入(RX)中断和FIFO，UART就会在到达trigger level时向CPU发起一个中断
  // (这个trigger level默认值为1)，同样，在输出THR为空时也会向CPU发起一个中断
  WriteReg(IER, IER_TX_ENABLE | IER_RX_ENABLE);
  
  // 初始化串口芯片输出缓冲区的锁
  // Xv6内核里给输出又设置了一个缓冲区，默认大小为32
  // 事实上在16550芯片内部TX和RX都有一个16字节的硬件FIFO作为缓冲
  // 但是Xv6内核实际上还是一个个地发送和接收字节的，相当于将这层硬件缓冲透明化了
  initlock(&uart_tx_lock, "uart");
}
```
上述就是UART芯片16550的全部初始化流程，需要注意的是除了16550芯片中已经拥有的硬件FIFO，Xv6内核中还设置了一个软件缓冲区`uart_tx_buf`用来暂存UART即将要发送的数据，与之一并定义的还有**两枚读写指针**和一个用于管理进程并发的自旋锁，代码如下:
```c
// 输出缓冲区
struct spinlock uart_tx_lock;
#define UART_TX_BUF_SIZE 32
char uart_tx_buf[UART_TX_BUF_SIZE];
uint64 uart_tx_w; // write next to uart_tx_buf[uart_tx_w % UART_TX_BUF_SIZE]
uint64 uart_tx_r; // read next from uart_tx_buf[uart_tx_r % UART_TX_BUF_SIZE]
```
**判空条件：uart_tx_w == uart_tx_r
判满条件：uart_tx_r + 32 == uart_tx_w**

**2. 发送一个字符 (uartputc)**

操作系统想要输出一个字符（例如，通过 printf）到控制台，最终会调用到这个函数。uartputc函数中做的主要就是尝试将一个字符放入上述的发送缓冲区(环形队列)中，如果**缓冲区已满就让进程陷入睡眠状态**，等到缓冲区有空位让出，而**真正的发送动作是在uartstart函数中完成**的。
```c
// 将一个字符放入输出缓冲区，如果UART还没有发送就告知它
// 如果输出缓冲区满了就阻塞
// 因为此函数可能被阻塞，所以它不能从中断中被调用，只适合被write使用
void
uartputc(int c)
{
  // 获取输出缓冲区(环形队列)的锁
  acquire(&uart_tx_lock);
  
  // 如果内核发生故障，直接陷入死循环
  // 程序失去响应
  if(panicked){
    for(;;)
      ;
  }
  
  // 否则尝试将字符放入发送缓冲区中并开始发送
  while(1){
    if(uart_tx_w == uart_tx_r + UART_TX_BUF_SIZE){
      // 缓冲区已满，等候uartstart函数在buffer中开辟出新的空间
      // 让当前线程休眠在uart_tx_r这个channel上，等待被唤醒
      // 关于锁与并发机制的更多细节在后面的博客会一一分析
      sleep(&uart_tx_r, &uart_tx_lock);
    } else {
      
      // 如果缓冲区未满，则将字符放入缓冲区，并调整指针
      // 使用uartstart函数告知UART准备发送
      // 最后释放锁
      uart_tx_buf[uart_tx_w % UART_TX_BUF_SIZE] = c;
      uart_tx_w += 1;
      uartstart();
      release(&uart_tx_lock);
      return;
    }
  }
}
```

**流程：**

    1.读取线路状态寄存器（LSR），检查 THRE（发送保持寄存器空）位是否为1。

    2.如果为空，说明UART已经准备好接收一个新的字节进行发送。此时，程序将字符 c 写入到 发送保持寄存器（THR）的地址。

    3.一旦数据写入THR，UART硬件会自动接管，开始将字节转换成串行数据流并从TX引脚发送出去。

    4.如果寄存器不为空（即UART还在发送上一个字节），驱动会等待（轮询）或者进入睡眠，等待发送完成中断（在xv6中，发送部分采用了轮询而非中断以简化设计）。

在这个函数中进一步调用了uartstart函数，我们再去研究一下这个函数，uartstart函数是**直接驱动UART芯片发送数据**的函数，它会首先检测一些条件，条件一旦满足就向UART的THR寄存器开始写入要发送的字符，驱动UART芯片向外发送数据，完整代码和注释如下：
```c
// 如果UART在空闲状态，字符正在发送缓冲区中等待
// 那么直接发送之，调用者必须持有uart_tx_lock锁
// 在驱动的上半、下半部分均会调用
// 驱动的上半部分指用户或内核可以调用的函数接口
// 驱动的下半部分指的是中断处理程序本身
// 事实上，uartstart函数会被两个地方调用
// 一个是我们刚刚看到的uartputc函数，对应驱动的上半部分
// 还会被uartintr函数调用，这部分则是驱动的下半部分
void
uartstart()
{
  while(1){
    // 如果发送缓冲区为空，则直接返回
    if(uart_tx_w == uart_tx_r){
      // transmit buffer is empty.
      return;
    }
    
    // 缓冲区中有字符等待发送，但是UART还没有完成上一次发送
    // 这时也不可以发送成功，直接返回
    // ReadReg和上面介绍的WriteReg宏类似，用来读取一个UART寄存器的值
    if((ReadReg(LSR) & LSR_TX_IDLE) == 0){
      // UART THR寄存器仍为满
      // 此时不能给它另外一个字节，所以只能等它准备好时主动发起中断
      return;
    }
    
    // 如果发送缓冲区中有字符并且UART正处于空闲状态
    // 则可以准备发送，读取字符并调整读指针
    int c = uart_tx_buf[uart_tx_r % UART_TX_BUF_SIZE];
    uart_tx_r += 1;
    
    // 也许uartputc函数正等待缓冲区中有新的空间
    // 这里直接唤醒之前在uart_tx_r地址上进行睡眠等待的锁
    // 其实也就是将进程状态更改为RUNNING，从而进入调度队列
    // 和uartputc中的sleep对应
    wakeup(&uart_tx_r);
    
    // 将数据写入UART的THR寄存器，这个值将会被UART自动移入
    // TSR(transmit shift register)寄存器，一位位地串行发送出去
    WriteReg(THR, c);
  }
}
```
将此字符放入输出缓冲区并驱动UART芯片将其发送出去，而这个由qemu模拟出来的UART 16550芯片的输出通道TX默认会连接到我们计算机的显示器上。

**3. 接收一个字符 (uartgetc)**

当有数据从串口线（例如从键盘）传来时，驱动需要读取它。
```c
int uartgetc(void) {
  if(uartstatus & UART_LSR_DR){
    // 检查数据就绪位（DR）
    // 数据已就绪，从接收保持寄存器（RHR）读取数据
    return uartrxreg;
  } else {
    // 没有数据
    return -1;
  }
}
```
**流程：**

    1.读取线路状态寄存器（LSR），检查 DR（数据就绪）位是否为1。

    2.如果为1，说明UART已经接收到了一个完整的字节并将其放入了接收保持寄存器（RHR）。

    3.从RHR的地址读取该字节的值并返回。

**4. 处理中断 (uartintr)**
这是驱动与RISC-V中断机制交互的关键部分。

- 当UART事件发生时（例如接收到了数据），UART硬件会向RISC-V平台的平台级中断控制器（PLIC）发出一个中断请求。

- PLIC会将这个中断路由到CPU核心。

- CPU核心在执行完当前指令后，会跳转到预先设置好的中断处理程序（在RISC-V中，这个入口点是 trampoline.S）。

- 最终，会调用到 devintr 函数（user/trap.c），该函数检查中断来源。

- 如果发现是UART中断，就会调用 uartintr 函数。
  
**uartintr 函数的工作是**：

    1.循环调用 uartgetc，读取所有已经到达的输入字符。

    2.将这些字符放入控制台缓冲区中，以便后续的 read 系统调用可以获取它们。

    3.如果是发送中断，也会进行处理，但xv6的发送默认没有使用中断。