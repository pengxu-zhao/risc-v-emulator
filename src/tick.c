
#include "tick.h"
#include <stdio.h>
#include "trap.h"
#include "instructions.h"
#include <string.h>

void check_pending_and_take(CPU_State *cpu){
    uint64_t mstatus = (uint64_t)read_csr(cpu, CSR_MSTATUS);
    if (!(mstatus & MSTATUS_MIE)) {
        //printf("[tick] Interrupts globally disabled\n");    
        return; /* 全局中断没开 */
    }
    uint64_t mie = (uint64_t)read_csr(cpu, CSR_MIE);
    uint64_t mip = (uint64_t)read_csr(cpu, CSR_MIP);
    uint64_t pending = mie & mip;
    if (!pending) {
        //printf("[tick] No enabled interrupts pending\n");   
        return;
    }
    /*
    mie = 中断使能寄存器（控制允许哪些中断进来）

    mip = 中断挂起寄存器（记录哪些中断源已经触发）

    pending = mie & mip → 得到 当前真正可触发的中断集合。
    
    */

    /* 简单优先级： external -> timer -> software */

    printf("--------pending:0x%08x\n",pending);
    if (pending & MIP_MEIP) take_trap(cpu, IRQ_M_EXT, true);
    else if (pending & MIP_MTIP) take_trap(cpu, IRQ_M_TIMER, true);
    else if (pending & MIP_MSIP) take_trap(cpu, IRQ_M_SOFT, true);
}


void clint_tick(CPU_State *cpu, uint64_t cycles){
    cpu->mtime += cycles;
   // printf("[clint tick] mtime:%ld >= mtimecmp:%ld ?\n",cpu->mtime,cpu->mtimecmp);
    if (cpu->mtime >= cpu->mtimecmp) {
        uint64_t mip = read_csr(cpu, CSR_MIP);
        mip |= MIP_MTIP;
        write_csr(cpu, CSR_MIP, mip);
        printf("mip:0x%08lx\n",mip);
    }
    
    /* 再检查是否要实际进入 trap（满足全局 MIE 且 对应 mie 掩码） */
    check_pending_and_take(cpu);
}



// test:  switch tasks by tick


typedef struct {
    taskContext ctx;
    int id;
} Task;

#define MAX_TASKS 2
Task tasks[MAX_TASKS];

uint32_t current_task_id = 0;


void save_context(CPU_State* cpu,Task* text){
    for(int i = 0; i < NUM_GPR; i++){
        text->ctx.gpr[i] = cpu->gpr[i];       
    }
    text->ctx.pc = cpu->pc;
}

void restore_context(CPU_State* cpu,Task* text){
    for(int i = 0; i < NUM_GPR; i++){
        cpu->gpr[i] = text->ctx.gpr[i];
    }
    cpu->pc = text->ctx.pc;
}

void switch_task(CPU_State *cpu) {

    printf("switch task-----now pc:0x%08x\n",cpu->pc);
    save_context(cpu,&tasks[current_task_id]);
    current_task_id = (current_task_id + 1) % 2;
    restore_context(cpu,&tasks[current_task_id]);

    printf("Switched to task %d\n", current_task_id);
}

void trap_handler2(CPU_State *cpu, uint32_t cause, bool is_interrupt) {
    if (is_interrupt && cause == IRQ_M_TIMER) {
        printf("[Timer Interrupt] Triggered!\n");
        switch_task(cpu); 
    } else if (!is_interrupt && cause == EXC_ECALL_U) {
        printf("[ECALL] User syscall\n");
        
    } else {
        printf("[Trap] cause=%d, interrupt=%d\n", cause, is_interrupt);
    }
}
