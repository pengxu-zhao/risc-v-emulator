#include "timer.h"

#define MAX_TASKS 2



void timer_init(CPU_State* cpu){
    // 设置一个较小的mtimecmp值，确保很快触发中断
    cpu->mtimecmp = 1000;
    cpu->mtime = 0;
    
    // 确保定时器中断使能
    uint64_t mie = read_csr(cpu, CSR_MIE);
    mie |= MIP_MTIP;
    write_csr(cpu, CSR_MIE, mie);
    
    // 确保全局中断使能
    uint64_t mstatus = read_csr(cpu, CSR_MSTATUS);
    mstatus |= MSTATUS_MIE;
    write_csr(cpu, CSR_MSTATUS, mstatus);
    
    printf("Timer initialized: mtimecmp=%lu\n", cpu->mtimecmp);
}

