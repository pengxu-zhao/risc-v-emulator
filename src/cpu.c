// src/cpu.c
#include "cpu.h"
#include "bus.h"
#include "decode.h"
#include "plic.h"

extern uint8_t* memory;
extern Bus bus;
extern int log_enable;
extern PLICState plic;
extern int j;
CPU_State cpu[MAX_CORES];

CPU_State* get_current_cpu(void) {
    return &cpu[0];
}

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
    cpu->privilege = 3; // M-mode

    clint_init(&cpu->clint);
    cpu->bus = bus;
    cpu->running = true;
    cpu->mip = cpu->csr[CSR_MIP];
    cpu->mie = cpu->csr[CSR_MIE];

    // 初始化指令表
    init_instruction_table();
    init_syscall();
 
    printf("CPU initialization complete\n");
}

void cpu_step(CPU_State* cpu, uint8_t* memory) {

    if (cpu == NULL) {
        printf("ERROR: CPU pointer is NULL in cpu_step!\n");
        return;
    }
    
    if (memory == NULL) {
        printf("ERROR: Memory pointer is NULL in cpu_step!\n");
        return;
    }
    
    if(log_enable){
        printf("Fetching instruction from " GREEN "pc:" RESET RED "0x%08lx" 
          RESET  "," GREEN"j:" RESET RED"%ld\n" RESET, cpu->pc,j);
    }

    // 取指
    uint64_t instruction = fetch_instruction(cpu, memory);
    if(log_enable){
    printf("Instruction: 0x%08x\n", instruction);
    }
    // 解码和执行
    decode_and_execute(cpu, instruction);
    
    cpu->cycle_count++;
    if(cpu->cycle_count % 100 == 0){
        clint_tick(&cpu->clint, 1);
    }
    cpu->csr[CSR_TIME] += 10;
    
    // 更新性能计数器
    cpu->inst_count++;

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

uint64_t get_cpu_cycle(CPU_State *cpu){
    return cpu->cycle_count;
}

bool cpu_has_interrupts_pending(CPU_State *cpu) {
    uint64_t pending = cpu->csr[CSR_MIP] & MIP_MEIP;
    return pending != 0;
}


void cpu_wakeup(CPU_State *cpu) {
    pthread_mutex_lock(&cpu->lock);
    cpu->halted = 0;
    pthread_cond_signal(&cpu->cond);
    pthread_mutex_unlock(&cpu->lock);
}

void cpu_try_wakeup(CPU_State *cpu) {

    pthread_mutex_lock(&cpu->lock);

    if (cpu->halted && cpu_has_interrupts_pending(cpu)) {
        printf("[CPU Wakeup] CPU is halted but has pending interrupts. Waking up...\n");
        printf("j:%d, pc:0x%08lx\n",j,cpu->pc);
        cpu->halted = 0;
        pthread_cond_signal(&cpu->cond);
    }

    pthread_mutex_unlock(&cpu->lock);
}