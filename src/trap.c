#include "trap.h"
#include "tick.h"
#include "uart.h"

#define DIRECT 0U
#define VECTORED 1U

static bool should_delegate_to_smode(CPU_State *cpu,uint64_t cause,bool is_interrupt){

    if(!is_interrupt){
        return false;
    }

    uint64_t mideleg = read_csr(cpu, CSR_MIDELEG);

    switch (cause)
    {
    case  IRQ_M_TIMER:
        return (mideleg >> 5) & 1;  //STIP
        break;
    case IRQ_M_EXT:
        return (mideleg >> 9) & 1; //SEIP
        break;
    case IRQ_M_SOFT:
        return (mideleg >> 1) & 1; //SSIP
        break;
    default:
        return false;
        break;
    }
}

static void take_smode_trap(CPU_State *cpu, uint64_t cause, bool is_interrupt){

    write_csr(cpu, CSR_SEPC, cpu->pc);
    uint64_t scause = is_interrupt ? (1ULL << 63) | (cause & 0x7fffffff) 
                                   : (cause & 0x7fffffff);

    write_csr(cpu,CSR_SCAUSE,scause);

    write_csr(cpu,CSR_STVAL,0);

    uint64_t sstatus = read_csr(cpu,CSR_SSTATUS);

    if (sstatus & SSTATUS_SIE) {
        sstatus |= SSTATUS_SPIE;
    } else {
        sstatus &= ~SSTATUS_SPIE;
    }
    sstatus &= ~SSTATUS_SIE;

    uint64_t spp = (cpu->privilege == 1) ? 1 : 0; // 1=S, 0=U
    sstatus = (sstatus & ~SSTATUS_SPP) | (spp << 8);
    
    write_csr(cpu,CSR_SSTATUS,sstatus);

    if(cpu->privilege != 1)
        cpu->privilege = 1;

    uint64_t stvec = read_csr(cpu, CSR_STVEC);
    uint64_t base = stvec & ~0x3ULL;
    uint64_t mode = stvec & 0x3;

    if (mode == VECTORED && is_interrupt) { 
        cpu->pc = base + ((uint64_t)cause << 2);
    } else {
        cpu->pc = base;
    }
}

static void take_mmode_tarp(CPU_State *cpu, uint64_t cause, bool is_interrupt){
      /* 1) 保存 mepc = 当前指令地址（spec: address of the ECALL/EBREAK instr）*/
    write_csr(cpu, CSR_MEPC, cpu->pc);

    /* 2) mcause: 高位标志中断 bit31: 1:interrupt  0:exception ,   cause: 异常/中断的类型编号*/
    uint64_t mcause = is_interrupt ? (1u<<63) | (cause & 0x7fffffff) : (cause & 0x7fffffff);
    write_csr(cpu, CSR_MCAUSE, mcause);

    /* 3) mtval（如果适用）— 这里简单置 0，某些异常需要写具体值 */
    write_csr(cpu, CSR_MTVAL, 0);

    /* 4) 保存 mstatus: MPIE = MIE ; MIE = 0 ; MPP = 当前特权等级 */
    uint64_t mstatus = read_csr(cpu, CSR_MSTATUS);
    if (mstatus & MSTATUS_MIE) mstatus |= MSTATUS_MPIE; else mstatus &= ~MSTATUS_MPIE;
    mstatus &= ~MSTATUS_MIE;
    mstatus = (mstatus & ~MSTATUS_MPP_MASK) | ((uint64_t)(cpu->privilege & 3) << MSTATUS_MPP_SHIFT);
    write_csr(cpu, CSR_MSTATUS, mstatus);

    cpu->privilege = 3; /* M */
    
    uint64_t mtvec = read_csr(cpu, CSR_MTVEC);
    uint64_t base = mtvec & ~0x3ULL;
    uint64_t mode = mtvec & 0x3;// 0:direct 1:vectored

    if (mode == VECTORED && is_interrupt) { 
        cpu->pc = base + ((uint64_t)cause << 2);
    } else {
        cpu->pc = base;
    }
}

/* cause: 低位为异常/中断编号； is_interrupt: true 表示中断(need set mcause MSB) */
void take_trap(CPU_State *cpu, uint64_t cause, bool is_interrupt){

    if(should_delegate_to_smode(cpu,cause,is_interrupt))
        take_smode_trap(cpu,cause,is_interrupt);
    else
        take_mmode_tarp(cpu,cause,is_interrupt);
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



void check_and_handle_interrupts(CPU_State *cpu){
    uint64_t current_privilege = cpu->privilege;
    bool take_interrupt = false;
    uint64_t m_cause = 0;  // M-mode 中断号
    bool interrupts_enabled = false;

    if (current_privilege == 3) { //M
        interrupts_enabled = (cpu->csr[CSR_MSTATUS] & MSTATUS_MIE) != 0;
    } else if (current_privilege == 1) { //S or U
        interrupts_enabled = (cpu->csr[CSR_SSTATUS] & SSTATUS_SIE) != 0;
    } 

    if(!interrupts_enabled) return;

    uint64_t mip = cpu->csr[CSR_MIP];
    uint64_t mie = cpu->csr[CSR_MIE];

    // 外部中断（优先级最高）
    if ((mip & MIP_MEIP) && (mie & MIE_MEIE)) {
        m_cause = IRQ_M_EXT;      // 11
        take_interrupt = true;
        printf("[INT] M-mode external interrupt pending\n");
    } else if ((mip & MIP_MTIP) && (mie & MIE_MTIE)) {// 定时器中断
        m_cause = IRQ_M_TIMER;    // 7
        take_interrupt = true;
        printf("[INT] M-mode timer interrupt pending\n");
    }else if ((mip & MIP_MSIP) && (mie & MIE_MSIE)) {// 软件中断
        m_cause = IRQ_M_SOFT;     // 3
        take_interrupt = true;
        printf("[INT] M-mode software interrupt pending\n");
    }
    // 3. 如果需要处理中断
    if (take_interrupt) {
        // 对于软件中断，可以在这里清除
        if (m_cause == IRQ_M_SOFT) {
            cpu->csr[CSR_MIP] &= ~MIP_MSIP;
        }
        
        take_trap(cpu, m_cause, true);
    }
}
