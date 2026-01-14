// src/decode.h
#ifndef DECODE_H
#define DECODE_H

#include <stdint.h>
#include "common.h"

// 指令执行函数类型
typedef void (*instruction_handler_t)(CPU_State* cpu, uint32_t instruction);

// 函数声明
void init_instruction_table(void);
uint32_t fetch_instruction(CPU_State* cpu, uint8_t* memory);
void decode_and_execute(CPU_State* cpu, uint32_t instruction);

#endif // DECODE_H