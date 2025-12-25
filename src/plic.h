#ifndef PLIC_H
#define PLIC_H
#include <stdint.h>
#include <stdbool.h>
#include "cpu.h"

#define PLIC_BASE 0x0c000000L
#define PLIC_SIZE 0x4000000

#define MAX_IRQS 1023
// 系统设备
#define UART_IRQ         10    // 串口控制器
#define TIMER_IRQ        11    // 系统定时器

// 存储设备
#define DISK_IRQ         20    // 磁盘控制器 (IDE/SATA)
#define USB_IRQ          21    // USB 控制器

// 网络设备
#define NET_RX_IRQ       30    // 网络接收
#define NET_TX_IRQ       31    // 网络发送

// PCI 设备
#define PCI_IRQ          40    // PCI 总线中断
#define PCI_DEVICE1_IRQ  41    // PCI 设备1
#define PCI_DEVICE2_IRQ  42    // PCI 设备2

// 多媒体设备
#define AUDIO_IRQ        50    // 音频控制器
#define VIDEO_IRQ        51    // 视频控制器

// GPIO 和其他外设
#define GPIO_IRQ         60    // GPIO 中断
#define SPI_IRQ          61    // SPI 控制器
#define I2C_IRQ          62    // I2C 控制器


#define VIRTIO_IRQ       70    // VirtIO 设备

typedef struct {
    // 优先级寄存器 (每个中断源1个，4字节对齐)
    uint32_t priority[1024];
    
    // 中断使能寄存器 (每个hart 1组)
    uint32_t enable[MAX_CORES][1024 / 32];  // 按位使能
    
    // 每个hart的阈值和索赔/完成寄存器
    uint32_t threshold[MAX_CORES];
    uint32_t claim_complete;
    
    // 内部状态
    uint32_t pending[32];           // 待处理中断位图
    uint32_t claimed[MAX_CORES][32];           // 已索赔中断位图
    uint32_t current_irq[MAX_CORES];

} PLICState;

static PLICState plic;


uint64_t plic_read(void *opaque,uint64_t addr, int size);
void plic_write(void *opaque,uint64_t addr, uint64_t value, int size);
void plic_set_irq(int irq, int level) ;
void plic_init(void);
void plic_set_irq_to_hart( int irq, int level, int target_hart);
#endif