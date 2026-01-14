#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <termios.h>
#include <fcntl.h> //fcntl(), S_SETFL ,O_NONBLOCK
#include <sys/queue.h>  // 用于链表
#include <sys/types.h>
#include <elf.h>

#define MAX_MMIO_REGIONS 8

#define NUM_GPR 32
#define NUM_FGPR 32
#define CSR_COUNT 4096
#define MAX_CORES 4

#define CLINT_BASE_ADDR    0x02000000
#define CLINT_SIZE         0x10000

#define MSIP_OFFSET        0x0000      // Machine Software Interrupt Pending
#define MTIMECMP_OFFSET    0x4000      // Timer Compare Register
#define MTIME_OFFSET       0xBFF8      // Timer Register

// 重要的CSR地址定义
#define SSTATUS_SIE (1 << 1)    // bit 1: Supervisor Interrupt Enable
#define SSTATUS_SPIE (1 << 5)   // bit 5: Previous SIE
#define SSTATUS_SPP (1 << 8)    // bit 8: Previous Privilege

#define SIE_SSIE (1ULL << 1)  // bit 1: Software interrupt enable
#define SIE_STIE (1ULL << 5)  // bit 5: Timer interrupt enable
#define SIE_SEIE (1ULL << 9)  // bit 9: External interrupt enable

#define SIP_SSIP (1 << 1)   // bit 1: Software interrupt pending/enable
#define SIP_STIP (1 << 5)   // bit 5: Timer interrupt pending/enable  
#define SIP_SEIP (1 << 9)   // bit 9: External interrupt pending/enable

#define CSR_SSTATUS  0x100
#define CSR_SIE      0x104 //supervisor 
#define CSR_STVEC    0x105
#define CSR_SEPC     0x141
#define CSR_SCAUSE   0x142
#define CSR_STVAL    0X143
#define CSR_SIP      0x144
#define CSR_STIMECMP 0x14D //S mode 定时器比较寄存器，用于设置监管者模式的定时器中断。
#define CSR_SATP     0x180
#define CSR_MSTATUS  0x300  // 机器状态寄存器
#define CSR_MISA     0x301
#define CSR_MEDELEG  0x302 // 委托异常
#define CSR_MIDELEG  0x303 // 委托中断
#define CSR_MIE      0x304 //中断使能寄存器
#define CSR_MTVEC    0x305 //陷阱向量基址寄存器
#define CSR_MCOUNTERN 0x306 //机器计数器使能
#define CSR_MEPC     0x341
#define CSR_MCAUSE   0x342
#define CSR_MTVAL    0x343
#define CSR_MIP      0x344
#define CSR_MTIME    0x701
#define CSR_MTIMECMP 0x741
#define CSR_TIME     0xC01 //time - 实际是cycle计数器的别名,记录从系统启动或复位以来经过的"时间单位
#define CSR_INSTRET  0xC02 //instret - 指令退休计数器 

#define CSR_MENVCFG 0x30A // 配置机器模式下的环境相关特性

#define CSR_PMPADDRO 0x3B0
#define CSR_PMPCFG0  0x3A0 // pmp0-7
#define CSR_PMPCFG1  0x3A1 // pmp8-15

#define CSR_MVENDORID 0xF11  //厂商ID
#define CSR_MARCHID  0xF12   //架构ID
#define CSR_MIMPID   0xF13   //机器实现ID
#define CSR_MHARTID  0xF14  //硬件线程ID

#define MSTATUS_MIE        (1u << 3)   /* 全局中断使能 */
#define MSTATUS_MPIE       (1u << 7)   /* 入口时保存的 MIE */
#define MSTATUS_MPP_SHIFT  11
#define MSTATUS_MPP_MASK   (3u << MSTATUS_MPP_SHIFT)
#define MSTATUS_MXR (1 << 19)
#define MSTATUS_TVM_MASK (1 << 20) //决定是否在 S-mode（监督者模式）下执行虚拟内存相关指令（如 SFENCE.VMA）时触发异常
                                    //M-mode（机器模式）通过设置 mstatus.TVM 来限制 S-mode 的虚拟内存操作，
                                    //通常用于虚拟化（hypervisor）场景，防止访客操作系统直接操作页表或 TLB。
/* mip/mie 位 */
#define MIP_MSIP  (1u << 3)   /* Machine software interrupt pending */
#define MIP_MTIP  (1u << 7)   /* Machine timer interrupt pending */
#define MIP_MEIP  (1u << 11)  /* Machine external interrupt pending */
#define MIP_STIE (1u << 5) // S mode timer interrupt enable

#define MIE_MSIE  (1u << 3)
#define MIE_MTIE (1UL << 7)
#define MIE_MEIE (1UL << 11)

/* 异常代码（同步异常） */
#define EXC_BREAKPOINT 3
#define EXC_ECALL_U    8
#define EXC_ECALL_S    9
#define EXC_ECALL_M    11

/* 中断编号（mcause 的低位） */
#define IRQ_M_SOFT  3
#define IRQ_M_TIMER 7
#define IRQ_M_EXT   11

#define IRQ_S_SOFT  3
#define IRQ_S_TIMER 7
#define IRQ_S_EXT 11

#define SATP_MODE (1 << 31)

#define MIDELEG_SSI    (1L << 1)   // 委托 Software Interrupt 给 S 模式
#define MIDELEG_MSI    (0L << 3)   // M 模式的 Software Interrupt（实际上不委托）
#define MIDELEG_STI    (1L << 5)   // 委托 Timer Interrupt 给 S 模式  
#define MIDELEG_MTI    (0L << 7)   // M 模式的 Timer Interrupt（实际上不委托）
#define MIDELEG_SEI    (1L << 9)   // 委托 External Interrupt 给 S 模式
#define MIDELEG_MEI    (0L << 11)  // M 模式的 External Interrupt（实际上不委托）

//TLB 
#define TLB_SIZE 64

// 内存顺序标记
typedef enum {
    MEM_ORDER_RELAXED = 0,  // 无顺序要求
    MEM_ORDER_ACQUIRE = 1,  // 获取语义
    MEM_ORDER_RELEASE = 2,  // 释放语义
    MEM_ORDER_ACQ_REL = 3,  // 获取+释放
} MemoryOrder;

#define FENCE_I (1 << 0)  // 输入（内存读）
#define FENCE_O (1 << 1)  // 输出（内存写）
#define FENCE_R (1 << 2)  // 读（同I）
#define FENCE_W (1 << 3)  // 写（同O）

#define COMPILER_BARRIER() asm volatile("" ::: "memory")


#define MEMORY_SIZE 0x40000000//(1024UL * 1024UL * 1024UL * 1) // 4GB
#define MEMORY_BASE 0x80000000         // 内存基地址

#define MEMORY_POOL_SIZE 0x40000000  // 例如 256MB 内存池
#define BLOCK_SIZE 0x1000            // 每块内存的大小，例如每块 4KB

#define PAGE_SIZE        4096u
#define PAGE_OFFSET_BITS 12u
#define VPN_BITS         10u     // per-level for Sv32
#define PTE_SIZE         4u      // bytes

#define SV32_LEVELS      2
#define SV39_LEVELS      3

// PTE flag bits (Sv32): V R W X U G A D (low bits)
#define PTE_V  (1u << 0)
#define PTE_R  (1u << 1)
#define PTE_W  (1u << 2)
#define PTE_X  (1u << 3)
#define PTE_U  (1u << 4)
#define PTE_G  (1u << 5)
#define PTE_A  (1u << 6)
#define PTE_D  (1u << 7)

// satp mode for RV32: 0 = BARE, 1 = SV32
#define SATP_MODE_MASK   (1u << 31)
#define SATP_PPN_MASK    ((1u << 22) - 1)//= 0x003fffff  // RV32: PPN is bits [21:0]


enum {  
    ACC_FETCH = 0, //取指
    ACC_LOAD = 1, //读
    ACC_STORE = 2 //写
};

enum {
    MMU_OK = 0,
    MMU_FAULT_PAGE,    // page-fault (permissions / invalid PTE / misaligned superpage / A/D)
    MMU_FAULT_ACCESS,  // access-fault (PMA/PMP) - emulator may treat same as page-fault
    MMU_FAULT_PASSTHRU // used internally
};

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


#define VIRTIO_IRQ       1    // VirtIO 设备

#define PLIC_PRIORITY     0x000000  // 优先级寄存器（每个 IRQ 4 字节）
#define PLIC_PENDING      0x001000  // Pending 位（每个 hart 一组）
#define PLIC_MENABLE      0x002000  // M 模式使能寄存器
#define PLIC_SENABLE      0x002080  // S 模式使能寄存器（注意：这个偏移可能不同！）
#define PLIC_MTHRESHOLD   0x200000  // M 模式阈值
#define PLIC_STHRESHOLD   0x201000  // S 模式阈值
#define PLIC_MCLAIM       0x200004  // M 模式 claim
#define PLIC_SCLAIM       0x201004  // S 模式 claim

// 每个 hart 的偏移量（对于多核系统）
#define PLIC_HART_OFFSET  0x2000    // 每个 hart 的寄存器空间

/* ---------- Configuration ---------- */
#define UART_TX_BUF_SIZE 1024
#define UART_RX_BUF_SIZE 1024

/* Register offsets (per device base) */
#define UART_REG_DATA       0x00
#define UART_REG_STATUS     0x04
#define UART_REG_CTRL       0x08
#define UART_REG_IRQ_STATUS 0x0C
#define UART_REG_CONFIG     0x10

/* Status bits */
#define UART_ST_TX_EMPTY    (1u << 0)
#define UART_ST_RX_READY    (1u << 1)

/* Ctrl bits */
#define UART_CTRL_RX_INT_EN (1u << 0)
#define UART_CTRL_TX_INT_EN (1u << 1)

/* IRQ status bits (pending) */
#define UART_IRQ_RX_PENDING (1u << 0)
#define UART_IRQ_TX_PENDING (1u << 1)

#define UART_BASE 0x10000000
#define UART_SIZE 0x100

#define UART_REG_RBR (UART_BASE + 0)  // 接收缓冲寄存器
#define UART_REG_THR (UART_BASE + 0)  // 发送保持寄存器  
#define UART_REG_IER (UART_BASE + 1)  // 中断使能寄存器
#define UART_REG_IIR (UART_BASE + 2)  // 中断标识寄存器
#define UART_REG_LCR (UART_BASE + 3)  // 线路控制寄存器
#define UART_REG_MCR (UART_BASE + 4)  // Modem 控制寄存器
#define UART_REG_LSR (UART_BASE + 5)  // 线路状态寄存器
#define UART_REG_MSR (UART_BASE + 6)  // Modem 状态寄存器
#define UART_REG_SCR (UART_BASE + 7)  // 暂存寄存器


#define IRQ_BASE 11 // 像 UART、VirtIO、GPIO 等外设，它们属于 外部中断，
                    //统一通过 Machine External Interrupt = 11 进 CPU，然后再通过 PLIC 判断具体是哪个设备
#define UART_IRQ_NUM 3 // uart0

#define LSR_DR    (1<<0)   // Data Ready
#define LSR_OE    (1<<1)   // Overrun Error
#define LSR_THRE  (1<<5)   // THR Empty
#define LSR_TEMT  (1<<6)   // Transmitter Empty

// LSR 位定义
#define LSR_RX_READY  0x01  // 数据就绪
#define LSR_TX_EMPTY  0x20  // 发送器空

// IER 位定义  
#define IER_RX_ENABLE  0x01  // 接收中断使能
#define IER_TX_ENABLE  0x02  // 发送中断使能

#define VIRTIO_MMIO_BASE    0x10001000ULL
#define VIRTIO_MMIO_SIZE    0x1000

// virtio-blk 请求类型
#define VIRTIO_BLK_T_IN     0   // 读
#define VIRTIO_BLK_T_OUT    1   // 写

// virtio 描述符标志位
#define VRING_DESC_F_NEXT    1   // 描述符链中还有下一个
#define VRING_DESC_F_WRITE   2   // 设备可写（用于数据方向）
#define VRING_DESC_F_INDIRECT 4  // 描述符指向间接描述符表
#define DISK_LATENCY_CYCLES 1000  // 模拟磁盘延迟：1000个CPU周期

#endif