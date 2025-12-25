// src/main.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "cpu.h"
#include "memory.h"
#include "elf_load.h"
#include "tick.h"
#include "instructions.h"
#include "uart.h"
#include "bus.h"
#include "plic.h"
#include "decode.h"
#include "virtio_blk.h"

// x1: returen address
// x2: stack pointer
// x3: global pointer
// x4: thread pointer
// x5~x7: temp register
// x8: save register/frame pointer
// x9: save register
// x10~x11: function argument / return value
// x12~x17: function argument
// x18~x27: save register20
// x28~x31: temp register


extern uint8_t* memory;
Bus bus;
CPU_State cpu[MAX_CORES];
int log_enable = 0;

struct memory_pool pool; 

static uint64_t get_real_time_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

void dump_sv39_walk(CPU_State* cpu, uint64_t va) {
    uint64_t satp = cpu->csr[CSR_SATP];
    printf("\n========== SV39 WALK: VA = 0x%016lx ==========\n", va);
    printf("SATP = 0x%016lx (mode=%d, asid=%d, ppn=0x%lx)\n",
        satp,
        (int)(satp >> 60),
        (int)((satp >> 44) & 0xFFFF),
        (uint64_t)(satp & ((1UL<<44)-1)));

    if ((satp >> 60) != 8) {
        printf("MMU disabled (Bare mode)\n");
        return;
    }

    uint64_t ppn = satp & ((1UL<<44)-1);
    uint64_t table = ppn << 12;

    for (int level = 2; level >= 0; level--) {

        uint64_t vpn_i = (va >> (12 + level*9)) & 0x1FF;
        uint64_t pte_addr = table + vpn_i * 8;

        uint64_t pte = phys_read_u64(cpu, pte_addr);

        printf("\n-- Level %d --\n", level);
        printf("VPN[%d] = %ld\n", level, vpn_i);
        printf("PTE addr = 0x%016lx\n", pte_addr);
        printf("PTE      = 0x%016lx\n", pte);

        if ((pte & 1) == 0) {
            printf("❌ INVALID: PTE.V = 0\n");
            return;
        }

        uint64_t flags = pte & 0x3FF;

        printf("Flags: V=%d R=%d W=%d X=%d U=%d G=%d A=%d D=%d\n",
            !!(flags & 1),
            !!(flags & 2),
            !!(flags & 4),
            !!(flags & 8),
            !!(flags & 16),
            !!(flags & 32),
            !!(flags & 64),
            !!(flags & 128)
        );

        int leaf = (flags & (2|4|8)) != 0;
        if (leaf) {
            printf("🌿 Leaf PTE at level %d\n", level);

            uint64_t pa;

            if (level == 2) { // 1GB page
                uint64_t ppn2 = (pte >> 28) & 0x3FFFFFF;
                pa = (ppn2 << 30) | (va & 0x3FFFFFF);
            } else if (level == 1) { // 2MB page
                uint64_t ppn21 = (pte >> 19) & 0x7FFFFFFFFULL;
                pa = (ppn21 << 21) | (va & 0x1FFFFF);
            } else { // level 0 normal page
                uint64_t ppn012 = (pte >> 10) & ((1UL<<44)-1);
                pa = (ppn012 << 12) | (va & 0xFFF);
            }

            printf("PA = 0x%016lx\n", pa);
            printf("========== END WALK ==========\n");
            return;
        }

        /* Non-leaf → go down */
        uint64_t next_ppn = (pte >> 10) & ((1UL<<44)-1);
        table = next_ppn << 12;
    }

    printf("❌ ERROR: Walk finished without leaf!\n");
}


int main() {
    setbuf(stdout, NULL);
    printf("Initializing RISC-V emulator...\n");
    printf("Memory size: %ld GB, Base address: 0x%08x\n", 
           MEMORY_SIZE / (1024 * 1024 * 1024), MEMORY_BASE);
    
    init_memory();
    
    // 初始化CPU
    printf("Initializing CPU...\n");
    memory_pool_init(&pool); 
    RAMDevice ram;
    ram.data = memory;
    ram.size = MEMORY_SIZE;
    
    // 初始化挂起操作缓冲区
    ram.pending.load_capacity = 16;
    ram.pending.load_addrs = malloc(ram.pending.load_capacity * sizeof(uint64_t));
    ram.pending.load_count = 0;
    
    ram.pending.store_capacity = 16;
    ram.pending.store_ops = malloc(ram.pending.store_capacity * 
                                   sizeof(*ram.pending.store_ops));
    ram.pending.store_count = 0;




    kalloc_sim(&ram);

    
    
    
    uint64_t entry_addr;
    if(load_elf64_SBI("kernel",&entry_addr) < 0){
        printf("load openSBI error\n");
    }else{
        printf("entry addr:0x%08lx\n",entry_addr);
    }

    virtio_blk_init("fs.img");

    bus_register_mmio(&bus, 
                    MEMORY_BASE, MEMORY_SIZE, 
                    ram_read, 
                    ram_write, 
                    &ram);

    UARTDevice *uart = uart_create(UART_BASE, &cpu, UART_IRQ_NUM);
    
    printf("TX thread tid=%ld\n", uart->tx_thread);
    cpu->uart_table[UART_IRQ_NUM] = uart;
    
    bus_register_mmio(&bus,
                  UART_BASE, UART_SIZE,
                  mmio_read,
                  mmio_write,
                  uart);

    plic_init();
    bus_register_mmio(&bus,
                    PLIC_BASE,PLIC_SIZE,
                    plic_read,
                    plic_write,
                    &plic);          
                    
    bus_register_mmio(&bus,VIRTIO_MMIO_BASE,
                    VIRTIO_MMIO_SIZE,
                    virtio_mmio_read,
                    virtio_mmio_write,
                    &dev);
    

    for(int i = 0; i< 1;i++){
        cpu[i].bus = bus;
        printf("pc[%d]:0x%08lx\n",i,cpu[i].pc);
        cpu_init(&cpu[i],i);
        tlb_flush(&cpu);
        

        // 运行模拟器
        printf("Starting RISC-V emulator...cpu privilege: %d\n",cpu[i].privilege);
        printf("Only cpu 0\n");
        // load elf file
        //load_elf32_bare("../../../mini-rtos/rtos",memory,MEMORY_BASE,MEMORY_BASE,&cpu);
        //load_elf32_bare("../../mini-rtos/rtos",memory,MEMORY_SIZE,MEMORY_BASE,&cpu);
        cpu[i].pc = entry_addr;
        int j = 0;
        if(cpu[i].halted != true){
        
                        //
                        //413339546
                            
            for( ;j <= 413363454; j++) {
                cpu_step(&cpu[i],memory);

                cpu->csr[CSR_TIME] += 10;
                int n = 0;
                
                if(cpu[0].csr[CSR_TIME] >= 4133628700){
                    log_enable = 1;
                }

                if(cpu[0].pc == 0x80000ed8){
                    //printf("j:%ld , time:%ld\n",j,cpu[0].csr[CSR_TIME]);
                    //cpu[0].halted = true;
                }

                if ((cpu[i].mip & cpu[i].mie) && (cpu[i].mstatus & MSTATUS_MIE)){
                    //trap handle
                    uint64_t cause = 0;
                    bool is_interrupt = true;

                    // 检查具体的中断源（按优先级）
                    if (cpu[i].mip & MIP_MSIP && cpu[i].mie & MIE_MSIE) {
                        cause = IRQ_M_SOFT;
                        cpu[i].mip &= ~MIP_MSIP;  // 软件中断可以立即清除
                    } else if (cpu[i].mip & MIP_MTIP && cpu[i].mie & MIE_MTIE) {
                        cause = IRQ_M_TIMER;
                        cpu[i].mip &= ~MIP_MTIP;  // 定时器中断可以立即清除
                    } else if (cpu[i].mip & MIP_MEIP && cpu[i].mie & MIE_MEIE) {
                        cause = IRQ_M_EXT;
                        // 重要：不要在这里清除 MEIP！
                        // MEIP 由 PLIC 在中断处理完成后清除
                    } else {
                        continue;
                    }
                    take_trap(&cpu[i], cause, is_interrupt);
                  
                }
            }
        }else{
            cpu[i].running = false;
        }

        
        printf("\nFinal CPU state:\n");
        //cpu_dump_registers(&cpu[i]);
        
        printf("Cleaning up...\n");
        
        //free(memory);
        printf("Emulator finished j:%ld\n",j);

        uint32_t val1 = 0;
        uint32_t val2 = 0;

        val1 = bus_read(&bus,0x8000f880,8);
        val2 = bus_read(&bus,0x87fff000,8);
        printf("[0x8000f880] :0x%lx\n",val1);
        printf("[0x87fff000] :0x%lx\n",val2);    
    }
    
    // 设置自旋锁地址为 1
    /*
    uint64_t spinlock_addr = 0x80040008;
    uint64_t ram_base = 0x80000000;  // 注意：OpenSBI 可能期望 RAM 从 0x80000000 开始
    
    uint64_t spinlock_addr2 = 0x80043230;

    if (spinlock_addr >= ram_base && spinlock_addr < ram_base + ram.size) {
        uint64_t offset = spinlock_addr - ram_base;
        uint64_t value = 1;
        memcpy(ram.data + offset, &value, 8);

        uint64_t offset2 = spinlock_addr2 - ram_base;
        memcpy(ram.data + offset2, &value, 8);

        printf("OpenSBI 自旋锁已修复\n");
    } else {
        printf("错误：自旋锁地址不在 RAM 中\n");
    } */ 

   // init_tasks();

    return 0;
}