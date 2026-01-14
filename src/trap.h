#ifndef TRAP_H
#define TRAP_H

#include "common.h"
#include <stdbool.h>
#include "cpu.h"


void take_trap(CPU_State *cpu, uint64_t cause, bool is_interrupt);
void do_mret(CPU_State *cpu);
void check_and_handle_interrupts(CPU_State *cpu);

#endif