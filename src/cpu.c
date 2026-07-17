// src/cpu.c
#include "cpu.h"
#include "bus.h"
#include "decode.h"
#include "plic.h"
#include "softfloat.h"
#include <time.h>
extern uint8_t* memory;
extern Bus bus;
extern int log_enable;
extern PLICState plic;
extern int j;
CPU_State cpu[MAX_CORES];

CPU_State* get_current_cpu(void) {
    return &cpu[0];
}
static uint64_t start_host_ns;
void timer_init(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    start_host_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    

}

uint64_t read_mtime(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    uint64_t delta_ns = now_ns - start_host_ns;

    // 1 MHz -> 每 1000ns 计数器加 1
    return delta_ns / 1000;
}


void softfloat_init(void) {
    softfloat_roundingMode = softfloat_round_near_even; // 默认舍入模式
    softfloat_exceptionFlags = 0;
    // RISC‑V 规范要求 tininess 检测时机在 rounding 之后
    softfloat_detectTininess = softfloat_tininess_afterRounding;
}
void cpu_init(CPU_State* cpu, uint8_t core_id) {
  
    if (cpu == NULL) {
        printf("ERROR: CPU pointer is NULL!\n");
        return;
    }
    
    // 清零所有状态
    memset(cpu, 0, sizeof(CPU_State));
    cpu->csr[CSR_MHARTID] = core_id;
    cpu->cycle_count = 0;
    cpu->bus = bus;
    cpu->mem_size = MEMORY_SIZE;
    cpu->mem = memory;
    cpu->pc = SBI_LOAD_ADDR;

    cpu->use_relaxed_memory = 0;//use_relaxed;
    cpu->privilege = 3; // M-mode

    clint_init(&cpu->clint);
    cpu->bus = bus;
    cpu->running = true;
    cpu->mip = cpu->csr[CSR_MIP];
    cpu->mie = cpu->csr[CSR_MIE];
    cpu->csr[CSR_MISA] = 1ULL << 18  // 支持 S 模式
                           | 1ULL << 20  // 支持 U 模式;
                           | 1ULL << 63 // RV64
                           | 1ULL << 7; //H 


    cpu->h_extension = !!(cpu->csr[CSR_MISA] & ( 1 << 7));

    cpu->gpr[10] = 0;                 // a0 = hartid
    cpu->gpr[11] = DTB_LOAD_ADDR;        // a1 = dtb address
    cpu->csr[CSR_FCSR] = 0;
    cpu->csr[CSR_FFLAGS] = 0;
    cpu->csr[CSR_FRM] = 0;

    // 初始化指令表
    init_instruction_table();
    init_syscall();

    softfloat_init();
 
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
          RESET  "," GREEN"j:" RESET RED"%ld" RESET"," GREEN "Hart:" RESET RED "%d" RESET "\n", cpu->pc,j,cpu->csr[CSR_MHARTID]);
    }

    // 取指
    uint64_t instruction = fetch_instruction(cpu, memory);
    if(log_enable){
        printf("Instruction: 0x%08x\n", instruction);
    }

    if(instruction == 0){
        if(log_enable)
            printf("ERROR: Invalid instruction (0) at PC: 0x%08lx\n", cpu->pc);
        return;
    }
    // 解码和执行
    decode_and_execute(cpu, instruction);
    
    
    //clint_tick(&cpu->clint,read_mtime());

    cpu->cycle_count++;
    if(cpu->cycle_count % 100 == 0){
        clint_tick(&cpu->clint, 800);
    }
    cpu->csr[CSR_TIME] += 10;
    
    // 更新性能计数器
    cpu->inst_count++;
    cpu->csr[CSR_RDCYCLE] = cpu->cycle_count;
    cpu->csr[CSR_INSTRET] = cpu->inst_count;

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

uint64_t read_csr(CPU_State *cpu, unsigned id){ 
    if(cpu->v)
    {
        switch(id){
            case CSR_SSTATUS : return cpu->vsstatus;
            case CSR_STVEC : return cpu->vstvec;
            case CSR_SEPC : return cpu->vsepc;
            case CSR_SATP : return cpu->vsatp;
            case CSR_SCAUSE: return cpu->vscause;
            case CSR_SIP : return cpu->vsip;
            case CSR_SIE : return cpu->vsie;
            case CSR_STVAL : return cpu->vstval;
           
            default: break;
        }
    }
    return cpu->csr[id & 0xfff]; 
}
void write_csr(CPU_State *cpu, unsigned id, uint64_t v){ 

    if(cpu->v){
        switch (id)
        {
            case CSR_SSTATUS :  cpu->vsstatus = v; break;
            case CSR_STVEC :  cpu->vstvec = v; break;
            case CSR_SEPC :  cpu->vsepc = v; break;
            case CSR_SATP :  cpu->vsatp = v; break;
            case CSR_SCAUSE:  cpu->vscause = v; break;
            case CSR_SIP :  cpu->vsip = v ; break;
            case CSR_SIE :  cpu->vsie = v; break;
            case CSR_STVAL :  cpu->vstval = v ; break;
            default:
                break;
       
        }
    }

    cpu->csr[id & 0xfff] = v; 
}