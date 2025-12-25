// src/cpu.c
#include "cpu.h"
#include "decode.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "memory.h"
#include "elf_load.h"
#include "uart.h"
#include "trap.h"
#include "timer.h"
#include "trap_vector.h"
#include "tick.h"
#include "dts.h"
#include "plic.h"
extern uint8_t* memory;
extern Bus bus;
extern int log_enable;
void cpu_init(CPU_State* cpu, uint8_t core_id) {
  
    if (cpu == NULL) {
        printf("ERROR: CPU pointer is NULL!\n");
        return;
    }
    
    // 清零所有状态
    memset(cpu, 0, sizeof(CPU_State));
    

    cpu->mem_size = MEMORY_SIZE;
    cpu->mem = memory;

    cpu->use_relaxed_memory = 0;//use_relaxed;
    
 
    // 设置初始PC
    //uint64_t entry_addr;
    //load_elf32_virt(cpu,"../../tests/elf1.elf",&entry_addr);
    //load_elf32_virt(cpu,"../../build-rtos/mini-rtos/rtos",&entry_addr);


        // 创建设备树
        /*
    uint64_t dtb_addr = 0x80040000;
    uint64_t dtb_offset = dtb_addr - MEMORY_BASE;
    create_complete_device_tree(memory, dtb_offset);
    W
    // 设置a1寄存器指向设备树（RISC-V启动约定）
    cpu->gpr[11] = dtb_addr;  // a1 = dtb pointer

    verify_dtb_for_opensbi(memory,dtb_offset);
   
    
    // 验证设备树
    uint32_t magic = *(uint32_t*)&memory[dtb_offset];
    printf("设备树魔数: 0x%08x %s\n", magic, magic == FDT_MAGIC ? "✅" : "❌");
    */
    timer_init(cpu);

    cpu->bus = bus;
    // 初始化运行状态
    cpu->running = true;

    // 初始化重要的CSR寄存器
    //cpu->csr[CSR_MTVEC] = 0x80000278;
  
    cpu->csr[CSR_MSTATUS] = 0x1800;
    cpu->csr[CSR_MISA] |= ((2ULL << 62)  // MXL=2 (RV64)
                            | (1 << 8)   //I - 基础ISA
                            | (1 << 12)  // M - 乘除法
                            | (1 << 2) // C - 压缩指令
                            | (1 << 0)); // A - 原子指令
    cpu->csr[CSR_MCOUNTERN] = 0x7;// 允许用户模式访问cycle/time/instret计数器
    cpu->csr[CSR_MHARTID] = core_id;
    cpu->mip = cpu->csr[CSR_MIP];
    cpu->mie = cpu->csr[CSR_MIE];

    
    cpu->privilege = 3;         // 初始用 M-mode（方便调度）
    //cpu->satp = (8 << 28) | (4*1024 >> 12);
    printf("cpu init:0x%08x\n",cpu->satp);
    //init_page_table(cpu);


    // 开启 M 模式下的全局中断
    write_csr(cpu, CSR_MSTATUS, read_csr(cpu, CSR_MSTATUS) | MSTATUS_MIE);
    // 允许定时器中断
    write_csr(cpu, CSR_MIE, read_csr(cpu, CSR_MIE) | MIP_MTIP);

    // 设置中断向量（trap handler 地址）
    //write_csr(cpu, CSR_MTVEC, TRAP_HANDLER_ADDRESS); 

    // 初始化指令表
    init_instruction_table();
    init_syscall();
 
    printf("CPU initialization complete\n");
}

void cpu_step(CPU_State* cpu, uint8_t* memory) {

    if(cpu->halted){
        cpu->pc += 4;
        return;
    }

    if (cpu == NULL) {
        printf("ERROR: CPU pointer is NULL in cpu_step!\n");
        return;
    }
    
    if (memory == NULL) {
        printf("ERROR: Memory pointer is NULL in cpu_step!\n");
        return;
    }
    
    if(log_enable){
        printf("Fetching instruction from PC: 0x%08x\n", cpu->pc);
    }

    // 取指
    
    uint64_t instruction = fetch_instruction(cpu, memory);
    if(log_enable){
    printf("Instruction: 0x%08x\n", instruction);
    }
    // 解码和执行
    decode_and_execute(cpu, instruction);
    
    /*
    uint64_t mtvec = read_csr(cpu, CSR_MTVEC);
    uint64_t base = mtvec & ~0x3ULL;
    //检测并处理中断
    
    if(cpu->csr[CSR_MSTATUS] & MSTATUS_MIE){
        for (int i = 0; i < 32; i++) {
            if ( cpu->irq_pending[i] && ( cpu->csr[CSR_MIE] &(1U << i)) ) {
                cpu->csr[CSR_MCAUSE] = (1u << 31) | i;
                cpu->csr[CSR_MEPC]   = cpu->pc;
                
                if(mtvec & 0x1) { 
                // Vectored mode
                    cpu->pc = base + (i << 2);
                } else {
                // Direct mode
                cpu->pc = base;
                }
                //printf("[Uart] cpu pc:0x%08x\n",cpu->pc);
                cpu->irq_pending[i] = 0;
                break; // 一次只处理一个中断
                
            }
        }
    }


    //处理 Direct / Vectored trap 异常
    uint64_t cause = (cpu->privilege == 0 ? EXC_ECALL_U :
                    cpu->privilege == 1 ? EXC_ECALL_S : EXC_ECALL_M);

    if(cpu->pc == base) { // trap_entry
       trap_handler(cpu);
    }else if(cpu->pc == base +  ((uint64_t)cause << 2)){
        trap_vectored_handler(cpu);
    } */
    // 更新性能计数器
    cpu->cycle_count++;
    cpu->inst_count++;
   
   // clint_tick(cpu, 1); // 1 cycle
    //printf("Step completed. New PC: 0x%08x\n", cpu->pc);
}

void cpu_run(CPU_State* cpu, uint8_t* memory) {
    printf("Starting CPU execution...\n");
    
    while (cpu->running) {
        cpu_step(cpu, memory);
        
        // 简单的退出条件：PC为0或达到最大指令数
        if (cpu->pc == 0 || cpu->inst_count > 1000) {
            cpu->running = false;
            printf("Execution stopped: PC=0x%08x, Instructions=%lu\n", cpu->pc, cpu->inst_count);
        }
    }
}

void cpu_dump_registers(CPU_State* cpu) {
    if (cpu == NULL) {
        printf("ERROR: CPU pointer is NULL!\n");
        return;
    }
    
    printf("\n=== CPU Register Dump ===\n");
    printf("PC: 0x%08x\n", cpu->pc);
    printf("Cycles: %lu, Instructions: %lu\n", cpu->cycle_count, cpu->inst_count);
    printf("Running: %s\n", cpu->running ? "Yes" : "No");
    
    printf("\nGeneral Purpose Registers:\n");
    for (int i = 0; i < 32; i++) {
        const char* reg_names[] = {
            "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
            "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
            "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
            "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
        };
        
        printf("x%-2d (%s): 0x%08x", i, reg_names[i], cpu->gpr[i]);
        
        if ((i + 1) % 4 == 0) printf("\n");
        else printf("\t");
    }
    
    printf("\nCSR Registers:\n");
    printf("mstatus: 0x%08x\n", cpu->csr[CSR_MSTATUS]);
    printf("mtvec:   0x%08x\n", cpu->csr[CSR_MTVEC]);
    printf("mepc:    0x%08x\n", cpu->csr[CSR_MEPC]);
    printf("mcause:  0x%08x\n", cpu->csr[CSR_MCAUSE]);
    printf("mtval:   0x%08x\n", cpu->csr[CSR_MTVAL]);
    //printf("mscratch:0x%08x\n", cpu->csr[CSR_MSCRATCH]);
    printf("mie:     0x%08x\n", cpu->csr[CSR_MIE]);
    printf("mip:     0x%08x\n", cpu->csr[CSR_MIP]);
}

/*
    31          22 21                     0
    +--------------+-----------------------+
    |   ASID[9:0]  |       PPN[21:0]       |
    +--------------+-----------------------+
*/

void csr_write_satp(CPU_State *cpu, uint64_t value) {
    cpu->satp = value;
    cpu->asid = (value >> 22) & 0x3FF;  // 取 satp[31:22] 作为 ASID
}

