// 初始化中断向量表
#include "trap_vector.h"
#include "mmu.h"
#include "uart.h"

extern uint8_t* memory;
typedef int32_t (*SyscallHandler)(CPU_State* cpu);
SyscallHandler syscall_handlers[0xFFFF] = {0};

int32_t write_mem(CPU_State* cpu){
    phys_write_u32(cpu,cpu->gpr[10],cpu->gpr[11]);
    return 0;
}

void init_syscall(){
    syscall_handlers[0x1001] = write_mem;
}

static void mmio_trap_handler(CPU_State* cpu,uint8_t cause){
   
    UARTDevice *uart = cpu->uart_table[UART_IRQ_NUM];

    pthread_mutex_lock(&uart->lock);
    if (uart->irq_status & UART_IRQ_RX_PENDING) {
        uart->irq_status &= ~UART_IRQ_RX_PENDING; // 清除中断状态
    }

    // 2. 检查发送中断
    if (uart->irq_status & UART_IRQ_TX_PENDING) {
        uart->irq_status &= ~UART_IRQ_TX_PENDING;
        // 如果有发送队列，可触发下一个字节发送
    }
    pthread_mutex_unlock(&uart->lock);
    // 3. 通知 CPU：该中断已完成处理
    cpu->csr[CSR_MIP] &= ~(1U << UART_IRQ_NUM);
}

//mcause: 高位标志中断 bit31: 1:interrupt  0:exception
//mtvec & 0x3;// 0:direct 1:vectored
void trap_handler(CPU_State* cpu){

    uint64_t mcause = cpu->csr[CSR_MCAUSE] & 0x7fffffff;//去掉最高位 (中断标志)
    uint64_t syscall = cpu->gpr[17]; // a7
    uint8_t cause_flag = (cpu->csr[CSR_MCAUSE] >> 31) & 0x1;
    uint8_t ret = 0;

    if(!cause_flag){
        if(syscall < 0xFFFF && syscall_handlers[syscall]){
            ret = syscall_handlers[syscall](cpu);
        }else{
            printf("unknown syscall:0x%08x\n",syscall);
            ret = -1;
        }
    }else{
        switch (mcause)
        {
            case UART_IRQ_NUM:
            {
                mmio_trap_handler(cpu,mcause);
                break;
            }/*
            case CLINT_IRQ_NUM: {
                // 比如：定时器中断
                // timer_handle_interrupt(cpu);
                break;
            */
            default:
                break;
        }
    }

   cpu->pc = read_csr(cpu,CSR_MEPC) + 4;
}

void trap_vectored_handler(CPU_State* cpu){

}



