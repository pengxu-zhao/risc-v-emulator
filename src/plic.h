#ifndef PLIC_H
#define PLIC_H

#include "common.h"


typedef struct {
    // 优先级寄存器 (每个中断源1个，4字节对齐)
    uint32_t priority[1024];
    
    // 中断使能寄存器 (每个hart 1组)
    uint32_t enable[MAX_CORES][1024 / 32];  // 按位使能
    
    // 每个hart的阈值和索赔/完成寄存器
    uint32_t threshold[MAX_CORES];
    uint32_t claim_complete;

    //每个 CPU 是否有中断请求
    bool irq_pending[MAX_CORES];
    
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