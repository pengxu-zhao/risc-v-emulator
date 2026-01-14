#ifndef UART_H
#define UART_H


#include "common.h"

#include "plic.h"

/* Opaque CPU callback for raising/clearing IRQs */
typedef void (*uart_irq_cb_t)(void *cpu_opaque, int raise, int irq_num);

/* Device structure */
typedef struct UARTDevice {
    int serial_fd;              // 串口设备文件描述符
    char serial_device[256];    // 串口设备路径

    uint8_t rbr;    // 接收缓冲寄存器
    uint8_t thr;    // 发送保持寄存器
    uint8_t ier;    // 中断使能寄存器

    uint8_t fcr;    //FIFO 控制寄存器
    uint8_t iir;    // 中断标识寄存器
          //bit0 = 1 表示无中断，0 表示有中断；bits[3:1] 标示中断类型（优先级）。
    uint8_t lcr;    // 线路控制寄存器
    uint8_t mcr;    // Modem 控制寄存器
    uint8_t lsr;    // 线路状态寄存器
    uint8_t msr;    // Modem 状态寄存器
    uint8_t scr;    // 暂存寄存器

      // 波特率除数寄存器
    uint8_t dll;  // Divisor Latch Low byte
    uint8_t dlm;  // Divisor Latch High byte

    uint8_t fifo_enable;
    uint8_t dma_mode;
    uint8_t rx_trigger_level;
    // 内部状态
    uint32_t baud_rate;
    
    uint64_t base_addr;         // mmio base
    // registers:
    uint32_t ctrl;
    /* ctrl
    使能位（enable）：打开或关闭 UART。
    中断使能位（interrupt enable）：是否允许 TX/RX 的中断。
    波特率配置：决定传输速率（在简化模拟器里常常忽略，或者写死一个速率）。
    数据位/停止位/奇偶校验：在真实硬件里会配置这些通信格式，但在最小模拟器中往往简化掉。
    */
    uint32_t irq_status;
    /* irq_status
    TX 空了（可写）：可以往发送 FIFO 里写数据。
    RX 满了（有数据）：接收到了新的字节。
    错误状态：比如溢出、帧错误、奇偶校验错误等（简单模拟里一般不实现）。
    */


    // circular buffers
    uint8_t tx_buf[UART_TX_BUF_SIZE];
    uint32_t tx_head, tx_tail; // head: next write index, tail: next read index
    uint32_t tx_count;

    uint8_t rx_buf[UART_RX_BUF_SIZE];
    uint32_t rx_head, rx_tail;
    uint32_t rx_count;

    // status flags are derived, but cached helpers can be kept
    pthread_mutex_t lock;
    pthread_mutex_t tx_buf_lock;
    pthread_cond_t  tx_cond;

    // 终端设置
    struct termios original_termios;
    bool terminal_configured;
    
    bool running;

    // host integration
    void *cpu_opaque;
    /*
    作用：保存一个“指向 CPU 的私有上下文指针”。
    在设备模拟里，我们经常需要让 设备回调 CPU，比如触发中断。
    但是设备代码不能直接依赖 CPU 的具体结构（避免耦合太死）。
    所以用一个 void* 来存 CPU 的上下文（opaque = “不透明”），
    当设备要告诉 CPU 某个事件时，就把这个指针原封不动传回去。
    可以理解为：设备不关心 CPU 内部怎么实现，只管把个“句柄”还给 CPU。
    */
    uart_irq_cb_t irq_cb;
    int irq_num; //UART 设备的 中断号
    int irq_pending;

    // thread handles
    pthread_t tx_thread;
    pthread_t rx_thread;
 
    uint64_t bit_time_ps;    // 每位的时间，单位皮秒 (picoseconds) 以便高精度

    // 发送状态机
    bool tx_in_progress;     // 是否正在发送一帧数据
    uint8_t tx_current_byte; // 当前正在发送的字节
    int tx_bit_pos;          // 当前发送到了第几位 (0=起始位，1-8=数据位...)
    uint64_t tx_next_bit_time; // 下一个位变化的时间点（你的模拟器的时间）

    void *plic;

} UARTDevice;

UARTDevice *uart_create(uint64_t base_addr, void *cpu_opaque, int irq_num);
uart_irq_cb_t cpu_irq_raise_cb(void *opaque, int level, int irq_num);
void simulator_printc(char c);
uint32_t mmio_read(UARTDevice *uart,uint64_t offset,int size);
void mmio_write(UARTDevice *uart,uint64_t offset, uint32_t val, int size);
void uart_cleanup(UARTDevice* uart);
void uart_destroy(UARTDevice *u);
#endif