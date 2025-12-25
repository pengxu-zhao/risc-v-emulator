#ifndef TICK_H
#define TICK_H

#include "cpu.h"
#include "trap.h"

typedef struct{
    uint32_t gpr[NUM_GPR];
    uint32_t pc;
}taskContext;

void clint_tick(CPU_State *cpu, uint64_t cycles);
void trap_handler2(CPU_State *cpu, uint32_t cause, bool is_interrupt);

void switch_task(CPU_State *cpu);
void check_pending_and_take(CPU_State *cpu);
#endif