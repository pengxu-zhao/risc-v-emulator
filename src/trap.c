#include "trap.h"
#include "uart.h"

#define DIRECT 0U
#define VECTORED 1U
extern int log_enable;

static bool should_delegate_to_smode(CPU_State *cpu,uint64_t cause){


    uint64_t mideleg = read_csr(cpu, CSR_MIDELEG);

    switch (cause)
    {
        case IRQ_S_SOFT:   // 1
        case IRQ_S_TIMER:  // 5
        case IRQ_S_EXT:    // 9
            return true;
        case  IRQ_M_TIMER:
            return (mideleg >> 5) & 1;  //STIP
        case IRQ_M_EXT:
            return (mideleg >> 9) & 1; //SEIP
        case IRQ_M_SOFT:
            return (mideleg >> 1) & 1; //SSIP
        default:
            return false;
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

    if(log_enable){
        printf("[S-Mode Trap] stvec:0x%16lx, scause:0x%16lx\n", stvec, scause);
    }
    uint64_t base = stvec & ~0x3ULL;
    uint64_t mode = stvec & 0x3;

    if (mode == VECTORED && is_interrupt) { 
        cpu->pc = base + ((uint64_t)cause << 2);
    } else {
        cpu->pc = base;
    }

    if(log_enable){
        printf("[S-Mode Trap]pc:0x%16lx\n", cpu->pc);
       
    }

}

static void take_mmode_trap(CPU_State *cpu, uint64_t cause, bool is_interrupt){
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

    if(should_delegate_to_smode(cpu,cause))
        take_smode_trap(cpu,cause,is_interrupt);
    else
        take_mmode_trap(cpu,cause,is_interrupt);
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
    uint64_t cause = 0; 
    bool interrupts_enabled = false;
    bool to_s_mode = false;
    uint64_t mip = cpu->csr[CSR_MIP];
    uint64_t mie = cpu->csr[CSR_MIE];
    uint64_t sie = cpu->csr[CSR_SIE];
    uint64_t mideleg = cpu->csr[CSR_MIDELEG];
    uint64_t sstatus = cpu->csr[CSR_SSTATUS];
    uint64_t mstatus = cpu->csr[CSR_MSTATUS];


    if(current_privilege == 3){ //M mode
        if (cpu->csr[CSR_MSTATUS] & MSTATUS_MIE){ 
            // 外部中断（优先级最高）
            if (mip & MIP_MEIP) {
                if (!(mideleg & (1 << 9))){
                    if(mie & MIE_MEIE)
                        cause = IRQ_M_EXT;      // 11
                }
                else{ 
                    if ((sstatus & SSTATUS_SIE ) && (sie & SIE_SEIE))
                        cause = IRQ_S_EXT;
                    else if(mie & MIE_MEIE)
                        cause = IRQ_M_EXT;
                }
            } else if ((mip & MIP_MTIP) ) {// 定时器中断
                if (!(mideleg & (1 << 5))){
                    if(mie & MIE_MTIE)
                        cause = IRQ_M_TIMER;    // 7
                }
                else{
                    if((sstatus & SSTATUS_SIE ) && (sie & SIE_STIE))
                        cause = IRQ_S_TIMER;
                    else if(mie & MIE_MTIE)
                        cause = IRQ_M_TIMER;
                }
            }else if(mip & MIP_MSIP) {// 软件中断
                if (!(mideleg & (1 << 1))){
                    if(mie & MIE_MSIE)
                        cause = IRQ_M_SOFT;     // 3  
                }
                else{
                    if((sstatus & SSTATUS_SIE ) && (sie & SIE_SSIE))
                        cause = IRQ_S_SOFT;
                    else if(mie & MIE_MSIE)
                        cause = IRQ_M_SOFT;
                }
            }
        }
    }else if(current_privilege <= 1){

        uint64_t sip_seip = (mip & MIP_MEIP) && (mideleg & (1 << 9)) ? SIP_SEIP:0;
        uint64_t sip_stip = (mip & MIP_MTIP) && (mideleg & (1 << 5)) ? SIP_STIP:0;
        uint64_t sip_ssip = (mip & MIP_MSIP) && (mideleg & (1 << 1)) ? SIP_SSIP:0;
        
        if(current_privilege == 1){
            if(!(cpu->csr[CSR_SSTATUS] & SSTATUS_SIE))   return;
        }

        if (sip_seip && (sie & SIE_SEIE)){  
            cause = IRQ_S_EXT;
        }else if(sip_stip && (sie & SIE_STIE)){
            cause = IRQ_S_TIMER;
        }else if(sip_ssip && (sie & SIE_SSIE)){
            cause = IRQ_S_SOFT;
        }
    }

    if (cause == IRQ_M_SOFT || cause == IRQ_S_SOFT) {
        cpu->csr[CSR_MIP] &= ~MIP_MSIP;
    }

    switch (cause)
    {
        case IRQ_S_SOFT:   // 1
        case IRQ_S_TIMER:  // 5
        case IRQ_S_EXT:    // 9
            take_interrupt = true;
            to_s_mode = true;
            break;
        case  IRQ_M_TIMER:
        case IRQ_M_EXT:
        case IRQ_M_SOFT:
            take_interrupt = true;
            to_s_mode = false;
            break;
        default:
            break;
    }

    if(log_enable && take_interrupt){
        printf("[Interrupt Check] privilege=%d, mip=0x%016lx, mie=0x%016lx, sie=0x%016lx, mideleg=0x%016lx, sstatus=0x%016lx, mstatus=0x%016lx, take_interrupt=%d, cause=%lu\n",
               current_privilege, mip, mie, sie, mideleg, sstatus, mstatus, take_interrupt, cause);
    }
  
    if(take_interrupt){
        if(!to_s_mode){
            take_mmode_trap(cpu,cause,true);
        }else{
            take_smode_trap(cpu,cause,true);
        }
    }

}
