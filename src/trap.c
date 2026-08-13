#include "trap.h"
#include "uart.h"

#define DIRECT 0U
#define VECTORED 1U
extern int log_enable;
extern int j;

void take_vsmode_trap(CPU_State *cpu, uint64_t cause, bool is_interrupt){


    // 1. 保存当前pc到sepc
    cpu->vsepc = cpu->pc;
    uint64_t scause = is_interrupt ? (1ULL << 63) | (cause & 0x7fffffff) 
                                   : (cause & 0x7fffffff);

    cpu->vscause = scause;
    
    // 对于异常，硬件会自动把故障地址写入 vstval
    if (!is_interrupt) {
        cpu->vstval = cpu->mem_fault.vaddr;
    } else {
        // 中断时 vstval 可以为 0（规范未强制）
        cpu->vstval = 0;
    }

    uint64_t sstatus = cpu->vsstatus;

    // 2.把当前sie保存到spie
    if (sstatus & SSTATUS_SIE) {
        sstatus |= SSTATUS_SPIE;
    } else {
        sstatus &= ~SSTATUS_SPIE;
    }
    // 3. 关闭中断（清SIE位）
    sstatus &= ~SSTATUS_SIE;

    // 4. 设置SPP位为当前特权级（0=U, 1=S）
    uint64_t spp = (cpu->privilege == 1) ? 1 : 0; // 1=S, 0=U
    sstatus = (sstatus & ~SSTATUS_SPP) | (spp << 8);
    
    cpu->vsstatus = sstatus;

    // 5. 设置特权级为S
    if(cpu->privilege != 1)
        cpu->privilege = 1;

    //6. 跳转到stvec指向的地址
    uint64_t stvec = cpu->vstvec;
    uint64_t base = stvec & ~0x3ULL;
    uint64_t mode = stvec & 0x3;

    if (mode == VECTORED && is_interrupt) { 
        cpu->pc = base + ((uint64_t)cause << 2);
    } else {
        cpu->pc = base;
    }

    if(log_enable){
        printf("[VS-Mode Trap]pc:0x%16lx\n", cpu->pc);
       
    }

}



void take_smode_trap(CPU_State *cpu, uint64_t cause, bool is_interrupt){

    //H extensions 保存发生陷阱前的 V 标志 spv
    uint64_t hstatus = cpu->csr[HSTATUS];
    hstatus = (hstatus & ~HSTATUS_SPV) | (cpu->v << 7);
    // gva HSTATUS_GVA

    // spvp
    if(cpu->v && cpu->privilege == 1)// VS
    {
        hstatus = (hstatus & ~HSTATUS_SPVP) | (1 << 8);
    }else
    {
        hstatus = hstatus & ~HSTATUS_SPVP;
    }
    cpu->v = false;//进入 HS 模式后清零

    
    //hinst

    // 1. 保存当前pc到sepc
    cpu->csr[CSR_SEPC] = cpu->pc;
    uint64_t scause = is_interrupt ? (1ULL << 63) | (cause & 0x7fffffff) 
                                   : (cause & 0x7fffffff);

    cpu->csr[CSR_SCAUSE] = scause;        

    uint64_t sstatus = 0;
    sstatus = cpu->v ? cpu->vsstatus:cpu->csr[CSR_SSTATUS];

    // 2.把当前sie保存到spie
    if (sstatus & SSTATUS_SIE) {
        sstatus |= SSTATUS_SPIE;
    } else {
        sstatus &= ~SSTATUS_SPIE;
    }
    // 3. 关闭中断（清SIE位）
    sstatus &= ~SSTATUS_SIE;

    // 4. 设置SPP位为当前特权级（0=U, 1=S）
    uint64_t spp = (cpu->privilege == 1) ? 1 : 0; // 1=S, 0=U
    sstatus = (sstatus & ~SSTATUS_SPP) | (spp << 8);
    
    cpu->csr[CSR_SSTATUS] = sstatus;


    cpu->csr[HSTATUS] = hstatus;


    // 5. 设置特权级为S
    if(cpu->privilege != 1)
        cpu->privilege = 1;

    //6. 跳转到stvec指向的地址
    uint64_t stvec = cpu->csr[CSR_STVEC];

    
    if(log_enable){
        printf("[S-Mode Trap] stvec:0x%16lx, scause:0x%16lx\n", stvec, scause);
    }
    uint64_t base = stvec & ~0x3ULL;
    uint64_t mode = stvec & 0x3;

    if(log_enable){
        printf("[S-Mode Trap]base :0x%16lx,mode:%d,is_interrupt:%d\n",base,mode,is_interrupt);
    }

    if (mode == VECTORED && is_interrupt) { 
        cpu->pc = base + ((uint64_t)cause << 2);
    } else {
        cpu->pc = base;
    }

    if(log_enable){
        printf("[S-Mode Trap]pc:0x%16lx\n", cpu->pc);
    }

}

void take_mmode_trap(CPU_State *cpu, uint64_t cause, bool is_interrupt){
      /* 1) 保存 mepc = 当前指令地址（spec: address of the ECALL/EBREAK instr）*/
    write_csr(cpu, CSR_MEPC, cpu->pc);
    if(log_enable){
        printf("[handle mmode trap]");
    }
    /* 2) mcause: 高位标志中断 bit31: 1:interrupt  0:exception ,   cause: 异常/中断的类型编号*/
    uint64_t mcause = is_interrupt ? (1ULL << 63) | (cause & 0x7fffffff) : (cause & 0x7fffffff);
    write_csr(cpu, CSR_MCAUSE, mcause);
    
    /* 3) mtval（如果适用）— 这里简单置 0，某些异常需要写具体值 */
    write_csr(cpu, CSR_MTVAL, 0);

    /* 4) 保存 mstatus: MPIE = MIE ; MIE = 0 ; MPP = 当前特权等级 */
    uint64_t mstatus = read_csr(cpu, CSR_MSTATUS);
    if (mstatus & MSTATUS_MIE) mstatus |= MSTATUS_MPIE; else mstatus &= ~MSTATUS_MPIE;
    mstatus &= ~MSTATUS_MIE;
    mstatus = (mstatus & ~MSTATUS_MPP_MASK) | ((uint64_t)(cpu->privilege & 3) << MSTATUS_MPP_SHIFT);
    mstatus = (mstatus & ~MSTATUS_MPV) | ((uint64_t)cpu->v << 39);

    write_csr(cpu, CSR_MSTATUS, mstatus);

    cpu->privilege = PRV_M;
    cpu->v = 0;

   
    uint64_t mtvec = read_csr(cpu, CSR_MTVEC);
    uint64_t base = mtvec & ~0x3ULL;
    uint64_t mode = mtvec & 0x3;// 0:direct 1:vectored

    if (mode == VECTORED && is_interrupt) { 
        cpu->pc = base + ((uint64_t)cause << 2);
    } else {
        cpu->pc = base;
    }
}


static bool check_real_ext_interrupt(CPU_State *cpu){

    int target_priv = -1;
    bool trigger = false;
    bool target_vs = false;
    uint64_t cause = 0;
    
    if(! (cpu->csr[CSR_MIE] & MIE_SEIE)) return false;

    if(! (cpu->csr[CSR_MIP] & MIP_SEIP)) return false;

    bool deleg_hs = !!(cpu->csr[CSR_MIDELEG] & MIDELEG_SEI);
    bool deleg_vs = !!(cpu->csr[HIDELEG] & HIDELEG_SEI);

    if(!deleg_hs){ 
        target_priv = 3;
    }else{
        target_priv = 1;
    }

    if(target_priv > cpu->privilege){
         trigger = true;
    }else if(target_priv == cpu->privilege){
        if(target_priv == 3){
            trigger = !!(cpu->csr[CSR_MSTATUS] & MSTATUS_MIE);
            cause = IRQ_M_EXT;
        }else if(target_priv == 1){
            if(!deleg_vs || !cpu->v){
                trigger = !!(cpu->csr[CSR_SSTATUS] & SSTATUS_SIE);
                cause = IRQ_S_EXT;
            }else{
                trigger = !!(cpu->vsstatus & SSTATUS_SIE);
                target_vs = true;
                cause = IRQ_S_EXT;
            }
        }

    }

    if(!trigger) return false;

    if(target_priv == 3){
        take_mmode_trap(cpu,cause,true);
        return true;
    }else{
        if(target_vs){
            take_vsmode_trap(cpu,cause,true);
            return true;
        }else{
            take_smode_trap(cpu,cause,true);
            return true;
        }
    }
    return false;
}

static bool check_virt_ext_interrupt(CPU_State *cpu){

    if(!(cpu->csr[HIE] & HIE_VSEIE)) return false;
    if(!(cpu->csr[HIP] & HIP_VSEIP)) return false;

    uint64_t deleg_vs = cpu->csr[HIDELEG] & HIDELEG_SEI;

    if(!deleg_vs || !cpu->v){
        if(!(cpu->csr[CSR_SSTATUS] & SSTATUS_SIE)) return false;
        take_smode_trap(cpu,IRQ_S_EXT,true);
        return true;
    }else{
        if(!(cpu->vsstatus & SSTATUS_SIE)) return false;
        take_vsmode_trap(cpu,IRQ_S_EXT,true);
        return true;
    }
    return false;
}

static bool check_real_timer_interrupt(CPU_State *cpu){

    int target_priv = -1;
    bool trigger = false;
    bool target_vs = false;
    uint64_t cause = 0;
    
    bool deleg_hs = !!(cpu->csr[CSR_MIDELEG] & MIDELEG_STI);
    bool deleg_vs = !!(cpu->csr[HIDELEG] & HIDELEG_STI);

    if(!deleg_hs){ 
        if(! (cpu->csr[CSR_MIE] & MIE_STIE)) return false;
        if(! (cpu->csr[CSR_MIP] & MIP_STIP)) return false;
        target_priv = 3;
       
    }else{
        if(! (cpu->csr[CSR_MIP] & MIP_STIP)) return false;
        target_priv = 1;
    }

    if(target_priv > cpu->privilege){
         trigger = true;
    }else if(target_priv == cpu->privilege){
        if(target_priv == 3){
            trigger = !!(cpu->csr[CSR_MSTATUS] & MSTATUS_MIE);
            cause = IRQ_M_TIMER;
        }else if(target_priv == 1){
            if(!deleg_vs || !cpu->v){
                trigger = !!(cpu->csr[CSR_SSTATUS] & SSTATUS_SIE);
                cause = IRQ_S_TIMER;
            }else{
                trigger = !!(cpu->vsstatus & SSTATUS_SIE);
                target_vs = true;
                cause = IRQ_S_TIMER;
            }
        }
    }

    if(!trigger) return false;

    if(target_priv == 3){
        take_mmode_trap(cpu,cause,true);
        return true;
    }else{
        if(target_vs){
            take_vsmode_trap(cpu,cause,true);
            return true;
        }else{
            take_smode_trap(cpu,cause,true);
            return true;
        }
    }
    return false;
}

static bool check_virt_timer_interrupt(CPU_State *cpu){
    if(!(cpu->csr[HIE] & HIE_VSTIE)) return false;
    if(!(cpu->csr[HIP] & HIP_VSTIP)) return false;

    uint64_t deleg_vs = cpu->csr[HIDELEG] & HIDELEG_STI;

    if(!deleg_vs || !cpu->v){
        if(!(cpu->csr[CSR_SSTATUS] & SSTATUS_SIE)) return false;
        take_smode_trap(cpu,IRQ_S_TIMER,true);
        return true;
    }else{
        if(!(cpu->vsstatus & SSTATUS_SIE)) return false;
        take_vsmode_trap(cpu,IRQ_S_TIMER,true);
        return true;
    }
}

static bool check_real_soft_interrupt(CPU_State *cpu){
    int target_priv = -1;
    bool trigger = false;
    bool target_vs = false;
    uint64_t cause = 0;
    
    bool deleg_hs = !!(cpu->csr[CSR_MIDELEG] & MIDELEG_SSI);
    bool deleg_vs = !!(cpu->csr[HIDELEG] & HIDELEG_SSI);

    if(!deleg_hs){ 
        if(! (cpu->csr[CSR_MIE] & MIE_SSIE)) return false;
        if(! (cpu->csr[CSR_MIP] & MIP_SSIP)) return false;
        target_priv = 3;
       
    }else{
        if(! (cpu->csr[CSR_MIP] & MIP_SSIP)) return false;
        target_priv = 1;
    }

    if(target_priv > cpu->privilege){
         trigger = true;
    }else if(target_priv == cpu->privilege){
        if(target_priv == 3){
            trigger = !!(cpu->csr[CSR_MSTATUS] & MSTATUS_MIE);
            cause = IRQ_M_SOFT;
        }else if(target_priv == 1){
            if(!deleg_vs || !cpu->v){
                trigger = !!(cpu->csr[CSR_SSTATUS] & SSTATUS_SIE);
                cause = IRQ_S_SOFT;
            }else{
                trigger = !!(cpu->vsstatus & SSTATUS_SIE);
                target_vs = true;
                cause = IRQ_S_SOFT;
            }
        }
    }

    if(!trigger) return;

    if(target_priv == 3){
        take_mmode_trap(cpu,cause,true);
        return true;
    }else{
        if(target_vs){
            take_vsmode_trap(cpu,cause,true);
            return true;
        }else{
            take_smode_trap(cpu,cause,true);
            return true;
        }
    }
}

static bool check_virt_soft_interrupt(CPU_State *cpu){
    if(!(cpu->csr[HIE] & HIE_VSSIE)) return false;
    if(!(cpu->csr[HIP] & HIP_VSSIP)) return false;

    uint64_t deleg_vs = cpu->csr[HIDELEG] & HIDELEG_SSI;

    if(!deleg_vs || !cpu->v){
        if(!(cpu->csr[CSR_SSTATUS] & SSTATUS_SIE)) return false;
        take_smode_trap(cpu,IRQ_S_SOFT,true);
        return true;
    }else{
        if(!(cpu->vsstatus & SSTATUS_SIE)) return false;
        take_vsmode_trap(cpu,IRQ_S_SOFT,true);
        return true;
    }
}

void check_and_handle_interrupts(CPU_State *cpu){

    if(cpu->privilege == 3){
        if(cpu->csr[CSR_MSTATUS] & MSTATUS_MIE){
            if((cpu->csr[CSR_MIP] & MIP_MEIP) && (cpu->csr[CSR_MIE] & MIE_MEIE)){
                take_mmode_trap(cpu,IRQ_M_EXT,true);
                return;
            }

             if((cpu->csr[CSR_MIP] & MIP_MSIP) && (cpu->csr[CSR_MIE] & MIE_MSIE)){
                take_mmode_trap(cpu,IRQ_M_SOFT,true);
                return;
            }

             if((cpu->csr[CSR_MIP] & MIP_MTIP) && (cpu->csr[CSR_MIE] & MIE_MTIE)){
                take_mmode_trap(cpu,IRQ_M_TIMER,true);
                return;
            }
        }
    }

    //hs vs check
    if(check_real_ext_interrupt(cpu)) return;
    if(check_real_soft_interrupt(cpu)) return;
    if(check_real_timer_interrupt(cpu)) return;
    if(check_virt_ext_interrupt(cpu)) return;
    if(check_virt_soft_interrupt(cpu)) return;
    if(check_virt_timer_interrupt(cpu)) return;
}
