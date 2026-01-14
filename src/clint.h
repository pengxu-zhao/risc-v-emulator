#ifndef CLINT_H
#define CLINT_H
#include "common.h"

typedef struct {
    // 核心寄存器
    uint64_t mtime;                     // 计时器值（实时递增）
    uint64_t mtimecmp;                  // 比较寄存器
    uint32_t msip;                      // 软件中断待处理寄存器
    
    // 回调函数指针（用于通知CPU中断）
    void (*timer_interrupt_callback)(void);    // 时钟中断回调
    void (*software_interrupt_callback)(void); // 软件中断回调
    
    // 中断状态
    bool timer_interrupt_pending;
    bool software_interrupt_pending;
} CLINT;

void clint_init(CLINT* clint);
void clint_reset(CLINT* clint);

uint64_t clint_read(CLINT* clint, uint64_t addr, uint32_t size);
void clint_write(CLINT* clint, uint64_t addr, uint64_t value, uint32_t size);

void clint_tick(CLINT* clint, uint64_t cycles);

void clint_update_interrupts(CLINT* clint);

bool clint_get_timer_interrupt(CLINT* clint);
bool clint_get_software_interrupt(CLINT* clint);



#endif