// src/cpu.h
#ifndef CPU_H
#define CPU_H

// cpu.h


#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdarg.h>
#include "common.h"
#include "clint.h"
#include "bus.h"



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

    CLINT clint;

} CPU_State;

// 函数声明
void cpu_init(CPU_State* cpu, uint8_t core_id);
void cpu_step(CPU_State* cpu, uint8_t* memory);
void cpu_run(CPU_State* cpu, uint8_t* memory);
void cpu_dump_registers(CPU_State* cpu);

static inline uint64_t read_csr(CPU_State *cpu, unsigned id){ return cpu->csr[id & 0xfff]; }
static inline void write_csr(CPU_State *cpu, unsigned id, uint64_t v){ cpu->csr[id & 0xfff] = v; }
uint64_t get_cpu_cycle(CPU_State *cpu);

#endif