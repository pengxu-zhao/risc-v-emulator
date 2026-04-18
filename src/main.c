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
extern CPU_State cpu[MAX_CORES];
int log_enable = 0;

 int j = 0;



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

    uint64_t entry_addr;
    if(load_elf64_SBI("kernel",&entry_addr) < 0){
        printf("load openSBI error\n");
    }else{
        printf("entry addr:0x%08lx\n",entry_addr);
    }

    virtio_blk_init("fs.img");
    printf("=====init driveraddr:0x%16lx\n",dev.avail_ring);

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
    
    static uint64_t last__v = 0;

    for(int i = 0; i< 1;i++){
        cpu[i].bus = bus;
        
        cpu_init(&cpu[i],i);
        cpu[i].cycle_count = 0;
        tlb_flush(&cpu);
        // 运行模拟器
        printf("Starting RISC-V emulator...cpu privilege: %d\n",cpu[i].privilege);
        printf("Only cpu 0\n");
     
        cpu[i].pc = entry_addr;
        printf("pc[%d]:0x%08lx\n",i,cpu[i].pc);
        if(cpu[i].halted != true){     
            
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
            
<<<<<<< Updated upstream



            //415421361 pc = 0x800053ce
            //415283483 pc = 0x80003486  fsinit

            //80001dbe jinru clint 5380

            //brelse -> releasesleep  chufa clint

            //415421382

            //415389550
            //[c.sdsp] addr:0x      3fffffdf48,pa:0x87fb7f48,x[1]:0x        80000cc2
            //c4e  x[2]:0x      3fffffdf40
            //c7c  x[2]:0x      3fffffdd40 
            // 422351780   forkret panic
            //422248695
            //421315535  namei return 0
            //422043084 kexec return -1
           //422145303
           //422163752    - 18450
           //423254947 forkret ret
           //423206143  0x8000487a  
           //423254850
           // 422381149 80000d20
           // 422887871 0x390
           //422383686  0x87f4f200 store 0
            for( ;j <= 422887891; j++) {
           
                cpu_step(&cpu[i],memory);
                clint_tick(&cpu->clint, 10);
                cpu[i].cycle_count++;
                cpu->csr[CSR_TIME] += 10;
                uint64_t v = bus_read(&cpu->bus,0x8001a397,1);
                 if(v != last__v){
                    
                    printf("v:0x%08lx last_v:0x%08lx,j:%d pc:0x%08lx\n",v,last__v,j,cpu[0].pc);
                    last__v = v;
                 }
                
                if(cpu[0].halted == true){
                    printf("halted j:%d pc:0x%08lx\n",j,cpu[0].pc);
                    break;  
                }

        
                if(cpu[0].pc == 0x80000bba && j > 422301011){
                    log_enable = 0;     

                   // uint64_t val = bus_read(&bus,0x8001a397,1);

                    //printf("0x8001a397:0x%08lx ,j:%d pc:0x%08lx\n",val,j,cpu[0].pc);
                }else{
                    log_enable = 0;
=======
            //429899252 ready to call wait()
            //431817210  0x74
            for( ;j <= 429901894; j++) //  0x74, 
            {       //0x800021b6   compare (pp->parent == p)
                if(cpu[0].pc == 0x3ffffff0b0){
                    printf("[kfork p->trapframe] j:%d a0:0x%08lx,a4:0x%08lx\n",j,cpu[0].gpr[10],cpu[0].gpr[14]);
                }
                if(cpu[0].csr[CSR_TIME] <= 4299023400 && cpu[0].csr[CSR_TIME] >= 4299018400){
                    log_enable = 1;
                    if(cpu[0].pc >= 0x80000bce && cpu[0].pc <= 0x80000c0e) // acquire()
                        log_enable = 0;
                    else if(cpu[0].pc >= 0x80000b8e && cpu[0].pc <= 0x80000bcc) // push_off()
                        log_enable = 0;    
                    else if(cpu[0].pc >= 0x8000187c && cpu[0].pc <= 0x80001896) // mycpu()
                            log_enable = 0;
                    else if(cpu[0].pc >= 0x80000b64 && cpu[0].pc <= 0x80000b8c) // holding()
                        log_enable = 0;
                    else if(cpu[0].pc >= 0x80000c66 && cpu[0].pc <= 0x80000c9e) // release()
                        log_enable = 0;    
                    else if(cpu[0].pc >= 0x80000c12 && cpu[0].pc <= 0x80000c62) // pop_off()
                        log_enable = 0;
                    else if(cpu[0].pc >= 0x80001898 && cpu[0].pc <= 0x800018c6) // myproc()
                        log_enable = 0;
              

                }else if(cpu[0].csr[CSR_TIME] <= 4232536000 && cpu[0].csr[CSR_TIME] >= 4232535800){
                    log_enable = 0;
                }
                
                else{
                    log_enable = 0;
                } 
                cpu_step(&cpu[i],memory);
                
                cpu[0].cycle_count++;
                if(cpu[0].cycle_count % 100 == 0){
                    clint_tick(&cpu->clint, 1);
                }
                
                cpu->csr[CSR_TIME] += 10;
                //uint64_t v = bus_read(&cpu->bus,0x87f99de8,4);
                uint64_t v = get_pa(&cpu[0],0x3fffffe000,ACC_LOAD);
                 if(v != last__v){        
                    printf("v:0x%08lx last_v:0x%08lx,j:%d pc:0x%08lx\n",v,last__v,j,cpu[0].pc);
                    last__v = v;
                 }
                
                if(cpu[0].halted == true){
                    printf("halted j:%d pc:0x%08lx\n",j,cpu[0].pc);
                    break;  
                }

                if(cpu[0].pc == 0x80001d6c && cpu[0].gpr[10] == 0x80010008){
                    if(j > 416893615){
                  //  printf("Found target pc:0x%08lx,j:%d\n",cpu[0].pc,j);
                  // break;
                    }
                }

                if(cpu[0].pc > 0x3fffffffff){
                    printf("error pc addr,j:%d  pc:0x%08lc\n",j,cpu[0].pc);
                    break;
                }

                if(cpu[0].gpr[6] == 0x8000000000087fff){
                   // break;
                }

                if(cpu[0].pc == 0x66){
                   // printf("j:%d pc:0x%08lx\n",j,cpu[0].pc);
                  //  break;
>>>>>>> Stashed changes
                }
    
                if(cpu[0].gpr[0] != 0){
                    printf("j:%d pc:0x%08lx\n",j,cpu[0].pc);
                    break;
                }
<<<<<<< Updated upstream
                
            
              
                if(cpu[0].csr[CSR_TIME] >= 4228878000){
                    if(cpu[0].pc >= 0x800018ac && cpu[0].pc <= 0x800018dc){
                        log_enable = 0;
                    }else if(cpu[0].pc >= 0x80000bc4 && cpu[0].pc <= 0x80000c06){
                        log_enable = 0;
                    }else if(cpu[0].pc >= 0x80000c9c && cpu[0].pc <= 0x80000cd0){
                        log_enable = 0;
                    }else if(cpu[0].pc >= 0x80000c08 && cpu[0].pc <= 0x80000c48){
                        log_enable = 0;
                    }else if(cpu[0].pc >= 0x8000188c && cpu[0].pc <= 0x800018aa){
                        log_enable = 0;
                    }else if(cpu[0].pc >= 0x80000b98 && cpu[0].pc <= 0x80000bc2){
                        log_enable = 0;
                    }else if(cpu[0].pc >= 0x80000c4c && cpu[0].pc <= 0x80000c98){
                        log_enable = 0;
                    }else if(cpu[0].pc >= 0x80001830 && cpu[0].pc <= 0x8000184a){
                        log_enable = 0;
                    }else if(cpu[0].pc >= 0x80000b6c && cpu[0].pc <= 0x80000baa){
                        log_enable = 0;
                    }else if(cpu[0].pc >= 0x80000b42 && cpu[0].pc <= 0x80000b68){
                        log_enable = 0;     
                    }else if(cpu[0].pc >= 0x8000187c && cpu[0].pc <= 0x80001896){
                        log_enable = 0;
                    }
                    else{
                        log_enable = 1;
                    }
                } 

=======
            
     
>>>>>>> Stashed changes
                virtio_disk_update(&cpu[i].cycle_count);

                check_and_handle_interrupts(&cpu[i]);
            
               //uint64_t val = bus_read(&bus,0x87fb7de8,8);
                
               // uint64_t val2 = bus_read(&bus,0x87fb7e90,8);
               // if((j > 414297515) && val != 0x505050505050505){
                   // printf("j:%d pc:0x%08lx\n",j,cpu[i].pc);
                  //  break;
               // }
            }
        }else{
            cpu[i].running = false;
        }

        
        printf("\nFinal CPU state:\n");
        //cpu_dump_registers(&cpu[i]);
        
        printf("Cleaning up...\n");
        
        //free(memory);
        printf("Emulator finished j:%ld,pc:0x%08lx\n",j,cpu[0].pc);


        printf("sstatus:0x%08lx\n",cpu[0].csr[CSR_SSTATUS]);
        printf("sip:0x%08lx\n",cpu[0].csr[CSR_SIP]);

        uint32_t val1 = 0;
        uint32_t val2 = 0;

<<<<<<< Updated upstream
        val1 = bus_read(&bus,0x8001a397,1);
        val2 = bus_read(&bus,0x87fff000,8);
        printf("[0x8001a397] :0x%lx\n",val1);
        printf("[0x87fff000] :0x%lx\n",val2);    
=======
        val1 = bus_read(&bus,0x87f55028,8);
        val2 = bus_read(&bus,0x87f56028,8);
        printf("[0x87f55028] :0x%lx\n",val1);
        printf("[0x87f56028] :0x%lx\n",val2);    

        uint64_t pa1 = get_pa(&cpu[0],0x3ffffff000,ACC_LOAD);
        uint32_t pa2 = get_pa(&cpu[0],0x3fffffe000,ACC_LOAD);
        printf("pa1:0x%08lx,pa2:0x%08lx\n",pa1,pa2);
>>>>>>> Stashed changes

        uint64_t pa = 0x8001a000; // VA 0 的物理地址
        for(int i=0; i<0x400; i+=16) {
            uint64_t val = bus_read(&bus, pa + i, 8);
<<<<<<< Updated upstream
            printf("0x%08lx: 0x%016lx\n", pa + i, val);
        }

        uint64_t vll = bus_read(&bus,0x8001a390,16);
        printf("[0x8001a390] :0x%016llx\n",vll);
=======
           // printf("0x%08lx: 0x%016lx\n", pa + i, val);
        }

        

>>>>>>> Stashed changes
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