// src/instructions.h
#ifndef INSTRUCTIONS_H
#define INSTRUCTIONS_H

#include <stdint.h>
#include "common.h"
#include "cpu.h"

// 基础整数指令 (I扩展)
void exec_lui(CPU_State* cpu, uint32_t instruction);
void exec_auipc(CPU_State* cpu, uint32_t instruction);
void exec_jal(CPU_State* cpu, uint32_t instruction);
void exec_jalr(CPU_State* cpu, uint32_t instruction);
void exec_imm(CPU_State* cpu, uint32_t instruction);
void exec_op(CPU_State* cpu, uint32_t instruction);
void exec_ecall(CPU_State* cpu, uint32_t instruction);
void exec_addi(CPU_State* cpu,uint32_t instruction);
void exec_sub(CPU_State* cpu,uint32_t instruction);
void exec_add(CPU_State* cpu,uint32_t instruction);
void exec_mul(CPU_State* cpu,uint32_t instruction);

void taskA(CPU_State* cpu,uint32_t instruction);
void taskB(CPU_State* cpu,uint32_t instruction);
void exec_sfencevma(CPU_State* cpu,uint32_t instruction);

void exec_store(CPU_State* cpu,uint32_t instructions);
void exec_ebreak(CPU_State* cpu,uint32_t instructions);
void exec_load(CPU_State *cpu,uint32_t instruction);
void exec_sltu(CPU_State* cpu,uint32_t instruction);
void exec_bltu(CPU_State* cpu,uint32_t instr);
void exec_slli(CPU_State* cpu,uint32_t instr);
void exec_andi(CPU_State* cpu,uint32_t instr);
void exec_bne(CPU_State* cpu,uint32_t instr);
void exec_beq(CPU_State* cpu,uint32_t instr);
void exec_csr(CPU_State* cpu,uint32_t instr);
void exec_mret(CPU_State* cpu,uint32_t instr);
void exec_si(CPU_State* cpu,uint32_t instr);
void exec_ori(CPU_State* cpu,uint32_t instr);
void exec_or(CPU_State* cpu,uint32_t instr);
void exec_and(CPU_State* cpu,uint32_t instr);

void exec_c0(CPU_State* cpu,uint16_t instr);
void exec_c1(CPU_State* cpu,uint16_t instr);
void exec_c2(CPU_State* cpu,uint16_t instr);
void exec_bge(CPU_State* cpu,uint32_t instr);
void exec_amo(CPU_State* cpu,uint32_t instr);
void exec_blt(CPU_State* cpu,uint32_t instr);
void exec_slti(CPU_State* cpu,uint32_t instr);
void exec_sltiu(CPU_State* cpu,uint32_t instr);
void exec_xor(CPU_State* cpu,uint32_t instr);
void exec_iw(CPU_State* cpu,uint32_t instr);
void exec_fence(CPU_State* cpu,uint32_t instr);
void exec_float(CPU_State* cpu,uint32_t instr);
void exec_wfi(CPU_State* cpu,uint32_t instr);
void exec_div(CPU_State* cpu,uint32_t instr);
void exec_bgeu(CPU_State* cpu,uint32_t instr);
void exec_3b(CPU_State* cpu,uint32_t instr);
void exec_srl(CPU_State* cpu,uint32_t instr);
void exec_remu(CPU_State *cpu,uint32_t instr);
void exec_divu(CPU_State *cpu,uint32_t instr);
void exec_xori(CPU_State* cpu,uint32_t instr);
#endif // INSTRUCTIONS_H