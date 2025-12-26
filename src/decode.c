// src/decode.c
#include "decode.h"
#include "instructions.h"
#include "memory.h"
#include <stdio.h>
#include "cpu.h"
#include "trap.h"
#include "mmu.h"
// 指令解码表

instruction_handler_t opcode_table[255] = {0};
instruction_handler_t r_type_instruction[1024] = {NULL};
instruction_handler_t i_type_imm_instruction[8] = {0};
instruction_handler_t instruction_table[128] = {0};

instruction_handler_t system_table[0xFFF] = {NULL};
instruction_handler_t s_type_instr[8] = {NULL};
instruction_handler_t load_type_instr[8] ={NULL};
instruction_handler_t b_type_instr[8] = {NULL};
instruction_handler_t csr_instr[3] = {NULL};

instruction_handler_t task_table[32] = {NULL};

static void handle_i_imm_type(CPU_State* cpu,uint32_t instruction){
    uint8_t funct3 = (instruction >> 12) & 0x7;

    i_type_imm_instruction[funct3](cpu,instruction);

}
static void handle_r_type(CPU_State* cpu,uint32_t instruction){
    /*

    funct7 | rs2  | rs1  | funct3 | rd   | opcode
    0000000 | 01010 | 01111 | 011 | 01110 | 0110011
    
    */

    uint8_t funct7 = (instruction >> 25) & 0x7F;
    uint8_t funct3 = (instruction >> 12) & 0x7;
    uint32_t index = funct7 << 3 | funct3;
    r_type_instruction[index](cpu,instruction);
    

}

static void handle_system(CPU_State* cpu,uint32_t instruction){
    uint8_t funct3 = (instruction >> 12) & 0x7;
    uint8_t funct7 = (instruction >> 25) & 0x3F;
    
    if ((funct3 == 0)) {
        uint64_t imm12 = (instruction >> 20) & 0xfff;

        if (imm12 == 0) { /* ECALL */
            system_table[imm12](cpu,instruction);
        } else if (imm12 == 1) { /* EBREAK */
            system_table[imm12](cpu,instruction);
        }else if(imm12 == 0x302){// funct12 == 0x302
            system_table[0xA](cpu,instruction);
        }else if(imm12 == 0x102){
            //sret
        }else if(imm12 == 0x002){
            //uret
        }else if(imm12 == 0x105){
            //wfi wait for interrupt
            system_table[0x105](cpu,instruction);
        }
    }
    if(funct3 == 0 && funct7 == 0x09){
        system_table[0x09](cpu,instruction);
    }
    if(funct3 != 0){
        csr_instr[0x1](cpu,instruction);
    }
}
static void hande_b_type(CPU_State* cpu,uint32_t instruction){
    uint8_t funct3 = (instruction >> 12) & 0x7;
    //printf("b type funct3:0x%08x\n",funct3);
    //printf("handler = %p\n",b_type_instr[funct3]);
    b_type_instr[funct3](cpu,instruction);
}

void init_opcode_table(){
    opcode_table[0x03] = exec_load;
    opcode_table[0x13] = handle_i_imm_type;
    opcode_table[0x1B] = exec_iw;
    opcode_table[0x17] = exec_auipc;
    opcode_table[0x23] = exec_store;
    opcode_table[0x33] = handle_r_type;
    opcode_table[0x3B] = exec_3b;
    opcode_table[0x73] = handle_system;
    opcode_table[0x37] = exec_lui;
    opcode_table[0x6f] = exec_jal;
    opcode_table[0x67] = exec_jalr;
    opcode_table[0x63] = hande_b_type;
    opcode_table[0x00] = exec_c0;
    opcode_table[0x01] = exec_c1;
    opcode_table[0x02] = exec_c2;
    opcode_table[0x2F] = exec_amo;
    opcode_table[0x0F] = exec_fence;
    opcode_table[0x53] = exec_float;
}

static void handle_opcode(CPU_State* cpu,uint32_t instruction){
    uint8_t half = instruction & 0x3;
    uint8_t opcode = 0;
    if(half == 0x3){
        opcode = instruction & 0x7F;
    }else{
        opcode = half;
    }
    //printf("Decoding opcode: 0x%02x\n", opcode);
    if (opcode_table[opcode]) {
        opcode_table[opcode](cpu, instruction);
    } else {
        printf("Unknown instruction: 0x%08x at PC: 0x%08x\n", instruction, cpu->pc);
        cpu->running = false;
    }
    
}

void init_r_type_instruction(){

    r_type_instruction[0x00 | 0x0] = exec_add;
    r_type_instruction[0x00 | 0x6 ] = exec_or;
    r_type_instruction[0x00 | 0x7] = exec_and;
    r_type_instruction[0x20 << 3 | 0x0] = exec_sub;
    r_type_instruction[0x01 << 3 | 0x0] = exec_mul;
    r_type_instruction[0x00 | 0x3] = exec_sltu;
    r_type_instruction[0x00 | 0b100] = exec_xor;
    r_type_instruction[0x08 | 0b100] = exec_div;
    r_type_instruction[0x00 | 0b101] = exec_srl;
    r_type_instruction[0x08 | 0b111] = exec_remu;
    r_type_instruction[0x08 | 0b101] = exec_divu;
}

void init_i_type_imm_instruction(){

    i_type_imm_instruction[0] = exec_addi;
    i_type_imm_instruction[0x1] = exec_slli;
    i_type_imm_instruction[0b010] = exec_slti;
    i_type_imm_instruction[0b011] = exec_sltiu;
    i_type_imm_instruction[0b101] = exec_si;
    i_type_imm_instruction[0x6] = exec_ori;
    i_type_imm_instruction[0x7] = exec_andi;
}

void init_A_instruction(){
    
}

void init_B_instr(){
    b_type_instr[0x00] = exec_beq;
    b_type_instr[0x01] = exec_bne;
    b_type_instr[0b110] = exec_bltu; 
    b_type_instr[0b101] = exec_bge;
    b_type_instr[0b100] = exec_blt;
    b_type_instr[0b111] = exec_bgeu;
}

void init_system_instrcution(){
    system_table[0] = exec_ecall;
    system_table[1] = exec_ebreak;
    system_table[0x09] = exec_sfencevma;
    system_table[0xA] = exec_mret;
    system_table[0x105] = exec_wfi;
    csr_instr[0x1] = exec_csr;
    
}


void init_instruction_table() {
    printf("Initializing instruction table...\n");
    init_opcode_table();
    init_r_type_instruction();
    init_i_type_imm_instruction();
   
    init_B_instr();
    init_system_instrcution();

    printf("Instruction table initialized\n");
} 

uint32_t fetch_instruction(CPU_State* cpu, uint8_t* memory) {

    uint64_t pa = 0;
    uint64_t va = cpu->pc;
    pa = get_pa(cpu,va,ACC_FETCH);
    
    uint16_t instr = memory_read(cpu->mem,pa,2) & 0xFFFF;
    if((instr & 0x3) == 0x3){
        return memory_read(cpu->mem,pa,4);
    }else{
        return instr;
    }
}

void decode_and_execute(CPU_State* cpu, uint32_t instruction) {
    if (instruction == 0) {
       // printf("ERROR: Invalid instruction (0) at PC: 0x%08x\n", cpu->pc);
        cpu->running = false;
        return;
    }
    

    handle_opcode(cpu,instruction);
    /*
    uint8_t opcode = instruction & 0x7F;
    printf("Decoding opcode: 0x%02x\n", opcode);
    
    if (instruction_table[opcode]) {
        instruction_table[opcode](cpu, instruction);
    } else {
        printf("Unknown instruction: 0x%08x at PC: 0x%08x\n", instruction, cpu->pc);
        cpu->running = false;
    }*/
}