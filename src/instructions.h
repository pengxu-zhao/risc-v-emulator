// src/instructions.h
#ifndef INSTRUCTIONS_H
#define INSTRUCTIONS_H

#include <stdint.h>
#include "common.h"
#include "cpu.h"

#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))


// fcsr 标志位
#define FFLAG_NX 0x01
#define FFLAG_UF 0x02
#define FFLAG_OF 0x04
#define FFLAG_DZ 0x08
#define FFLAG_NV 0x10


// 舍入模式编码（与 RISC‑V frm 一致）
enum {
    RNE = 0,  // round to nearest, ties to even
    RTZ = 1,  // round towards zero
    RDN = 2,  // round down (towards -inf)
    RUP = 3,  // round up (towards +inf)
    RMM = 4   // round to nearest, ties to max magnitude
};

typedef struct {
    uint32_t sign : 1;
    uint32_t exp  : 8;
    uint32_t mant : 23;   // 隐含位不存
} float_bits_t;


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
void exec_sret(CPU_State *cpu,uint32_t instr);
void exec_sll(CPU_State *cpu,uint32_t instr);
void exec_slt(CPU_State *cpu,uint32_t instr);
void exec_mulhu(CPU_State *cpu,uint32_t instr);
void exec_sra(CPU_State *cpu,uint32_t instr);
void exec_mulh(CPU_State *cpu,uint32_t instr);
void exec_mulhsu(CPU_State *cpu,uint32_t instr);
void exec_rem(CPU_State *cpu,uint32_t instr);
void exec_flw(CPU_State *cpu,uint32_t instr);
void exec_43(CPU_State *cpu,uint32_t instr);
void exec_4f(CPU_State *cpu,uint32_t instr);
void exec_47(CPU_State *cpu,uint32_t instr);
void exec_4b(CPU_State *cpu,uint32_t instr);
void exec_27(CPU_State *cpu,uint32_t instr);
void exec_hfence(CPU_State* cpu,uint32_t instr);
#endif // INSTRUCTIONS_H