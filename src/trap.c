#include "trap.h"
#include "tick.h"
#include "uart.h"

/* cause: 低位为异常/中断编号； is_interrupt: true 表示中断(need set mcause MSB) */
void take_trap(CPU_State *cpu, uint64_t cause, bool is_interrupt){
    #define DIRECT 0U
    #define VECTORED 1U
    /* 1) 保存 mepc = 当前指令地址（spec: address of the ECALL/EBREAK instr）*/
    write_csr(cpu, CSR_MEPC, cpu->pc);

    /* 2) mcause: 高位标志中断 bit31: 1:interrupt  0:exception ,   cause: 异常/中断的类型编号*/
    uint64_t mcause = is_interrupt ? (1u<<31) | (cause & 0x7fffffff) : (cause & 0x7fffffff);
    write_csr(cpu, CSR_MCAUSE, mcause);

    /* 3) mtval（如果适用）— 这里简单置 0，某些异常需要写具体值 */
    write_csr(cpu, CSR_MTVAL, 0);

    /* 4) 保存 mstatus: MPIE = MIE ; MIE = 0 ; MPP = 当前特权等级 */
    uint64_t mstatus = read_csr(cpu, CSR_MSTATUS);
    if (mstatus & MSTATUS_MIE) mstatus |= MSTATUS_MPIE; else mstatus &= ~MSTATUS_MPIE;
    mstatus &= ~MSTATUS_MIE;
    mstatus = (mstatus & ~MSTATUS_MPP_MASK) | ((uint64_t)(cpu->privilege & 3) << MSTATUS_MPP_SHIFT);
    write_csr(cpu, CSR_MSTATUS, mstatus);

    /* 5) 切换到 M-mode */
    cpu->privilege = 3; /* M */
    
    if(is_interrupt && cause == IRQ_M_TIMER){
     //   switch_task(cpu);
        cpu->mtimecmp = cpu->mtime + 1000; 
    }

    /* 6) 跳到 mtvec（考虑 vectored 模式对中断的处理） */
    uint64_t mtvec = read_csr(cpu, CSR_MTVEC);
    //printf("mtvec:0x%08x\n",mtvec);
    uint64_t base = mtvec & ~0x3ULL;
    uint64_t mode = mtvec & 0x3;// 0:direct 1:vectored
   //printf("base ? 0x%08x\n",base);
    if (mode == VECTORED && is_interrupt) { 
        cpu->pc = base + ((uint64_t)cause << 2);
    } else {
        cpu->pc = base;
    }

    

    #undef DIRECT
    #undef VECTORED
}

void do_mret(CPU_State *cpu){
    uint64_t mstatus = read_csr(cpu, CSR_MSTATUS);
    uint64_t mpp = (mstatus & MSTATUS_MPP_MASK) >> MSTATUS_MPP_SHIFT;

    /* mstatus.MIE = mstatus.MPIE ; MPIE = 1 ; MPP = 0 ; 恢复特权模式与 pc = mepc */
    if (mstatus & MSTATUS_MPIE) mstatus |= MSTATUS_MIE; else mstatus &= ~MSTATUS_MIE;
    mstatus |= MSTATUS_MPIE; /* MPIE <- 1 per spec */
    mstatus &= ~MSTATUS_MPP_MASK; /* clear MPP */
    write_csr(cpu, CSR_MSTATUS, mstatus);

    cpu->privilege = mpp; /* restore mode */
    cpu->pc = (uint64_t) read_csr(cpu, CSR_MEPC);
}

