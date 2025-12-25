#ifndef TRAP_VECTOR_H
#define TRAP_VECTOR_H


#include <stdint.h>
#include "memory.h"
#include "cpu.h"

#define TRAP_HANDLER_ADDRESS 0x40000000

void init_syscall();
void trap_handler(CPU_State* cpu);
void trap_vectored_handler(CPU_State* cpu);
#endif