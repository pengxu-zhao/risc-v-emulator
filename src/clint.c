#include "clint.h"

void clint_init(CLINT* clint) {
    if (!clint) return;
    
    memset(clint, 0, sizeof(CLINT));
    clint->mtime = 0;
    clint->mtimecmp = 0;
    clint->msip = 0;
    clint->timer_interrupt_callback = NULL;
    clint->software_interrupt_callback = NULL;
    clint->timer_interrupt_pending = false;
    clint->software_interrupt_pending = false;
}

void clint_reset(CLINT* clint) {
    if (!clint) return;
    
    clint->mtime = 0;
    clint->mtimecmp = 0;
    clint->msip = 0;
    clint->timer_interrupt_pending = false;
    clint->software_interrupt_pending = false;
}

uint64_t clint_read(CLINT* clint, uint64_t addr, uint32_t size) {
    if (!clint) return 0;
    
    uint64_t offset = addr - CLINT_BASE_ADDR;
    
    // MSIP 寄存器（32位）
    if (offset == MSIP_OFFSET) {
        if (size == 4) {
            // MSIP 寄存器只有 bit0 有效
            return clint->msip & 0x1;
        }
        printf("[CLINT] Warning: MSIP read with size %u\n", size);
        return 0;
    }
    
    // MTIMECMP 寄存器（64位）
    if (offset == MTIMECMP_OFFSET) {
        if (size == 8) {
            return clint->mtimecmp;
        } else if (size == 4) {
            // 读取低32位
            return clint->mtimecmp & 0xFFFFFFFF;
        }
        printf("[CLINT] Warning: MTIMECMP read with size %u\n", size);
        return 0;
    }
    
    // MTIME 寄存器（64位）
    if (offset == MTIME_OFFSET) {
        if (size == 8) {
            return clint->mtime;
        } else if (size == 4) {
            // 读取低32位
            return clint->mtime & 0xFFFFFFFF;
        }
        printf("[CLINT] Warning: MTIME read with size %u\n", size);
        return 0;
    }
    
    // 如果偏移量有效但不是对齐的地址，可能是在访问64位寄存器的高32位
    if (offset == MTIMECMP_OFFSET + 4 && size == 4) {
        return (clint->mtimecmp >> 32) & 0xFFFFFFFF;
    }
    
    if (offset == MTIME_OFFSET + 4 && size == 4) {
        return (clint->mtime >> 32) & 0xFFFFFFFF;
    }
    
    printf("[CLINT] Read from unmapped address: 0x%016lx (offset: 0x%lx, size: %u)\n", 
           addr, offset, size);
    return 0;
}

void clint_write(CLINT* clint, uint64_t addr, uint64_t value, uint32_t size) {
    if (!clint) return;
    
    uint64_t offset = addr - CLINT_BASE_ADDR;
    
    // MSIP 寄存器
    if (offset == MSIP_OFFSET) {
        if (size == 4) {
            // MSIP 只有 bit0 有效，其他位保留
            clint->msip = value & 0x1;
            clint_update_interrupts(clint);
            // printf("[CLINT] MSIP set to %u\n", clint->msip);
        } else {
            printf("[CLINT] Warning: MSIP write with size %u\n", size);
        }
        return;
    }
    
    // MTIMECMP 寄存器
    if (offset == MTIMECMP_OFFSET) {
        if (size == 8) {
            clint->mtimecmp = value;
        } else if (size == 4) {
            clint->mtimecmp = (clint->mtimecmp & 0xFFFFFFFF00000000ULL) | (value & 0xFFFFFFFF);
        }
        clint_update_interrupts(clint);
        // printf("[CLINT] MTIMECMP set to 0x%016lx\n", clint->mtimecmp);
        return;
    }
    
    // MTIME 寄存器
    if (offset == MTIME_OFFSET) {
        if (size == 8) {
            clint->mtime = value;
        } else if (size == 4) {
            // 写入低32位
            clint->mtime = (clint->mtime & 0xFFFFFFFF00000000ULL) | (value & 0xFFFFFFFF);
        }
        clint_update_interrupts(clint);
        // printf("[CLINT] MTIME set to 0x%016lx\n", clint->mtime);
        return;
    }
    
    // 64位寄存器的高32位部分
    if (offset == MTIMECMP_OFFSET + 4 && size == 4) {
        clint->mtimecmp = (clint->mtimecmp & 0xFFFFFFFFULL) | ((value & 0xFFFFFFFF) << 32);
        clint_update_interrupts(clint);
        return;
    }
    
    if (offset == MTIME_OFFSET + 4 && size == 4) {
        clint->mtime = (clint->mtime & 0xFFFFFFFFULL) | ((value & 0xFFFFFFFF) << 32);
        clint_update_interrupts(clint);
        return;
    }
}

void clint_update_interrupts(CLINT* clint) {
    if (!clint) return;
    
    // 检查时钟中断：mtime >= mtimecmp
    bool new_timer_interrupt = (clint->mtime >= clint->mtimecmp);
    bool new_software_interrupt = (clint->msip != 0);
    
    bool timer_changed = (new_timer_interrupt != clint->timer_interrupt_pending);
    bool software_changed = (new_software_interrupt != clint->software_interrupt_pending);
    
    clint->timer_interrupt_pending = new_timer_interrupt;
    clint->software_interrupt_pending = new_software_interrupt;
    
    // 调用回调函数通知中断状态变化
    if (timer_changed && clint->timer_interrupt_callback) {
        clint->timer_interrupt_callback();
    }
    
    if (software_changed && clint->software_interrupt_callback) {
        clint->software_interrupt_callback();
    }
}

// 时钟前进（模拟时间流逝）
void clint_tick(CLINT* clint, uint64_t cycles) {
    if (!clint || cycles == 0) return;
    
    clint->mtime += cycles;
    
    // 检查是否触发中断
    clint_update_interrupts(clint);
}

bool clint_get_timer_interrupt(CLINT* clint) {
    return clint ? clint->timer_interrupt_pending : false;
}

bool clint_get_software_interrupt(CLINT* clint) {
    return clint ? clint->software_interrupt_pending : false;
}