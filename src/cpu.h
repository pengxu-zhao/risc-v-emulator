// src/cpu.h
#ifndef CPU_H
#define CPU_H

#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdarg.h>
#include "bus.h"
#include "memory.h"

#define NUM_GPR 32
#define NUM_FGPR 32
#define CSR_COUNT 4096
#define MAX_CORES 4

// 重要的CSR地址定义

#define CSR_SIE      0x104 //supervisor 模式下管理中断使能 与 SIP 寄存器共享同一个地址
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
/*
mstatus：全局中断开关 + 模式保存

mie：允许哪些中断

mip：哪些中断挂起

mtvec：trap 入口地址

mepc：trap 前 PC

mcause：trap 原因

mtval：trap 附加信息
*/


/* mstatus 位/字段（常用） */
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

#define SATP_MODE (1 << 31)

//TLB 
#define TLB_SIZE 64

typedef struct {
    uint64_t tag;          // 虚拟页号 + ASID
    uint64_t ppn;          // 物理页号
    uint8_t  flags;  // R/W/X/U等权限位
    bool     valid;        // 条目是否有效
    bool     global;       // 是否是全局页（忽略ASID）
    uint8_t  asid;         // 地址空间ID（0-255）
    uint64_t last_used;    // LRU计数器
    
} TLBEntry;

typedef struct {
    TLBEntry entries[TLB_SIZE];
    int next_replace; // 下一个要替换的位置（FIFO）
   
} TLB;

typedef struct {
    TLB iTLB;  // 指令 TLB
    TLB dTLB;  // 数据 TLB
} CPU_TLB;

// 内存顺序标记
typedef enum {
    MEM_ORDER_RELAXED = 0,  // 无顺序要求
    MEM_ORDER_ACQUIRE = 1,  // 获取语义
    MEM_ORDER_RELEASE = 2,  // 释放语义
    MEM_ORDER_ACQ_REL = 3,  // 获取+释放
} MemoryOrder;

// CPU
typedef struct {
    // 程序计数器
    uint64_t pc;
    
    // 32个通用寄存器 (x0-x31)
    uint64_t gpr[NUM_GPR];

    uint64_t fgpr[NUM_FGPR];
    
    // CSR寄存器组
    uint64_t csr[CSR_COUNT];


    //irq   记录哪个 IRQ 正在 pending
    int irq_pending[32] ;

    int privilege; /* 0=U,1=S,3=M */
    int hartid;

    uint64_t mstatus;
    uint64_t mcause;
    uint64_t mepc;
    uint64_t mtvec;
    uint64_t mip;
    uint64_t mie;

    /* CLINT: 64-bit timer */
    uint64_t mtime;
    uint64_t mtimecmp;
    
    // 运行状态标志
    bool running;
    
    // 性能计数器（可选）
    uint64_t cycle_count;
    uint64_t inst_count;

    uint8_t *mem;
    uint64_t mem_size;
    uint64_t satp;    // raw satp CSR for RV32
    uint16_t asid;
    // simplified status bits (extend later)
    int mxr; // 允许 执行权限的页也可以被读取 (from sstatus/mstatus)
    int sum; // s-mode  can access U=1 ,can't excute U=1 page, in sstatus register

    TLB tlb;
    CPU_TLB cpu_tlb;
    uint64_t tlb_cnt;

    uint64_t sepc; // 异常 PC
    uint64_t scause; // 异常原因
    uint64_t stval;  // 错误地址
    uint64_t stvec;  // 异常向量
    uint64_t next_free_ppn; // 下一个可用物理页号
    uint8_t *uart_table[32];
    Bus bus;
    bool halted;

    // 原子操作状态
    struct {
        bool in_atomic;           // 是否在原子操作中
        MemoryOrder last_order;   // 最后一个原子操作的内存顺序
        uint64_t last_atomic_pc;  // 最后一个原子操作的PC
    } atomic_state;
    
    int use_relaxed_memory;  // 是否使用宽松内存模型


} CPU_State;


#define FENCE_I (1 << 0)  // 输入（内存读）
#define FENCE_O (1 << 1)  // 输出（内存写）
#define FENCE_R (1 << 2)  // 读（同I）
#define FENCE_W (1 << 3)  // 写（同O）

#define COMPILER_BARRIER() asm volatile("" ::: "memory")
// 函数声明
void cpu_init(CPU_State* cpu, uint8_t core_id);
void cpu_step(CPU_State* cpu, uint8_t* memory);
void cpu_run(CPU_State* cpu, uint8_t* memory);
void cpu_dump_registers(CPU_State* cpu);

static inline uint64_t read_csr(CPU_State *cpu, unsigned id){ return cpu->csr[id & 0xfff]; }
static inline void write_csr(CPU_State *cpu, unsigned id, uint64_t v){ cpu->csr[id & 0xfff] = v; }


#endif