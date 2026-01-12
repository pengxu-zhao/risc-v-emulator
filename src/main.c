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
extern virtio_blk_device dev;
Bus bus;
CPU_State cpu[MAX_CORES];
int log_enable = 0;

 int j = 0;

struct memory_pool pool; 

static uint64_t get_real_time_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
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
    printf("=====init driveraddr:0x%16lx\n",dev.driver_addr);

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
       
        static uint64_t old = 0;
        if(cpu[i].halted != true){
                    //
                    //415173710  sleep
                    //422282255   virtio 0x50
                    //
            for( ;j <= 415005914; j++) {
                cpu_step(&cpu[i],memory);

                cpu[i].cycle_count++;
                if(log_enable){
                   // printf("j:%ld\n",j);
                }
                cpu->csr[CSR_TIME] += 10;
                int n = 0;

                if(cpu[i].pc == 0x800023ee){
                   // printf("j:%ld\n",j);
                  //  break;
                }

                if(cpu[0].csr[CSR_TIME] >= 4150059100){
                   log_enable = 1;
                }

                virtio_disk_update(&cpu[i].cycle_count);

                check_and_handle_interrupts(&cpu[i]);

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