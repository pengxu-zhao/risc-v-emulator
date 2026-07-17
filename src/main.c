// src/main.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "cpu.h"
#include "memory.h"
#include "elf_load.h"

#include "instructions.h"
#include "uart.h"
#include "bus.h"
#include "plic.h"
#include "decode.h"
#include "virtio_blk.h"
#include "clint.h"
#include "trap.h"
#include "bootloader.h"
#include "cache.h"
#include "mmu.h"
#include "imsic.h"
// x1: returen address
// x2: stack pointer
// x3: global pointer
// x4: thread pointer
// x5~x7: temp register
// x8: save register/frame pointer  s0
// x9: save register  s1
// x10~x11: function argument / return value
// x12~x17: function argument
// x18~x27: save register20  x18-s2
// x28~x31: temp register


extern uint8_t* memory;
extern virtio_blk_device dev;
Bus bus;
extern CPU_State cpu[MAX_CORES];
extern PLICState plic;
int log_enable = 0;
int j,m = 0;

extern imsic_t g_imsic;

extern cache_t *L1,*L2,*L3;

static uint64_t get_real_time_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

int main(int argc, char *argv[]) {
    setbuf(stdout, NULL);
    printf("Initializing RISC-V emulator...\n");
    printf("Memory size: %ld GB, Base address: 0x%08x\n", 
           MEMORY_SIZE / (1024 * 1024 * 1024), MEMORY_BASE);
    
    init_memory();
    init_cache();
    printf("cache_t:line_size:%d sets:%d ways:%d\n", L1->line_size, L1->sets, L1->ways);
    printf("cache_t:line_size:%d sets:%d ways:%d\n", L2->line_size, L2->sets, L2->ways);
    printf("cache_t:line_size:%d sets:%d ways:%d\n", L3->line_size, L3->sets, L3->ways);
    // 初始化CPU
    printf("Initializing CPU...\n");
    imsic_init(&g_imsic, MAX_CORES, set_ext_irq);
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
    bus.cache_enabled = 0; // 启用总线缓存
 //load xv6
 /*
    uint64_t entry_addr;
    if(load_elf64_SBI("kernel",&entry_addr) < 0){
        printf("load openSBI error\n");
    }else{
        printf("entry addr:0x%08lx\n",entry_addr);
    } */

    
    load_bin("fw_jump.bin", SBI_LOAD_ADDR);
    load_bin("Image", IMAGE_LOAD_ADDR);
    load_dtb("v1.dtb",DTB_LOAD_ADDR);
    
    for (int i = 0; i < 16; i++) {
        printf("%02x ", bus_read(&bus, 0x87000154 + i, 1));
    }
    printf("\n");
               /*
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <test.elf>\n", argv[0]);
        return 1;
    }

    const char *elf_path = argv[1];
    uint64_t entry_addr;
    load_elf64_SBI(elf_path, &entry_addr);

    */
    //virtio_blk_init("fs.img");
    //printf("=====init driveraddr:0x%16lx\n",dev.avail_ring);
    
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

    bus_register_mmio(&bus,CLINT_BASE_ADDR,
                         CLINT_SIZE,         
                        clint_read,
                        clint_write,
                        &cpu->clint);

     bus_register_mmio(&bus,hart0_M, 
                            IMSIC_FILE_SIZE,         
                        imsic_read,
                        imsic_write,
                        &g_imsic);
    
    bus_register_mmio(&bus,hart0_S, 
                            IMSIC_FILE_SIZE,         
                        imsic_read,
                        imsic_write,
                        &g_imsic);

    bus_register_mmio(&bus,hart1_M, 
                        IMSIC_FILE_SIZE,         
                        imsic_read,
                        imsic_write,
                        &g_imsic);

    bus_register_mmio(&bus,hart1_S, 
                        IMSIC_FILE_SIZE,         
                        imsic_read,
                        imsic_write,
                        &g_imsic);

        
    for(int i = 0; i < 2; i++){
        cpu_init(&cpu[i],i);
        tlb_flush(&cpu);
        printf("Starting RISC-V emulator...cpu privilege: %d\n",cpu[i].privilege);
        printf("pc[%d]:0x%08lx\n",i,cpu[i].pc);
    }
    //275165  generic_domain_init
   while(j < 439001){
  
        j++;
      
        if(j == 439000) log_enable = 1;
    
        
    for(int i = 0; i < 2;i++){

        //415421550
        // 415283130  first to 800053ce

        // 415283320 pc = 0x00000039 ??

        //j:415283317 pc:0x800018a6   write 0x39 to x[1] ,tomorrow check it
        //415283285, addr not in any memory region.
        //执行kexec的时候才会建立PTE_U的页表，kexec在 forkret函数内，
        //现在刚进入forkret函数 就因为PTE_U为0 而导致翻译出错了，
        // 目前怀疑进入forkret之前的执行顺序不对，继续往前进行排查
        //415264564   pc = 800053a6, mepc:0x80000e8e,sepc:0x80001dbe
        // 硬件在处理中断跳转到stvec前 特权级自动切换到对应的级别。  
        //发现问题所在是 sret指令的实现，应该是讲特权级切换到 status.xpp保存的特权级，而不是置为0
        
        //429899252 ready to call wait()
        //431817210  0x74
        //for( ;j <= 431961669; j++)
        // 
        // 37974731 __list_add_valid 37974711
        //43068080   riscv_intc_init
        //43879594    sie.stie = 1
        //44701841   kernel_init
        // 391373372
    
        for( int k = 0;k < 10; k++){
            //0x264d02 156138704
            //198456480 bus_write halted
            //198456786 
            
            //201395390   kernel_init ret to ret_from_exception
            //201424982
            uint32_t stop_addr = 0xFFFFFFFF;

            if(argc > 1){
                stop_addr = strtoul(argv[1], NULL, 16);
            }
            uint64_t base = 0;
         
            //base = 0xffffffe000000000;
            
            base = 0x80000000;
            
            
            if(stop_addr == (cpu[0].pc - base) && j > 68501){
                printf("stop at pc:0x%08lx,j:%ld,pri:%d\n",stop_addr + base,j,cpu[0].privilege);
                j = 1000000000;
                break;
            } 
       
            if(cpu[i].running == false){
                break;
            }

            pthread_mutex_lock(&cpu[i].lock);

            while (cpu[i].halted) {
                pthread_cond_wait(&cpu[i].cond, &cpu[i].lock);
            }

            pthread_mutex_unlock(&cpu[i].lock);
                
            cpu_step(&cpu[i],memory);
            
            if(cpu[i].gpr[0] != 0){
                printf("j:%d pc:0x%08lx\n",j,cpu[i].pc);
                printf("x[0]:0x%lx\n",cpu[i].gpr[0]);
                cpu[i].halted = true;
            }
            virtio_disk_update(&cpu[i].cycle_count);
        
            check_and_handle_interrupts(&cpu[i]);
         
        }
        
     }
        if(log_enable){
            printf("\nFinal CPU state:\n");
            //cpu_dump_registers(&cpu[i]);
            
            printf("Cleaning up...\n");
            
            //free(memory);
            printf("Emulator finished j:%ld,pc:0x%08lx\n",j,cpu[0].pc);


            printf("sstatus:0x%08lx\n",cpu[0].csr[CSR_SSTATUS]);
            printf("sip:0x%08lx\n",cpu[0].csr[CSR_SIP]);


            uint64_t pa3 = 0x80001000;
            uint64_t val3 = bus_read(&bus, pa3, 8);
            printf("0x%08lx: 0x%016lx\n", pa3, val3);
            if(val3 == 0x1){
                printf("TEST PASS\n");
            }
        }
            
    }
    

    return 0;
}