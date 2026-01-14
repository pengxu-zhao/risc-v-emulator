// src/cpu.c
#include "cpu.h"
#include "bus.h"
#include "decode.h"

extern uint8_t* memory;
extern Bus bus;
extern int log_enable;

CPU_State cpu[MAX_CORES];

CPU_State* get_current_cpu(void) {
    return &cpu[0];
}

static void cpu_timer_interrupt_callback(void);
static void cpu_software_interrupt_callback(void);

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


    clint_init(&cpu->clint);
    cpu->clint.timer_interrupt_callback = cpu_timer_interrupt_callback;
    cpu->clint.software_interrupt_callback = cpu_software_interrupt_callback;
   

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

uint64_t get_cpu_cycle(CPU_State *cpu){
    return cpu->cycle_count;
}

// 时钟中断回调函数
static void cpu_timer_interrupt_callback(void) {
    // 这个函数会在时钟中断状态变化时被 CLINT 调用
    // 更新 CPU 的 MIP 寄存器
    CPU_State* cpu = get_current_cpu(); // 需要获取当前 CPU 上下文
    
    if (cpu) {
        if (clint_get_timer_interrupt(&cpu->clint)) {
            cpu->mip |= MIP_MTIP;
        } else {
            cpu->mip &= ~MIP_MTIP;
        }
        
        // 检查是否应该处理中断
        cpu_check_interrupts(cpu);
    }
}

// 软件中断回调函数
static void cpu_software_interrupt_callback(void) {
    CPU_State* cpu = get_current_cpu();
    
    if (cpu) {
        if (clint_get_software_interrupt(&cpu->clint)) {
            cpu->mip |= MIP_MSIP;
        } else {
            cpu->mip &= ~MIP_MSIP;
        }
        
        cpu_check_interrupts(cpu);
    }
}

// 检查并处理中断
void cpu_check_interrupts(CPU_State* cpu) {
    if (!cpu) return;
    
    // 检查全局中断使能
    if (!(cpu->mstatus & (1 << 3))) {  // MIE bit
        return;  // 全局中断禁用
    }
    
    // 检查是否有使能且待处理的中断
    uint64_t pending = cpu->mip & cpu->mie;
    
    // 定时器中断 (优先级通常较高)
    if (pending & MIP_MTIP) {
        cpu_take_interrupt(cpu, 0x8000000000000007ULL); // Machine timer interrupt
        return;
    }
    
    // 软件中断
    if (pending & MIP_MSIP) {
        cpu_take_interrupt(cpu, 0x8000000000000003ULL); // Machine software interrupt
        return;
    }
}

// 处理中断
void cpu_take_interrupt(CPU_State* cpu, uint64_t cause) {
    // 保存当前状态
    cpu->mepc = cpu->pc;
    cpu->mcause = cause;
    
    // 设置 MPP 和 MPIE
    uint64_t mpp = (cpu->mstatus >> 11) & 0x3;  // 当前特权级
    cpu->mstatus &= ~(0x3 << 11);  // 清除 MPP
    cpu->mstatus |= (mpp << 11);   // 保存当前特权级
    
    cpu->mstatus &= ~(1 << 7);     // 清除 MPIE
    cpu->mstatus |= ((cpu->mstatus >> 3) & 1) << 7;  // 保存 MIE 到 MPIE
    
    // 进入 M-mode
    cpu->mstatus &= ~(0x3 << 11);  // MPP = M-mode
    cpu->mstatus &= ~(1 << 3);     // 禁用中断 (MIE=0)
    
    // 跳转到中断处理程序
    cpu->pc = cpu->mtvec;
    
    printf("[CPU] Taking interrupt, cause: 0x%016lx, mepc: 0x%016lx\n", cause, cpu->mepc);
}