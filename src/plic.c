#include "plic.h"

extern CPU_State cpu[MAX_CORES];

// 验证 IRQ 号是否有效
static int plic_is_valid_irq(int irq) {
    return (irq >= 1 && irq < MAX_IRQS);
}

// 验证 cpu ID 是否有效  
static int plic_is_valid_cpu(int cpu_id) {
    return (cpu_id >= 0 && cpu_id < MAX_CORES);
}

// 设置中断使能状态
void plic_set_enable(int cpu_id, int irq, int enabled) {
    if (!plic_is_valid_irq(irq) || !plic_is_valid_cpu(cpu_id)) return;
    
    int word_index = irq / 32;
    int bit_offset = irq % 32;
    
    if (enabled) {
        plic.enable[cpu_id][word_index] |= (1 << bit_offset);
    } else {
        plic.enable[cpu_id][word_index] &= ~(1 << bit_offset);
    }
    
    // 使能状态改变，需要更新中断线
    plic_update( cpu_id);
}

// 批量设置中断使能
void plic_set_enable_range( int cpu_id, int start_irq, int end_irq, int enabled) {
    for (int irq = start_irq; irq <= end_irq; irq++) {
        plic_set_enable( cpu_id, irq, enabled);
    }
}

// 获取使能寄存器值（用于内存读取）
uint32_t plic_get_enable(int cpu_id, int word_index) {
    if ( cpu_id < 0 || cpu_id >= MAX_CORES) return 0;
    if (word_index < 0 || word_index >= (MAX_IRQS / 32)) return 0;
    
    return plic.enable[cpu_id][word_index];
}

// 设置使能寄存器值（用于内存写入）
void plic_set_enable_word(int cpu_id, int word_index, uint32_t value) {
    if (cpu_id < 0 || cpu_id >= MAX_CORES) return;
    if (word_index < 0 || word_index >= (MAX_IRQS / 32)) return;
    
    plic.enable[cpu_id][word_index] = value;
    
    // 使能状态改变，需要更新中断线
    plic_update( cpu_id);
}



// 检查中断是否使能
int plic_is_enabled(int irq, int cpu_id) {
    
    if (irq < 1 || irq >= MAX_IRQS) return 0;
    if (cpu_id < 0 || cpu_id >= MAX_CORES) return 0;
    
    // 计算在使能寄存器数组中的位置
    int word_index = irq / 32;
    int bit_offset = irq % 32;
    
    // 检查使能位
    return (plic.enable[cpu_id][word_index] >> bit_offset) & 1;
}

void plic_set_irq(int irq, int level) {
    if (irq < 1 || irq >= MAX_IRQS) return;

    int pending_word = irq >> 5;
    int pending_bit = irq & 0x1F;
    
    if (level) {
        plic.pending[pending_word] |= (1 << pending_bit);
    } else {
        plic.pending[pending_word] &= ~(1 << pending_bit);
    }

    for (int cpu_id = 0; cpu_id < MAX_CORES; cpu_id++) {
        if (plic_is_enabled( irq, cpu_id)) {
            plic_update( cpu_id);
        }
    }
}


// 中断索赔 - CPU 读取此寄存器获取最高优先级中断
uint32_t plic_claim(int cpu_id) {
    if (cpu_id < 0 || cpu_id >= MAX_CORES) return 0;
    
    uint32_t max_priority = plic.threshold[cpu_id];
    uint32_t winning_irq = 0;
    
    for (int irq = 1; irq < MAX_IRQS; irq++) {

        int pending_word = irq >> 5;
        int pending_bit = irq & 0x1F;
        int claimed_word = irq >> 5;
        int claimed_bit = irq & 0x1F;

        if (!(plic.pending[pending_word] & (1 << pending_bit))) continue;

        if(plic.claimed[cpu_id][claimed_word] & (1 << claimed_bit)) continue;

        if(!plic_is_enabled(irq,cpu_id)) continue;

        if(plic.priority[irq] <= max_priority) continue;

        max_priority = plic.priority[irq];
        winning_irq = irq; 
    }
    
    if (winning_irq != 0) {
        int claimed_word = winning_irq >> 5;
        int claimed_bit = winning_irq & 0x1F;
        plic.claimed[cpu_id][claimed_word] |= (1 << claimed_bit);
        plic.pending[winning_irq >> 5] &= ~(1 << claimed_bit); // 保持这行如果选择简化
        plic_update(cpu_id);
    }
    
    return winning_irq;
}


// 中断完成 - CPU 写入此寄存器表示中断处理完成
// 设备调用此函数触发中断
void plic_complete(int cpu_id, uint32_t irq) {
    if ( cpu_id < 0 || cpu_id >= MAX_CORES) return;
    if (irq < 1 || irq >= MAX_IRQS) return;

    int claimed_bit = irq & 0x1F;
    
    plic.claimed[cpu_id][irq >> 5] &= ~(1 << claimed_bit);
    
    // 只更新该 cpu 的中断状态
    plic_update(cpu_id);
}

// 更新中断线状态
void plic_update(int cpu_id) {
    if ( cpu_id < 0 || cpu_id >= MAX_CORES) return;
    
    int has_pending = 0;

    uint32_t max_priority = plic.threshold[cpu_id]; 
    uint32_t max_irq = 0;
    
    // 检查该 cpu 是否有待处理的中断
    for (int irq = 1; irq < MAX_IRQS; irq++) {
        int word_index = irq >> 5; //  irq / 32
        int bit_offset = irq & 0x1F; // irq % 32

        if(!plic_is_enabled(irq,cpu_id)) continue;
        if( !(plic.pending[word_index] & (1 << bit_offset))) continue;

        //检查是否已被认领
        if(plic.claimed && (plic.claimed[cpu_id][irq >> 5] & (1 << (irq & 0x1F)))){
            continue;
        }

        if(plic.priority[irq] > max_priority){
            max_priority = plic.priority[irq];
            max_irq = irq;
        }

        if(max_irq > 0){
            cpu[cpu_id].mip |= MIP_MEIP;
            plic.current_irq[cpu_id] = max_irq;
        }else{
            cpu[cpu_id].mip &= ~MIP_MEIP;
            plic.current_irq[cpu_id] = 0;

        }
    }
}

uint64_t plic_read(void* opaque,uint64_t offset, int size) {
    RAMDevice *ram = (RAMDevice*) opaque;
   
    switch (offset) {
        // 优先级寄存器读取 (0x000000 - 0x000FFF) - 共享
        case 0x000000 ... 0x000FFF: {
            int irq_index = offset >> 2; // 第 irq_index 个中断源
            if (irq_index < MAX_IRQS) {
                return plic.priority[irq_index];
            }
            return 0;
        }

        case 0x1000 ... 0x107F:{
            int index = (offset - 0x1000) >> 2;
            return plic.pending[index];
        }
            
        // 中断使能寄存器读取 (每个 cpu 有自己的使能寄存器组)
        case 0x002000 ... 0x002FFF: {
            // 计算 cpu_id: 每个 cpu 占用 0x80 字节
            //cpu0 0x2000~0x207F , cpu1 0x2080~0x20FF ........
            int target_cpu = (offset - 0x2000) / 0x80;
            int word_index = ((offset - 0x2000) % 0x80) >> 2;
            
            if (target_cpu >= 0 && target_cpu < MAX_CORES && 
                word_index >= 0 && word_index < (MAX_IRQS / 32)) {
                return plic.enable[target_cpu][word_index];
            }
            return 0;
        }
            
        // 阈值寄存器读取 (每个 cpu 有自己的阈值寄存器)
        case 0x200000 ... 0x20FFFF: {
            // 每个 cpu 占用 0x1000 字节空间
            int target_cpu = (offset - 0x200000) / 0x1000;
            int reg_offset = (offset - 0x200000) % 0x1000;
            
            if (target_cpu >= 0 && target_cpu < MAX_CORES) {
                if (reg_offset == 0x000) {  // 阈值寄存器
                    return plic.threshold[target_cpu];
                } else if (reg_offset == 0x004) {  // 索赔寄存器
                    return plic_claim(target_cpu);
                }
            }
            return 0;
        }
            
        default:
            printf("PLIC: unknown read offset 0x%lx from cpu\n", offset);
            return 0;
    }
}

void plic_write(void* opaque,uint64_t offset, uint64_t value, int size) {
    switch (offset) {
        // 优先级寄存器写入 (0x000000 - 0x000FFF) - 共享
        case 0x000000 ... 0x000FFF: {
            int irq_index = offset >> 2;
            if (irq_index < MAX_IRQS) {
                plic.priority[irq_index] = value & 0x7;
            }
            break;
        }
            
        // 中断使能寄存器写入 (每个 cpu 有自己的使能寄存器组)
        case 0x002000 ... 0x002FFF: {
            // 计算 cpu_id: 每个 cpu 占用 0x80 字节
            int target_cpu = (offset - 0x2000) / 0x80;
            int word_index = ((offset - 0x2000) % 0x80) >> 2;
            
            if (target_cpu >= 0 && target_cpu < MAX_CORES && 
                word_index >= 0 && word_index < (MAX_IRQS / 32)) {
                plic.enable[target_cpu][word_index] = value;
                // 使能状态改变，需要更新中断线
                plic_update(target_cpu);
            }
            break;
        }
            
        // 阈值和完成寄存器写入 (每个 cpu 有自己的寄存器)
        case 0x200000 ... 0x20FFFF: {
            // 每个 cpu 占用 0x1000 字节空间
            int target_cpu = (offset - 0x200000) / 0x1000;
            int reg_offset = (offset - 0x200000) % 0x1000;
            
            if (target_cpu >= 0 && target_cpu < MAX_CORES) {
                if (reg_offset == 0x000) {  // 阈值寄存器
                    plic.threshold[target_cpu] = value & 0x7;
                    // 阈值改变，需要更新中断线
                    plic_update( target_cpu);
                } else if (reg_offset == 0x004) {  // 完成寄存器
                    plic_complete(target_cpu, value);
                }
            }
            break;
        }
            
        default:
            printf("PLIC: unknown write offset 0x%lx from cpu, value=0x%lx\n", 
                   offset, value);
    }
}

int plic_select_target_cpu_affinity(int irq) {
    // 这里可以实现更复杂的中断亲和性策略
    // 比如：轮询、固定分配、基于负载等
    
    // 简单实现：轮询选择
    static int next_cpu = 0;
    for (int i = 0; i < MAX_CORES; i++) {
        int cpu_id = (next_cpu + i) % MAX_CORES;
        if (plic_is_enabled(irq, cpu_id)) {
            next_cpu = (cpu_id + 1) % MAX_CORES;
            return cpu_id;
        }
    }
    return -1;
}

void plic_set_irq_affinity(int irq, int target_cpu) {
    // 参数验证
    if (irq < 1 || irq >= MAX_IRQS) {
        return;
    }
    if (target_cpu < 0 || target_cpu >= MAX_CORES) {
        return;
    }
    
    // 配置亲和性：只对目标 CPU 使能，其他 CPU 禁用
    for (int cpu_id = 0; cpu_id < MAX_CORES; cpu_id++) {
        if (cpu_id == target_cpu) {
            plic_set_enable(irq, cpu_id,1);
        } else {
            plic_set_enable(irq, cpu_id,0);
        }
    }
}

void setup_critical_irq_affinity(void) {
    // 系统关键中断 - 固定CPU分配
    plic_set_irq_affinity(UART_IRQ, 0);           // 控制台响应要求低延迟
    plic_set_irq_affinity(TIMER_IRQ, 0);          // 系统定时器
    
    // 网络中断 - 负载均衡
    plic_set_irq_affinity(NET_RX_IRQ, 1);         
    plic_set_irq_affinity(NET_TX_IRQ, 2);         
    
    // 存储中断 - 集中处理
    plic_set_irq_affinity(DISK_IRQ, 3);           
    plic_set_irq_affinity(USB_IRQ, 3);            
    
    // 其他重要设备...
    plic_set_irq_affinity(PCI_IRQ, 2);
    plic_set_irq_affinity(AUDIO_IRQ, 1);

    plic_set_irq_affinity(VIRTIO_IRQ, 0);
}

int plic_is_irq_enabled_on_any(int irq) {
    for (int cpu_id = 0; cpu_id < MAX_CORES; cpu_id++) {
        if (plic_is_enabled(irq, cpu_id)) {
            return 1;
        }
    }
    return 0;
}

void setup_generic_irq_affinity(void) {
    // 为其他中断设置默认亲和性
    for (int irq = 1; irq < MAX_IRQS; irq++) {
        if(!plic_is_irq_enabled_on_any(irq)){
            int target_cpu = plic_select_target_cpu_affinity(irq);
            if (target_cpu >= 0) {
                plic_set_irq_affinity(irq, target_cpu);
            }
        }
    }
}

void setup_irq_affinity() {

    // 阶段1：关键设备固定分配
    setup_critical_irq_affinity();
    
    // 阶段2：其他设备使用默认策略
    setup_generic_irq_affinity();
    
}

void plic_init_enables() {
    
    // 为每个 CPU 核心使能常见的中断
    for (int cpu_id = 0; cpu_id < MAX_CORES; cpu_id++) {
        // 使能 UART 中断（通常 IRQ 10）
        plic_set_enable(cpu_id, UART_IRQ, 1);
        
        // 使能 VirtIO 磁盘中断（通常 IRQ 1）
        plic_set_enable(cpu_id, VIRTIO_IRQ, 1);
        
        // 使能其他设备中断...
    }
}

void plic_init(void) {
   // PLICState *plic = malloc(sizeof(PLICState));
    memset(&plic, 0, sizeof(PLICState));
    
    // 设置默认优先级
    for (int i = 1; i < MAX_IRQS; i++) {
        plic.priority[i] = 1;  // 默认优先级为1
    }
    
    // 设置默认阈值
    for (int i = 0; i < MAX_CORES; i++) {
        plic.threshold[i] = 0;  // 阈值为0，允许所有优先级>0的中断
    }
    
    // 初始化使能寄存器
    plic_init_enables();
    setup_irq_affinity();
}