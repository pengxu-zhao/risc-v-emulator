// src/instructions.c
#include "instructions.h"
#include "memory.h"
#include <stdio.h>
#include "trap.h"
#include "trap_vector.h"
#include "mmu.h"
#include "bus.h"


extern uint8_t* memory;
extern int log_enable;

static inline print_all_gpr(CPU_State* cpu){
    printf("x0~x31 value=========================\n");
    for(uint8_t i = 0; i < 32; i++){
        printf("x[%d]:0x%016lx\n",i,cpu->gpr[i]);
    }
}


void exec_c0(CPU_State* cpu,uint16_t instr){
    /*
    | 15 14 13 | 12   | 11   |10 9 8 7| 6 5    |4 3 2| 1 0 |
    | funct3   | imm5 | imm4 | imm9:6 | imm3:2 | rd' | opcode |
    | 000      | imm5 | imm4 | imm9:6 | imm3:2 | rd' | 00 |
    */
    uint8_t funct3 = (instr >> 13) & 0x7;
    

    switch (funct3)
    {
    case 0b000: // c.addi4spn
        {
            uint8_t rd = ((instr >> 2) & 0x7) + 8;    
            uint32_t imm10 = 
                ((instr >> 5 ) & 0x1) << 3|
                ((instr >> 6) & 0x1) << 2|
                ((instr >> 7) & 0xF) << 6|
                ((instr >> 11) & 0x3) << 4;

            if(imm10 != 0){
                cpu->gpr[rd] = cpu->gpr[2] + imm10;
            }               
            cpu->pc += 2;
            if(log_enable){
            printf("[c.addi4spn] sp:0x%16lx + imm:0x%16lx,x[%d]:0x%16lx\n",
                        cpu->gpr[2],imm10,rd,cpu->gpr[rd]);
            }
            break;
        }
    case 0b010://c.lw
        {
            uint8_t rd = ((instr >> 2) & 0x7) + 8;
            uint8_t rs1 = ((instr >> 7) & 0x7) + 8;
            uint8_t imm8 = ((instr >> 10) & 0x7) << 3 |
                            ((instr >> 6) & 0x1) << 2 |
                            ((instr >> 5) & 0x1) << 6;
        
            uint64_t addr = cpu->gpr[rs1] + imm8;
            uint64_t val = 0;
            uint64_t pa = get_pa(cpu,addr,ACC_LOAD);
            if(rd != 0){
                val = bus_read(&cpu->bus,pa,4);
                cpu->gpr[rd] = val;
            }
            if(log_enable){
                printf("[c.lw] x[%d]:0x%08lx, pa:0x%08lx\n",rd,val,pa);
            }
            cpu->pc += 2;
            break;
        }
    case 0b110://c.sw
    {
        uint32_t rs1 = ((instr >> 7) & 0x7) + 8;
        uint32_t rs2 = ((instr >> 2) & 0x7) + 8;

        uint64_t imm = ((instr >> 6) & 0x1) << 2  
                | ((instr >> 10) & 0x7) << 3 
                | ((instr >> 5) & 0x1) << 6; 
        uint64_t addr = cpu->gpr[rs1] + imm;      
        uint64_t pa = get_pa(cpu,addr,ACC_STORE);
        bus_write(&cpu->bus,pa,cpu->gpr[rs2],4);
        if(log_enable)
            printf("[c.sw after] imm:0x%08lx,pa:0x%08x,rs2 val:%d 0x%08x\n",imm,pa,rs2,cpu->gpr[rs2]);
        cpu->pc += 2;
        break;
    }
    case 0b111://c.sd
    {
        uint32_t rs1 = ((instr >> 7) & 0x7) + 8;
        uint32_t rs2 = ((instr >> 2) & 0x7) + 8;

        uint64_t imm =  
                 ((instr >> 10) & 0x7) << 3 
                | ((instr >> 5) & 0x3) << 6; 
        uint64_t addr = cpu->gpr[rs1] + imm;
        uint64_t pa = get_pa(cpu,addr,ACC_STORE);
      
        bus_write(&cpu->bus,pa,cpu->gpr[rs2],8);
        cpu->pc += 2;
        if(log_enable){
        printf("[c.sd] x[%d]:0x%16lx,pa:0x%16lx,x[%d]:0x%16lx\n",
            rs1,cpu->gpr[rs1],pa,rs2,cpu->gpr[rs2]);
        }
        break;
    }
    case 0b011: //c.ld
    {
        uint8_t rs1 = ((instr >> 7) & 0x7) + 8;
        uint8_t rd = ((instr >> 2) & 0x7) + 8;

        uint32_t imm8 = ((instr >> 10) & 0x7) << 3 |
                        ((instr >> 5) & 0x3) << 6;
        uint64_t imm = (uint64_t)imm8;
        
        uint64_t val = 0;
        uint64_t vaddr = cpu->gpr[rs1] + imm;
        uint64_t pa = get_pa(cpu,vaddr,ACC_LOAD);
        
        val = bus_read(&cpu->bus,pa,8);
        
        cpu->gpr[rd] = val;
        cpu->pc += 2;
        if(log_enable){
            printf("[c.ld load 64bits] x[%d]:0x%16lx = load from pa:0x%16lx\n",
                rd,cpu->gpr[rd],pa);
        }
        break;
    }
    
    default:
        break;
    }

}


void exec_c1(CPU_State* cpu,uint16_t instr){
    /*
        15      13  12    11     7 6        2    1 0
        | funct3 | imm[5]| rd/rs1 | imm[4:0] | opcode |
    */

    uint8_t funct3 = (instr >> 13) & 0x7;

    switch (funct3)
    {
    case 0b000: // c.addi   c.nop
        {
            uint8_t rd = (instr >> 7) & 0x1F;        // rd/rs1
            uint32_t imm6 = ((instr >> 2) & 0x1F) |    // imm[4:0]
                 ((instr >> 12) & 0x1) << 5;    // imm[5]
            // 符号扩展 6-bit -> int64
            int64_t imm = (int64_t)(((int32_t)imm6 << 26) >> 26);

            if (rd != 0) {
                cpu->gpr[rd] += imm;
            }

            if(log_enable){
            printf("[c.addi] x[%d]:0x%16lx,imm:0x%16lx\n",rd,cpu->gpr[rd],imm);
            }
               
            cpu->pc += 2;  // 压缩指令 PC +2   
        break;
        }
    case 0b001: //c.addiw

        uint8_t rd = (instr >> 7) & 0x1F;

        if(rd != 0){ //c.addiw
            uint32_t imm6 = ((instr >> 12) & 0x1) << 5 |
                            ((instr >> 2) & 0x1F);
            int32_t imm = ((int32_t)imm6 << 26) >>26;
            if(log_enable){
            printf("[before c.addiw] x[%d]:0x%16lx,imm:0x%08x\n",rd,cpu->gpr[rd],imm);
            }
            int64_t result = (int64_t)((int64_t)cpu->gpr[rd] + imm);
            cpu->gpr[rd] = result;
            cpu->pc += 2;
            if(log_enable){
            printf("[c.addiw] x[%d]:0x%16lx,imm:0x%08x\n",rd,cpu->gpr[rd],imm);
            }
        }
        else //c.jal
        {
            uint32_t imm12 = ((instr >> 12 ) & 0x1) << 11 |
                            ((instr >> 11) & 0x1) << 4|
                            ((instr >> 9) & 0x3) << 8 |
                            ((instr >> 8) & 0x1) << 10|
                            ((instr >> 7) & 0x1) << 6 |
                            ((instr >> 6) & 0x1) << 7 |
                            ((instr >> 3) & 0x7) << 1 |
                            ((instr >> 2) & 0x1) << 5;
            int64_t imm = (int32_t)(imm12 << 20) >> 20;
            cpu->gpr[1] = cpu->pc + 2;
            cpu->pc += imm;
        }
        break;
    case 0b010: //c.li
    {
        uint8_t rd = (instr >> 7) & 0x1F;
        uint32_t imm6 = ((instr >> 12) & 0x1) << 5 |
                        ((instr >> 2) & 0x1F);
        int64_t imm = (int64_t)(((int32_t)imm6 << 26) >> 26);
        
        if(rd != 0){
            cpu->gpr[rd] = imm;
        }

        if(log_enable){
        printf("[c.li] x[%d]:0x%16lx = imm:0x%16lx\n",rd,cpu->gpr[rd],imm);
        }
        cpu->pc += 2;
        break;
    }
    case 0b011: // 
    {
            uint8_t rd = (instr >> 7) & 0x1F;
 
            if(rd != 2){ // c.lui
                uint32_t imm18 = ( ((instr >> 2) & 0x1F) << 12 ) | 
                            (((instr >> 12) & 0x1) << 17);
                int64_t imm = 0;
                imm = (int64_t)(((int32_t)imm18 << 14) >> 14);
                if(rd != 0 && rd != 0x2)
                    cpu->gpr[rd] = imm;
                if(log_enable){
                printf("[c.lui] x[%d]:0x%016lx\n",rd,cpu->gpr[rd]);
                }
                cpu->pc += 2;
            }else{ // c.addi16sp
                uint32_t imm6 = ((instr >> 2) & 0x1) << 5 |
                                ((instr >> 3) & 0x3) << 7 |
                                ((instr >> 5) & 0x1) << 6 |
                                ((instr >> 6) & 0x1) << 4 |
                                ((instr >> 12) & 0x1) << 9;

                int64_t imm = (int64_t)((int32_t)(imm6 << 22) >> 22);
                cpu->gpr[rd] += imm; 
                cpu->pc += 2;
                if(log_enable){
                    printf("[c.addi16sp] x[rd:%d]:0x%08lx,imm:0x%16lx\n",
                        rd,cpu->gpr[rd],imm);
                }

            }
            break;
    }
    case 0b100: //
    {
        uint8_t rd = ((instr >> 7) & 0x7) + 8;
        uint8_t funct2_56 = (instr >> 5) & 0x3;
        uint8_t funct2_10_11 =  (instr >> 10) & 0x3;
        uint8_t rs2 = ((instr >> 2) & 0x7) + 8;

        if(funct2_10_11 == 0b00){ //c.srli c.srli64
            uint8_t shamt = (instr >> 2) & 0x1F |
                            ((instr >> 12) & 0x1) << 5;
            cpu->gpr[rd] >>= shamt;
            cpu->pc += 2;
            if(log_enable){
            printf("[c.srli] x[%d]:0x%16lx\n",rd,cpu->gpr[rd]);
            }
            
        }else if(funct2_10_11 == 0b01){//c.srai c.srai64
            uint8_t shamt = (instr >> 2) & 0x1F | 
                            (((instr >> 12) & 0x1) << 5);
             if(log_enable){
                printf("[before c.srai] x[%d]:0x%08lx,shamt:%d\n",rd,cpu->gpr[rd],shamt);
            }


            cpu->gpr[rd] = (int64_t)cpu->gpr[rd] >> shamt;
            if(log_enable){
                printf("[after c.srai] x[%d]:0x%08lx,shamt:%d\n",rd,cpu->gpr[rd],shamt);

            }

            cpu->pc += 2;

        }else if(funct2_10_11 == 0b10){  // c.andi
            uint32_t imm6 = (instr >> 2) & 0x1F |
                            ((instr >> 12) & 0x1) << 5;
            int32_t imm = (int32_t)(imm6 << 26) >> 26;
            cpu->gpr[rd] &= imm;

            if(log_enable){
                printf("x[%d]:0x%08lx,imm:0x%08lx\n",rd,cpu->gpr[rd],imm);
            }

            cpu->pc += 2;

        }else if(funct2_10_11 == 0b11){
            if(funct2_56 == 0b11){ // c.and
                cpu->gpr[rd] &= cpu->gpr[rs2];
                cpu->pc += 2; 
                if(log_enable){
                printf("[c.and] x[%d]:0x%16lx,x[%d]:0x%16lx\n",rd,cpu->gpr[rd],
                rs2,cpu->gpr[rs2]);
                }
            }else if(funct2_56 == 0b10){ //c.or
                cpu->gpr[rd] |= cpu->gpr[rs2];
                if(log_enable){
                    printf("[c.or] x[%d]:0x%16lx |= x[%d]:0x%16lx\n",
                        rd,cpu->gpr[rd],rs2,cpu->gpr[rs2]);
                }

                cpu->pc += 2;
            }else if(funct2_56 == 0b00){ //c.sub
                cpu->gpr[rd] -= cpu->gpr[rs2];
                cpu->pc += 2;
            }else if(funct2_56 == 0b01){ //c.addw
                int32_t sum = (int32_t)cpu->gpr[rd] + (int32_t)cpu->gpr[rs2];
                cpu->gpr[rd] = (int32_t)sum;
                cpu->pc += 2;

            }
        }
        break;
    }
    case 0b101://c.j
        {
            uint32_t imm11 = ((instr >> 2) & 0x1) << 5 |
                        ((instr >> 3) & 0x7) << 1 |
                        ((instr >> 6) & 0x1) << 7 |
                        ((instr >> 7) & 0x1) << 6 |
                        ((instr >> 8) & 0x1) << 10|
                        ((instr >> 9) & 0x3) << 8 |
                        ((instr >> 11) & 0x1) << 4 |
                        ((instr >> 12) & 0x1) << 11; 
            int64_t imm = (int64_t)(((int32_t)imm11 << 20) >> 20);
            cpu->pc += imm;
            if(log_enable){
            printf("[c.j] imm:0x%16lx\n");
            }
            break;
        }
    case 0b111://c.bnez  not equal ,jump to (pc+imm)
    {
        uint32_t imm8 = ((instr >> 3) & 0x3) << 1|
                        ((instr >> 10) & 0x3) << 3|
                        ((instr >> 2) & 0x1) << 5 |
                        ((instr >> 5) & 0x3) << 6 |
                        ((instr >> 12) & 0x1) << 8 ;
        int64_t imm = (int64_t)(((int32_t)imm8 << 23) >> 23);
        uint8_t rs1 = ((instr >> 7) & 0x7) + 8;
        //printf("-----a5:0x%08x\n",cpu->gpr[15]);

        if(cpu->gpr[rs1] != 0){
            cpu->pc += imm;
        }else{
            cpu->pc += 2;
        }
        if(log_enable){
        printf("[c.bnez]x[%d]:0x%16lx,imm:0x%16lx\n",rs1,cpu->gpr[rs1],imm);
        }

        break;
    }
    case 0b110:// c.beqz 
    {
        uint32_t imm8 = ((instr >> 3) & 0x3) << 1|
                        ((instr >> 10) & 0x3) << 3|
                        ((instr >> 2) & 0x1) << 5|
                        ((instr >>5) & 0x3) << 6|
                        ((instr >> 12) & 0x1) << 8;
        int64_t imm = (int64_t) (((int32_t)imm8 << 23) >> 23);
        uint8_t rs1 = ((instr >> 7) & 0x7) + 8;
        if(cpu->gpr[rs1] == 0){
            cpu->pc += imm;
        }else{
            cpu->pc += 2;
        }
        if(log_enable){
        printf("[c.beqz]x[%d]:0x%16lx, imm:0x%16lx\n",rs1,cpu->gpr[rs1],imm);
        }

        break;
    }
    default:
        break;
    }

}

void exec_c2(CPU_State* cpu,uint16_t instr){

    uint8_t funct3 = (instr >> 13) & 0x7; 
    uint8_t rd = (instr >> 7 ) & 0x1F;
    uint32_t rs2 = ((instr >> 2) & 0x1F);
    uint32_t rs1 = ((instr >> 7) & 0x1F);

    switch (funct3)
    {
    case 0b000://c.slli
    {
        
        uint8_t shamt = (instr >> 2) & 0x1F | 
                        ((instr >> 12) & 0x1) << 5;
        if(rd != 0){
            cpu->gpr[rd] = cpu->gpr[rd] << shamt;
        }
        cpu->pc += 2;
        if(log_enable){
        printf("[c.slli] shamt:0x%08x, x[%d]:0x%16lx\n",shamt,rd,cpu->gpr[rd]);
        }


        break;
    }

    case 0b010://c.lwsp
    {
        
        uint32_t imm = ((instr >> 4) & 0x7) << 2 |
                        ((instr >> 12) & 0x1) << 5 |
                        ((instr >> 2) & 0x3) << 6;
        uint64_t addr = cpu->gpr[2] + imm;
        uint64_t pa = get_pa(cpu,addr,ACC_STORE);
        if(rd != 0){
            cpu->gpr[rd] = bus_read(&cpu->bus,pa,4);
        }
        if(log_enable){
            printf("[c.lwsp]imm:0x%08x,pa:0x%08x,ra:0x%08x\n",imm,pa,cpu->gpr[rd]);
        }
        cpu->pc += 2;
        break;
    }
    case 0b011://c.ldsp
    {
        uint32_t imm6 = ((instr >> 2) & 0x7) << 6 |
                        ((instr >> 5) & 0x3) << 3 |
                        ((instr >> 12) & 0x1) << 5;
        uint64_t imm = (uint64_t)(uint32_t)imm6;
        
        uint64_t vaddr = cpu->gpr[2] + imm;
        int64_t val = 0;

        uint64_t pa = get_pa(cpu,vaddr,ACC_LOAD);
     
        val = bus_read(&cpu->bus,pa,8);
        cpu->gpr[rd] = val;
        cpu->pc += 2;
        if(log_enable){
            printf("[c.ldsp] x[%d]:0x%16lx,vaddr:0x%16lx,pa:0x%08lx\n",rd,cpu->gpr[rd],vaddr,pa);
        }
        break;
    }

    case 0b110://c.swsp
    {
       
        uint32_t imm = ((instr >> 7) & 0x3) << 6 
                | ((instr >> 9) & 0xF) << 2 ;
           
        uint64_t addr = cpu->gpr[0x2] + imm;//x2 + imm

        uint64_t pa = get_pa(cpu,addr,ACC_STORE);

        bus_write(&cpu->bus,pa,cpu->gpr[rs2],4);
        if(log_enable){
            printf("c.swsp pa : 0x%08x ,rs2 val:%d 0x%08x\n",pa,rs2,cpu->gpr[rs2]);
        }
        cpu->pc += 2;
        break;
    }
    case 0b100:
    {
        uint8_t instr12 = (instr >> 12) & 0x1;
        if(rs2 == 0){ 
            if(instr12 == 0){ //c.jr
                if(rs1 != 0){
                    //printf("c.jr rs1:0x%08x\n",cpu->gpr[rs1]);
                    cpu->pc = cpu->gpr[rs1];
                }
                if(log_enable){
                printf("[c.jr c.ret] pc = x[%d]:0x%16lx\n",rs1,cpu->gpr[rs1]);
                }
            }else{ //c.jalr   
                cpu->gpr[0x1] = cpu->pc+2;
                cpu->pc = cpu->gpr[rs1];
                if(log_enable){
                printf("[c.jarl or c.ret] x[0x1]:0x%16lx,pc = x[%d]:0x%16lx\n",
                        cpu->gpr[0x1],rs1,cpu->gpr[rs1]);
                } 

            }
        }else if(rs1 != 0){
            if(instr12 == 0){ //c.mv
                cpu->gpr[rs1] = cpu->gpr[rs2];
                cpu->pc += 2;

            if(log_enable){
            printf("[c.mv] x[%d]:0x%16lx,x[%d]:0x%16lx\n",rs1,cpu->gpr[rs1],
                    rs2,cpu->gpr[rs2]);
            }
            }else{ //c.add
                if(log_enable){
                printf("[before c.add] x[%d]:0x%16lx,x[%d]:0x%16lx,x[%d]:0x%16lx\n",
                rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2]);
                }
                cpu->gpr[rd] = cpu->gpr[rs1] + cpu->gpr[rs2];
                cpu->pc += 2;
            if(log_enable){
            printf("[c.add] x[%d]:0x%16lx,x[%d]:0x%16lx,x[%d]:0x%16lx\n",
                rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2]);
            }
            }
        }
        
        break;
    }
    case 0b111: //c.sdsp
    {
        uint32_t imm6 = ((instr >> 7) & 0x7) << 6 |
                        ((instr >> 10) & 0x7) << 3;
        uint64_t imm = (uint64_t)imm6;
        uint64_t vaddr = cpu->gpr[2] + imm;

        int64_t val = 0;

        uint64_t pa = get_pa(cpu,vaddr,ACC_STORE);

        bus_write(&cpu->bus,pa,cpu->gpr[rs2],8);
        
        cpu->pc += 2;
        if(log_enable){
        printf("[c.sdsp] addr:0x%16lx,pa:0x%08lx,x[%d]:0x%16lx\n",vaddr,pa,rs2,cpu->gpr[rs2]);
        }

        
    }
    default:
        break;
    }
}



/*
类型	指令字段位数	实际偏移位数
----------------------------------
I 型	12	          12（符号扩展到 32 位）
B 型	12（拆分）	    13（<<1 字节对齐）
J 型	20	          21（<<1 字节对齐）

B 型和 J 型特殊点在于 偏移是字节地址，最低位总是 0，所以逻辑上多了一位

| 指令类型    | 位域说明                                   | 立即数位置                     | 立即数意义                  |                  |                    |             |
| ---------- | ---------------------------------------- | ---------------------------- | ---------------------- | ---------------- | ------------------ | ----------- |
| **I-type** | imm[11:0] rd rs1 funct3 opcode           | 低 12 位                      | 直接作为加数或逻辑操作数（低位）       |                  |                    |             |
| **S-type** | imm[11:5] rs2 rs1 funct3 imm[4:0] opcode | 低 12 位（拆成两段）            | 存储地址偏移，低位为页内偏移         |                  |                    |             |
| **B-type** | imm[12                                   | 10:5] rs2 rs1 funct3 imm[4:1 | 11] opcode             | 高位 + 低位组合        | 分支偏移，最终左移 1 才是字节偏移 |             |
| **U-type** | imm[31:12] rd opcode                     | 高 20 位                      | 左移 12 位后与 PC 相加（高位立即数） |                  |                    |             |
| **J-type** | imm[20                                   | 10:1                         | 11                     | 19:12] rd opcode | 高位 + 分散            | 跳转偏移，左移 1 位 |


*/

void exec_addi(CPU_State* cpu,uint32_t instruction){
    uint8_t rd = (instruction >> 7) & 0x1F;
    uint8_t rs1 = (instruction >> 15) & 0x1F;
    uint32_t imm12 = (instruction >> 20) & 0xFFF ;
    int64_t imm = (int64_t) (((int32_t)imm12 << 20) >> 20 );

    if(rd != 0){
        cpu->gpr[rd] = cpu->gpr[rs1] + imm;
    }
    if(log_enable){
    printf("[addi] x[%d]:0x%16lx = x[%d]:0x%16lx + imm:0x%16lx\n",
            rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],imm);
    }
    
    cpu->pc += 4;
   
}

// LUI指令 - 
void exec_lui(CPU_State* cpu, uint32_t instruction) {

    /*
        imm[31:12] | rd | 0110111
    
    */
    uint8_t rd = (instruction >> 7) & 0x1F;
    uint32_t imm20 = (instruction  & 0xFFFFF000);
    int32_t imm = (int32_t)imm20;

    if (rd != 0) {
        cpu->gpr[rd] = imm;
    }
    if(log_enable){
    printf("[lui] x[%d] = imm:0x%16lx\n",rd,cpu->gpr[rd]);
    }

    cpu->pc += 4;
}

// AUIPC指令 - 
void exec_auipc(CPU_State* cpu, uint32_t instruction) {
    /*
    imm[31:12]          rd        opcode
    [31............12][11..7] [6.....0]
    */
    uint8_t rd = (instruction >> 7) & 0x1F;
    uint32_t imm20 = (instruction & 0xFFFFF000);
          
    int32_t imm = (int32_t)imm20;
    if (rd != 0) {
        cpu->gpr[rd] = cpu->pc + imm;
    }
    
    if(log_enable){
    printf("[auipc] x[%d]:0x%16lx,imm:0x%08x\n",rd,cpu->gpr[rd],imm);

    }

    cpu->pc += 4;
    
}

// JAL指令 - 
void exec_jal(CPU_State* cpu, uint32_t instruction) {
    /*
    imm[20]  imm[10:1]  imm[11]  imm[19:12]   rd       opcode
    31       30..21      20       19..12    11..7     6..0

    offset[20|10:1|11|19:12]   （再 <<1，因为最低位总是0）
    */
    uint8_t rd = (instruction >> 7) & 0x1F;
    uint32_t imm20 = ((instruction >> 31) & 0x1) << 20 
        | ((instruction >> 12) & 0xFF) << 12
        | ((instruction >> 20) & 0x1) << 11
        | ((instruction >> 21) & 0x3FF) << 1;
    
    int64_t imm = (int64_t)(((int32_t)imm20 << 12) >> 12);

    if (rd != 0) {
        cpu->gpr[rd] = cpu->pc + 4;
    }

    if(log_enable){
    printf("[jal] x[%d]:0x%16lx\n",rd,cpu->pc+4);

    }
    cpu->pc += imm;
}

void exec_jalr(CPU_State* cpu, uint32_t instruction){
    /*
         31            20 19   15 14  12 11    7 6      0
        +---------------+-------+------+-------+---------+
        |   imm[11:0]   |  rs1  |000   |  rd   | 1100111 |
        +---------------+-------+------+-------+---------+
        12 bits        5 bits 3bits   5bits    7bits

    */
    uint8_t rs1 = (instruction >> 15) & 0x1F;
    uint8_t rd = (instruction >> 7) & 0x1F;
    uint32_t imm12 = (instruction >> 20) & 0xFFF;

    int64_t imm = (int64_t)(((int32_t)imm12 << 20) >> 20);
    

    uint64_t addr = (cpu->gpr[rs1] + imm) & ~1;

    if(rd != 0){
        cpu->gpr[rd] = cpu->pc + 4;
    } 

    if(log_enable){
    printf("[jalr/jr] x[%d]:0x%16lx,pa:0x%16lx\n",rd,cpu->gpr[rd],addr);
    }

    cpu->pc = addr;
    //c.ret = jalr x0 ,0(ra)
}

// ADD指令 - 
void exec_add(CPU_State* cpu, uint32_t instruction) {
    uint8_t rd = (instruction >> 7) & 0x1F;
    uint8_t rs1 = (instruction >> 15) & 0x1F;
    uint8_t rs2 = (instruction >> 20) & 0x1F;

    if (rd != 0) {
        cpu->gpr[rd] = (uint64_t)(cpu->gpr[rs1] + cpu->gpr[rs2]);
    }

    if(log_enable){
        printf("[add] x[%d]:0x%16lx = x[%d]:0x%16lx + x[%d]:0x%16lx\n",
                    rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2]);
    }

    cpu->pc += 4;
}

void exec_xor(CPU_State* cpu,uint32_t instr){
    uint8_t rd = (instr >> 7) & 0x1F;
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rs2 = (instr >> 20) & 0x1F;
    if(rd != 0){
        cpu->gpr[rd] = cpu->gpr[rs1] ^ cpu->gpr[rs2];
    }
    if(log_enable){
        printf("[xor] x[%d]:0x%16lx = x[%d]:0x%16lx + x[%d]:0x%16lx\n",
                    rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2]);
    }
    cpu->pc += 4;
}

void exec_sub(CPU_State* cpu, uint32_t instruction){
    uint8_t rd = (instruction >> 7) & 0x1F;
    uint8_t rs1 = (instruction >> 15) & 0x1F;
    uint8_t rs2 = (instruction >> 20) & 0x1F;

    if (rd != 0) {
        cpu->gpr[rd] = (uint64_t)((int64_t)cpu->gpr[rs1] - (int64_t)cpu->gpr[rs2]);
    }
    if(log_enable){
    printf("[sub] x[%d]:0x%16lx = x[%d]:0x%16lx - x[%d]:0x%16lx\n",
            rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2]);
    }

    cpu->pc += 4;

}

void exec_sltu(CPU_State* cpu,uint32_t instruction){
    uint8_t rd = (instruction >> 7) & 0x1F;
    uint8_t rs1 = (instruction >> 15) & 0x1F;
    uint8_t rs2 = (instruction >> 20) & 0x1F;

    if(rd != 0){
        cpu->gpr[rd] = ((uint64_t)cpu->gpr[rs1] < (uint64_t)cpu->gpr[rs2]) ? 1 : 0;
    }

    if(log_enable){
        printf("[sltu] x[%d]:0x%16lx = x[%d]:0x%16lx < x[%d]:0x%16lx ?\n",
                    rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2]);
    }

    cpu->pc += 4;
}

//B
void exec_bge(CPU_State* cpu,uint32_t instr){
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rs2 = (instr >> 20) & 0x1F;
    uint32_t imm12 = ((instr >> 7) & 0x1) << 11 |
                    ((instr >> 8) & 0xF) << 1 |
                    ((instr >> 25) & 0x3F ) << 5|
                    ((instr >> 31) & 0x1) << 12;
    
    int64_t imm = (int64_t)(((int32_t)imm12 << 19) >> 19);

    cpu->pc = cpu->gpr[rs1] >= cpu->gpr[rs2] ? cpu->pc + imm :cpu->pc + 4;
    //printf("-------- a5:0x%08x a4:0x%08x\n",cpu->gpr[rs1],cpu->gpr[rs2]);
}

void exec_bgeu(CPU_State* cpu,uint32_t instr){
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rs2 = (instr >> 20) & 0x1F;
    uint32_t imm12 = (((instr >> 7 )) & 0x1) << 11 |
                    ((instr >> 8) & 0xF) << 1|
                    ((instr >> 25) & 0x3F) << 5 |
                    ((instr >> 31) & 0x1) << 12 ;
    uint32_t m12 = ((instr >> 31) & 0x1) << 12;
    uint32_t m11 = ((instr >> 7 )) & 0x1;
    uint32_t m10_5 = ((instr >> 25) & 0x3F) << 5;
    uint32_t m4_1 = ((instr >> 8) & 0xF) << 1;

    uint32_t m = m12 | m11 | m10_5 | m4_1;
    if(log_enable){
    printf("m12:0x%16lx,m11:0x%16lx,m10_5:0x%16lx,m4_1:0x%16lx,m:0x%16lx\n",
            m12,m11,m10_5,m4_1,m);
    }

    int64_t imm = (int64_t)(((int32_t)imm12 << 19) >> 19);
    
    if((uint64_t)cpu->gpr[rs1] >= (uint64_t)cpu->gpr[rs2]){
        cpu->pc += imm;
    }else{
        cpu->pc += 4;
    }
    if(log_enable){
    printf("[bgeu] x[%d]:0x%16lx >= x[%d]:0x%16lx\n ",rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2]);
    printf("[bgeu] imm12:0x%08x,imm:0x%16lx\n",imm12,imm);
    printf("[bgeu >= jmp] pc:0x%16lx\n",cpu->pc);

    }
}

void exec_blt(CPU_State* cpu,uint32_t instr){
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rs2 = (instr >> 20) & 0x1F;

    uint32_t imm12 = (instr >> 31) << 12 |
                ((instr >> 24) & 0x3F) << 5 |
                ((instr >> 8 ) & 0xF) << 1 |
                ((instr >> 7) & 0x1) << 11;
    int64_t imm = (int64_t)( ((int32_t)imm12 << 20) >> 20);
    cpu->pc = ((int64_t)cpu->gpr[rs1] < (int64_t)cpu->gpr[rs2]) ? cpu->pc+imm:cpu->pc+4;
    if(log_enable){
    printf("[blt] x[%d]:0x%16lx < x[%d]:0x%16lx\n",rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2]);
    }

}

void exec_bltu(CPU_State* cpu,uint32_t instr){
    /*
    31     30       25 24     20 19     15 14   12 11         8      7       0
    +--------+---------+--------+-------+--------+---------+---------+
    | imm[12]| imm[10:5]|   rs2  |  rs1  | funct3 | imm[4:1]| imm[11]| opcode |
    +--------+---------+--------+-------+--------+---------+---------+
   */
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rs2 = (instr >> 20) & 0x1F;
   
    uint32_t imm12 = (instr >> 31) << 12 |
                ((instr >> 24) & 0x3F) << 5 |
                ((instr >> 8 ) & 0xF) << 1 |
                ((instr >> 7) & 0x1) << 11;
    

    int64_t imm = (int64_t)( ((int32_t)imm12 << 20) >> 20);
  
    cpu->pc = ((uint64_t)cpu->gpr[rs1] < (uint64_t)cpu->gpr[rs2]) ? cpu->pc+imm:cpu->pc+4;
   
    if(log_enable){
        printf("x[%d]:0x%08lx < x[%d]:0x%08lx ? \n",rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2]);
    }

}

void exec_bne(CPU_State* cpu,uint32_t instr){
    /*
        | 31     | 30..25 | 24..20| 19..15| 14..12 | 11..8 | 7      | 6..0   |
        |imm[12]|imm[10:5]|  rs2  |  rs1  | funct3 |imm[4:1]|imm[11]|opcode|
    */
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rs2 = (instr >> 20) & 0x1F;

    uint32_t imm12 = ((instr >> 31) & 0x1) << 12|
                 ((instr >> 25) & 0x3F) << 5 |
                 ((instr >> 8 ) & 0xF) << 1|
                 ((instr >> 7) & 0x1) << 11;
    int64_t imm = (int64_t)(((int32_t)imm12 << 19) >> 19);
    
    if(cpu->gpr[rs1] != cpu->gpr[rs2]){
        cpu->pc += imm; 
    }else{
        cpu->pc += 4;
    }
    if(log_enable){
    printf("[bne not equal jal to pc+=imm] x[%d]:0x%16lx != x[%d]:0x%16lx\n",rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2]);
    printf("[bne] old pc:0x%16lx + imm: 0x%16lx]\n",cpu->pc,imm);
    }


}

void exec_beq(CPU_State* cpu,uint32_t instr){
    /*
    +---------+---------+---------+---------+---------+---------+
    | imm[12] | imm[10:5] | rs2   | rs1    | funct3 | imm[4:1] | imm[11] | opcode |
    | 31      | 30:25     | 24:20 | 19:15  | 14:12  | 11:8     | 7       | 6:0    |
    */
    
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rs2 = (instr >> 20) & 0x1F;
    uint32_t imm12 = ((instr >> 31) & 0x1) << 12 |
                    ((instr >> 25) & 0x3F) << 5 |
                    ((instr >> 8) & 0xF) << 1 |
                    ((instr >> 7) & 0x1) << 11;
    
    int64_t imm = (int64_t)(((int32_t)imm12 << 19) >> 19);

    if(cpu->gpr[rs1] == cpu->gpr[rs2]){
        cpu->pc += imm; //2B align
    }else{
        cpu->pc += 4;
    }
    if(log_enable){
    printf("[beq equal jal to pc+=imm] x[%d]:0x%16lx == x[%d]:0x%16lx\n",
        rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2]);
    }

}

//M
void exec_mul(CPU_State* cpu,uint32_t instruction){
    uint8_t rd = (instruction >> 7) & 0x1F;
    uint8_t rs1 = (instruction >> 15) & 0x1F;
    uint8_t rs2 = (instruction >> 20) & 0x1F;
    
    if(rd != 0){
        cpu->gpr[rd] = (cpu->gpr[rs1] * cpu->gpr[rs2]) & 0XFFFFFFFF;
    }
    cpu->pc += 4;
}

void exec_div(CPU_State* cpu,uint32_t instr){
    uint8_t rd = (instr >> 7) & 0x1F;
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rs2 = (instr >> 20) & 0x1F;

    int64_t dividend = (int64_t)cpu->gpr[rs1];
    int64_t divisor = (int64_t)cpu->gpr[rs2];
    int64_t result = 0;

    if(rs2 == 0){ //检查是否是 mv 别名模式
        cpu->gpr[rd] = cpu->gpr[rs1];
    }else if(divisor == 0){
        result = -1;
    }else if(dividend == INT64_MIN && divisor == -1){
        result = dividend;
    }else{
        result = dividend / divisor;
    }
    if(rd != 0){
        cpu->gpr[rd] = result;
    }
    cpu->pc += 4;
    if(log_enable){
    printf("[div] x[%d]:0x%16lx = x[%d]:0x%16lx / x[%d]:0x%16lx\n",
        rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2]);
    }
}


//S
static void cpu_store8(CPU_State* cpu,uint64_t addr,uint8_t val){
    //memory_write(cpu->mem,addr,val,1);
    uint64_t pa = get_pa(cpu,addr,ACC_STORE);

    bus_write(&cpu->bus,pa,val,1);
}

void cpu_store16(CPU_State *cpu, uint64_t addr, uint16_t val) {

    bus_write(&cpu->bus,addr,val & 0xFF,2);
}

void cpu_store32(CPU_State *cpu, uint64_t addr, uint32_t val) {

    bus_write(&cpu->bus,addr,val,4);
}

void cpu_store64(CPU_State *cpu, uint64_t addr, uint64_t val) {
    
    bus_write(&cpu->bus,addr,val,8);
}
void exec_store(CPU_State* cpu,uint32_t instructions){
/*
 imm[11:5]   rs2   rs1   funct3   imm[4:0]   opcode
 31...25  24..20 19..15   14..12   11..7   6..0
*/
    uint8_t rs1 = (instructions >> 15) & 0x1F;
    uint8_t rs2 = (instructions >> 20) & 0x1F;
    uint8_t imm_25_31 = (instructions >> 25) & 0x7F;
    uint8_t imm_7_11 = (instructions >> 7) & 0x1F;
    uint32_t imm12 = (imm_25_31 << 5) | imm_7_11;
    int64_t imm = (int64_t)( ((int32_t)imm12 << 20) >> 20 );

    uint64_t addr = cpu->gpr[rs1] + imm;
    uint64_t value = cpu->gpr[rs2];

    uint8_t funct3 = (instructions >> 12) & 0x7 ;

    uint64_t pa = get_pa(cpu,addr,ACC_STORE);

    switch (funct3)
    {
    case 0x0: //SB
    {
        cpu_store8(cpu, pa, (uint8_t)(value & 0xFF));
        if(log_enable){
        printf("[Sb load 1 byte] x[%d]:0x%16lx + imm:0x%16lx = pa:0x%16lx,value = x[%d]:0x%16lx\n",
               rs1,cpu->gpr[rs1],imm,pa,rs2,cpu->gpr[rs2] );
        }
        break;
    }
    case 0x1: //SH
        cpu_store16(cpu, pa, (uint16_t)(value & 0xFFFF));
        if(log_enable){
            printf("[sh] pa:0x%08lx,val:0x%08lx\n",pa,(uint16_t)(value & 0xFFFF));
        }
        break;
    case 0x2://SW
        cpu_store32(cpu, pa, value);
        if(log_enable){
            printf("[exec_sw] pa:0x%08x,value:0x%08x,x[%d]:0x%16lx\n",pa,value,rs2,cpu->gpr[rs2]);
        }
        break;

    case 0b11://SD
    {
        cpu_store64(cpu, pa, (uint64_t)value);
        if(log_enable){
        printf("[sd store 8bytes] x[%d]:0x%16lx,x[%d]:0x%16lx,imm:0x%16lx\n",
                rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2],imm);
        printf("[sd 8 bytes] pa:0x%08lx\n",pa);
        }
        break;
    }
    default:
      
        break;
    }
    cpu->pc += 4;

}


// 系统调用（ECALL）指令 - 
void exec_ecall(CPU_State* cpu, uint32_t instruction) {
    //printf("ECALL instruction at PC: 0x%08x\n", cpu->pc);

    // 这里可以处理系统调用
    /* 选择是从 U/S/M 发出的 ECALL：根据当前 privilege 设置 cause */
 
    uint32_t cause = (cpu->privilege == 0 ? EXC_ECALL_U :
                    cpu->privilege == 1 ? EXC_ECALL_S : EXC_ECALL_M);
    /* 简单模式：在 emulator 中直接处理 syscall（host 接管），或把异常交给 guest */
    
 
    take_trap(cpu, cause, false);
   // printf("exec_ecall cpu->pc:0x%08x\n",cpu->pc);
   // printf("after take trap:%u\n",cpu->csr[CSR_MCAUSE]);
    
    //cpu->pc += 4;   
}

//ebreak
void exec_ebreak(CPU_State* cpu,uint32_t instructions){
    cpu->halted = true;
}

//mret
void exec_mret(CPU_State* cpu,uint32_t instr){
    /*
        31      20 19     15 14     12 11      7 6       0
        +----------+---------+---------+---------+---------+
        | funct12  |   rs1   | funct3  |   rd    | opcode  |
        +----------+---------+---------+---------+---------+
        | 0011000  | 00010   | 000     | 00000   | 1110011 |
        | (mret)   | (0x2)   | (PRIV)  | (x0)    | (SYSTEM)|
    */
   
    uint64_t mie = (cpu->csr[CSR_MSTATUS] & MSTATUS_MIE); //机器模式中断全局使能
    uint64_t mpie = cpu->csr[CSR_MSTATUS] & MSTATUS_MPIE ;//在进入异常（Trap）之前的 MIE 值
    uint64_t mpp = (cpu->csr[CSR_MSTATUS] >> 11) & 0x3;//进入机器模式异常之前的特权级别


    if(log_enable){
        printf("mpp:%d,mstatus:0x%08lx\n",mpp,cpu->csr[CSR_MSTATUS]);
    }

    uint64_t mstatus = cpu->csr[CSR_MSTATUS];

    if(mstatus & MSTATUS_MPIE){
        mstatus |= MSTATUS_MIE;
    }else{
        mstatus &= ~MSTATUS_MIE;
    }

    mstatus |= MSTATUS_MPIE;     
    switch (mpp)
    {
    case 0:
        cpu->privilege = 0;
        break;
    case 1:
        cpu->privilege = 1;
        break;
    case 3:
        cpu->privilege = 3;
        break;
    default:
        break;
    }

    if(log_enable){
        printf("mpp:%d,cpu->privilege:%d\n",mpp,cpu->privilege);
    }

    mstatus &= ~MSTATUS_MPP_MASK; 
    mstatus |= (0 << MSTATUS_MPP_SHIFT);

    cpu->csr[CSR_MSTATUS] = mstatus;
    cpu->pc = cpu->csr[CSR_MEPC];
   
}

//sfence.vma 
void exec_sfencevma(CPU_State* cpu,uint32_t instruction){

    uint8_t rs1 = (instruction >> 15) & 0x1F;
    uint8_t rs2 = (instruction >> 20) & 0x1F;
    //rs1 (vaddr): 指定要失效的虚拟地址。
    //rs2 (asid): 指定地址空间标识符。
    uint64_t vaddr = cpu->gpr[rs1];
    uint64_t asid = cpu->gpr[rs2];
    uint64_t vpn = (vaddr >> 12) & 0x3FFFFFF;

    // 直接处理 xv6 常用的情况：无参数 sfence.vma x0,x0
    // xv6 只用这一种！
    if (rs1 == 0 && rs2 == 0) {
        for(int i = 0; i < TLB_SIZE; i++){
            cpu->cpu_tlb.iTLB.entries[i].valid = false;
            cpu->cpu_tlb.dTLB.entries[i].valid = false;
        }
        printf("[sfence] flush all TLB\n");
    } else {
        // 其他情况暂不实现或简单 flush all
        for(int i = 0; i < TLB_SIZE; i++){
            cpu->cpu_tlb.iTLB.entries[i].valid = false;
            cpu->cpu_tlb.dTLB.entries[i].valid = false;
        }
        printf("[sfence] unimplemented case, flush all anyway\n");
    }
    cpu->pc += 4;
   
}

static void load_lb(CPU_State* cpu,uint64_t addr,uint8_t rd){
    int32_t val = 0;

    val = (int32_t)(int8_t)bus_read(&cpu->bus,addr,1);
    if(rd != 0){
        cpu->gpr[rd] = val;
    }
}
static void load_lbu(CPU_State* cpu,uint64_t addr,uint8_t rd){
    uint32_t val = 0;



    val = (uint32_t)bus_read(&cpu->bus,addr,1);
    if(rd != 0){
        cpu->gpr[rd] = val;
    }
    if(log_enable){
        printf("[lbu] x[%d]:0x%16lx,val:0x%16lx\n",rd,val);
    }
}

static void load_lw(CPU_State* cpu,uint64_t addr,uint8_t rd){
    uint64_t val = 0;


    val = (uint32_t)bus_read(&cpu->bus,addr,4);
    if(rd != 0){
        cpu->gpr[rd] = val;
    }
    if(log_enable){
        printf("[lw] x[rd:%d]:0x%08lx,va:0x%08lx,pa:0x%08lx\n",
            rd,cpu->gpr[rd],addr,addr);
    }

}

//load
/*
    imm[11:0]   rs1   funct3   rd   opcode
    [31:20]   [19:15]  [14:12] [11:7] [6:0]
*/

void exec_load(CPU_State *cpu,uint32_t instruction){
    uint8_t funct3 = (instruction >> 12) & 0x7;
    uint8_t rd = (instruction >> 7) & 0x1F;
    uint8_t rs1 = (instruction >> 15) & 0x1F;
    uint32_t imm12 = (instruction >> 20) & 0xFFF;
    int64_t imm = (int64_t)(((int32_t)imm12 << 20) >> 20);

    uint64_t addr = cpu->gpr[rs1] + imm;

    addr = get_pa(cpu,addr,ACC_LOAD);

    //printf("load funct3:%d\n",funct3);
    switch (funct3)
    {
    case 0x0:
       load_lb(cpu,addr,rd);
       
        break;
    case 0x1:
    
        break;
    case 0x2:
    {
        load_lw(cpu,addr,rd);
       // printf("lw a5 val:0x%08x\n",cpu->gpr[15]);   
        //printf("sp:0x%08x\n",cpu->gpr[8]);
        
        break;
    }
    case 0b011: // ld
    {
        int64_t val = 0;
        
        val = bus_read(&cpu->bus,addr,8);
        if(rd != 0){
            cpu->gpr[rd] = val;
        }
        
        if(log_enable){
        printf("[ld load 8 bytes] x[%d]:0x%16lx,addr:0x%16lx\n",rd,
        cpu->gpr[rd],addr);
        printf("[ld] rs1:%d,0x%16lx\n",rs1,cpu->gpr[rs1]);
        }
        break;
    }
    case 0b100:
        load_lbu(cpu,addr,rd);
       // printf("lbu rd:%d,addr:0x%08x,val:0x%08x",rd,addr,cpu->gpr[rd]);
        break;
    case 0b101://lhu

        uint64_t data = bus_read(&cpu->bus,addr,2);
        uint64_t val = data & 0xFFFF;
        if(rd != 0){
            cpu->gpr[rd] = val;
        }
        if(log_enable){
            printf("[lhu] x[rd:%d]:0x%08lx,addr:0x%08lx\n",rd,cpu->gpr[rd],addr
            );
        }
        break;
    case 0b110: //lwu
    {
        uint32_t val = 0;
        
        val = bus_read(&cpu->bus,addr,4);
        
        if(rd != 0){
            cpu->gpr[rd] = val;
        }
        break;
    }
    default:
        break;
    }
    cpu->pc += 4;
}

void exec_slli(CPU_State* cpu,uint32_t instr){
    /*
    | 31..25 | 24..20 | 19..15 | 14..12 | 11..7 | 6..0  |
    | funct7 | shamt  |  rs1   | funct3 |  rd   | opcode|
    */
    uint8_t rd = (instr >> 7) & 0x1F;
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t shamt = (instr >> 20) & 0x3F;
    if(rd != 0){
        cpu->gpr[rd] = cpu->gpr[rs1] << shamt;
    }
    if(log_enable){
          printf("[slli] x[%d]:0x%16lx = x[%d]:0x%16lx << shamt:0x%08lx\n",
                    rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],shamt);
    }

    cpu->pc += 4;
}

void exec_slti(CPU_State* cpu,uint32_t instr){
    uint8_t rd = (instr >> 7) & 0x1F;
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint64_t imm12 = (instr >> 20) & 0xFFF;
    int64_t imm = (int64_t)(( (int32_t)imm12 << 20) >> 20);

    if(rd != 0){
        cpu->gpr[rd] = ((int64_t)cpu->gpr[rs1] < (int64_t)imm)? 1:0;
    }

    if(log_enable){
    printf("[slti] x[%d]:0x%16lx,x[%d]:0x%16lx,imm:0x%16lx\n",
            rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],imm);

    }
    cpu->pc += 4;

}

void exec_sltiu(CPU_State* cpu,uint32_t instr){
    uint8_t rd = (instr >> 7) & 0x1F;
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint64_t imm12 = (instr >> 20) & 0xFFF;
    int64_t imm = (int64_t)( ((int32_t)imm12 << 20) >> 20);

    if(rd != 0){
        cpu->gpr[rd] = ((uint64_t)cpu->gpr[rs1] < (uint64_t)imm)? 1:0;
    }

    if(log_enable){
    printf("[sltiU] x[%d]:0x%16lx,x[%d]:0x%16lx,imm:0x%16lx\n",
            rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],imm);

    }
    cpu->pc += 4;
}

void exec_si(CPU_State* cpu,uint32_t instr){
 
    uint8_t funct7 = (instr >> 25) & 0x7F;
    uint8_t shamt = (instr >> 20) & 0x3F;
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rd = (instr >> 7) & 0x1F;

    switch (funct7)
    {
    case 0x0:
        if(rd != 0){
            cpu->gpr[rd] = (uint64_t)cpu->gpr[rs1] >> shamt; // 逻辑右移 SRLI
        if(log_enable){
        printf("[srli] x[%d]:0x%16lx,x[%d]:0x%16lx\n",rd,cpu->gpr[rd],rs1,cpu->gpr[rs1]);
        }
        }
        break;
    case 0x0100000:
        if(rd != 0){
            cpu->gpr[rd] = (int64_t)cpu->gpr[rs1] >> shamt; // 算术右移 SRAI
        }
        break;
    default:
        break;
    }
 
    cpu->pc += 4;
}

void exec_and(CPU_State* cpu,uint32_t instr){
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rs2 = (instr >> 20) & 0x1F;
    uint8_t rd = (instr >> 7) & 0x1F;

    if(rd != 0){
        cpu->gpr[rd] = cpu->gpr[rs1] & cpu->gpr[rs2];
    }
    if(log_enable){
        printf("[and] x[%d]:0x%16lx = x[%d]:0x%16lx + x[%d]:0x%16lx\n",
                    rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2]);
    }

    cpu->pc += 4;
    cpu->gpr[0] = 0;

}

void exec_or(CPU_State* cpu,uint32_t instr){
    /*
[31:25] funct7 | [24:20] rs2 | [19:15] rs1 | [14:12] funct3 | [11:7] rd | [6:0] opcode
    */

    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rs2 = (instr >> 20) & 0x1F;
    uint8_t rd = (instr >> 7) & 0x1F;

    if(rd != 0){
        cpu->gpr[rd] = cpu->gpr[rs1] | cpu->gpr[rs2];
    }
    cpu->pc += 4;
    cpu->gpr[0] = 0;

    if(log_enable){
        printf("[or] x[%d]:0x%08lx = x[%d]:0x%08lx | x[%d]:0x%08lx\n",
                rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2]);
    }

}

void exec_ori(CPU_State* cpu,uint32_t instr){
    /*
        imm[11:0] | rs1   | funct3 | rd    | opcode
        12 bits  | 5 bits | 3 bits | 5 bits| 7 bits
        ------------------------------------------------
        imm[11:0] | rs1   | 110    | rd    | 0010011
    */
    int32_t imm = (int32_t)(instr & 0xFFF00000) >> 20;

    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rd = (instr >> 7) & 0x1F;

    if(rd != 0){
        cpu->gpr[rd] = cpu->gpr[rs1] | imm;
    }

    
    cpu->pc += 4;

    if(log_enable){
        printf("[ori] x[%d]:0x%08lx = x[%d]:0x%08lx | imm:0x%08lx\n",
                rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],imm);
    }

}

void exec_andi(CPU_State* cpu,uint32_t instr){ //zext.b  &0xff
    /*
    | 31........20 | 19..15 | 14..12 | 11..7 | 6..0   |
    |    imm[11:0] |  rs1   | funct3 |  rd   | opcode |
    */
    uint8_t rd = (instr >> 7) & 0x1F;
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint64_t imm12 = (instr >> 20) & 0xFFF;
    int64_t imm = (int64_t)((int32_t)imm12);

    if(rd != 0){
        cpu->gpr[rd] = cpu->gpr[rs1] & imm;
    }
    if(log_enable){
    printf("[andi] x[%d]:0x%16lx,x[%d]:0x%16lx,imm:0x%16lx\n",rd,cpu->gpr[rd],
            rs1,cpu->gpr[rs1],imm);
    }


    cpu->pc += 4;
}

//csr

void exec_csr(CPU_State* cpu,uint32_t instr){
    /*
       | 31..20 (12) | 19..15 (5) | 14..12 (3) | 11..7 (5) | 6..0 (7) |
       |   csr[11:0] |    rs1     |  funct3    |    rd     |  opcode  |
    */

    uint8_t funct3 = (instr >> 12) & 0x7;
    uint8_t rd = (instr >> 7) & 0x1F;
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint32_t csr = (instr >> 20) & 0xFFF;
   
    switch (funct3)
    {
        case 0x1: // csrrw
            {
            if(rd != 0){
                cpu->gpr[rd] = cpu->csr[csr];
            }
            cpu->csr[csr] = cpu->gpr[rs1];
            if(log_enable){
            printf("[csrrw] x[rd:%d]:0x%16lx,x[rs1:%d]:0x%16lx,csr[0x%08x]:0x%16lx\n",
                        rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],csr,cpu->csr[csr]);
            }
         
            break;
            }
        case 0b010:  //csrrs
            if(rd != 0){
                cpu->gpr[rd] = cpu->csr[csr];
            }
            if(rs1 != 0){
                cpu->csr[csr] |= cpu->gpr[rs1];
            }
            if(log_enable){
            printf("[csrrs] x[rd:%d]:0x%16lx,csr[0x%08x] |= x[rs1:%d]:0x%16lx,val = 0x%16lx\n ",
                            rd,cpu->gpr[rd],csr,rs1,cpu->gpr[rs1],cpu->csr[csr]);
            }

            break;
        case 0b011: //csrrc
            if(rd != 0){
                cpu->gpr[rd] = cpu->csr[csr];
            }
            cpu->csr[csr] &= ~cpu->gpr[rs1];
            if(log_enable){
            printf("[csrrc] x[%d]:0x%16lx,csr[0x%08x] |= x[%d]:0x%16lx,val = 0x%16lx\n ",
                            rd,cpu->gpr[rd],csr,rs1,cpu->gpr[rs1],cpu->csr[csr]);
            }

            break;
        case 0b101://csrrwi
        {
            uint8_t imm5 = (instr >> 15) & 0x1F;
            uint64_t old_val = read_csr(cpu,csr);
            if(rd != 0){
                cpu->gpr[rd] = old_val;
            }

            write_csr(cpu,csr,imm5);
            if(log_enable){
            printf("[csrrwi] x[%d]:0x%16lx,csr[0x%08x]:0x%16lx\n ",
                            rd,cpu->gpr[rd],csr,cpu->csr[csr]);
            }
            break;
        }
        case 0b111://csrrci
        {
            uint8_t imm5 = (instr >> 15) & 0x1F;
            uint64_t old_value = read_csr(cpu,csr);
            uint64_t imm = ~((uint64_t)imm5);
            if(rd != 0){
                cpu->gpr[rd] = old_value;
            }
            write_csr(cpu,csr,old_value & imm );
            if(log_enable){
            printf("[csrrci] x[%d]:0x%16lx,new value:0x%16lx\n",rd,cpu->gpr[rd],
                        cpu->csr[csr]);
            }
        }
        default:
            break;
    }
    cpu->pc += 4;

}

void exec_iw(CPU_State* cpu,uint32_t instr){
    uint8_t funct3 = (instr >> 12) & 0x7;
    uint8_t rd = (instr >> 7) & 0x1F;
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint32_t imm12 = (instr >> 20) & 0xFFF;
    int32_t imm = ((int32_t)imm12 << 20) >> 20;
    if(rd != 0){
        cpu->gpr[rd] = (int64_t)((int32_t)cpu->gpr[rs1] + imm);//sext.w
    }
    if(log_enable){
    printf("[addiw]x[%d]:0x%16lx,x[%d]:0x%16lx,imm:0x%08x\n",
        rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],imm);
    }
    cpu->pc += 4;
}

void exec_amo(CPU_State* cpu,uint32_t instr){
    uint8_t rd = (instr >> 7) & 0x1F;
    uint8_t funct3 = (instr >> 12) & 0x7;
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rs2 = (instr >> 20) & 0x1F;
    uint8_t rl = (instr >> 25) & 0x1;
    uint8_t aq = (instr >> 26) & 0x1;
    uint8_t funct7 = (instr >> 27) & 0x1F;

    uint64_t addr = get_pa(cpu,cpu->gpr[rs1],ACC_STORE);

    if(funct3 == 0b010){ // .w
        switch (funct7)
        {
        case 0b00001: //AMOSWAP.W
            {   
                if(cpu->gpr[rs1] % 4 != 0){
                    printf("addr error\n");
                    return;
                }
                uint32_t tmp = bus_read(&cpu->bus,addr,4);
                bus_write(&cpu->bus,addr,cpu->gpr[rs2],4);
                cpu->gpr[rd] = tmp;
                cpu->pc += 4;
                if(log_enable){
                printf("[AMOSWAP.W] x[%d]:0x%16lx,x[%d]:0x%16lx,x[%d]:0x%16lx,tmp:0x%08x",
                        rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2],tmp);
                }
            
            break;
            }
        case 0b00000: //amoadd.w
        {
            
            uint64_t val = cpu->gpr[rs2];
            uint64_t old_val = 0;
            
            old_val = bus_read(&cpu->bus,addr,4); 
            val += old_val;
            bus_write(&cpu->bus,addr,val,4);
            if(rd != 0){
                cpu->gpr[rd] = (int64_t)((int32_t)old_val);
            }
            cpu->pc += 4;
        }
        default:
            break;
        }
    
    }
}

void memory_barrier(CPU_State *cpu, uint8_t pred, uint8_t succ) {
    // 完成pred指定的操作
   /* if (pred & FENCE_I) {  // 输入（读取）操作
        for (int i = 0; i < cpu->pending.load_count; i++) {
            uint64_t addr = cpu->pending.load_addrs[i];
            memory_sync_read(cpu->mem, addr);
        }
        cpu->pending.load_count = 0;
    }
    
    if (pred & FENCE_O) {  // 输出（写入）操作
        for (int i = 0; i < cpu->pending.store_count; i++) {
            uint64_t addr = cpu->pending.store_ops[i].addr;
            uint64_t value = cpu->pending.store_ops[i].value;
            memory_sync_write(cpu->mem, addr, value);
        }
        cpu->pending.store_count = 0;
    }
    
    // 其他操作类型（如果需要）
    if (pred & FENCE_R) {  // 读操作（同I）
        // 与FENCE_I相同
    }
    
    if (pred & FENCE_W) {  // 写操作（同O）
        // 与FENCE_O相同
    }
    
    // 同步内存系统
    memory_synchronize(cpu->mem);
    */
    printf("CPU: Memory barrier completed, pred=0x%x, succ=0x%x\n", pred, succ);
}


void exec_fence(CPU_State* cpu,uint32_t instr){
    uint8_t funct3 = (instr >> 12) & 0x7;
    uint64_t imm12 = (instr >> 20) & 0xFFF;
    uint8_t rd = (instr >> 7) & 0x1F;
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t succ = (instr >> 20) & 0xF;
    uint8_t pred = (instr >> 24) & 0xF;
    uint8_t fm = (instr >> 28) & 0xF;

    switch (funct3)
    {
    case 0b000: //fence
    {
        cpu->pc += 4;
        break;
    }
    case 0b001:
            cpu->pc += 4;
        break;
    
    default:
        break;
    }
}

void exec_float(CPU_State* cpu,uint32_t instr){
    uint8_t funct3 = (instr >> 12) & 0x7;
    uint8_t funct7 = (instr >> 25) & 0x7F;
    uint8_t rd = (instr >> 7) & 0x1F;
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rs2 = (instr >> 20) & 0x1F;

    switch (funct7)
    {
    case 0b1110000://fmv.x.w
        { 
        if(rs2 == 0 && funct3 == 0){
            uint32_t float_bits = cpu->fgpr[rs1] & 0xFFFFFFFF;
            int64_t float_datas = (int64_t)((int32_t)float_bits);
            cpu->gpr[rd] = float_datas;
        if(log_enable){
        printf("[fmv.x.w] x[%d]:0x%16lx,f[%d]:0x%16lx\n",rd,cpu->gpr[rd],
                    rs1,cpu->fgpr[rs1]);
        }
            }
        break;
        }
    case 0b1111000: //fmv.w.x
    {
        if(rs2 == 0 && funct3 == 0){
            uint32_t origin_bits = cpu->gpr[rs1] & 0xFFFFFFFF;
            cpu->fgpr[rd] = origin_bits;
        if(log_enable){
        printf("[fmv.w.x] x[%d]:0x%16lx,f[%d]:0x%16lx\n",rd,cpu->gpr[rd],
                    rs1,cpu->fgpr[rs1]);
        }
        }
        break;
    }
    case 0b1110001://fmv.x.d
    {
        if(rs2 == 0 && funct3 == 0){
            cpu->gpr[rd] = cpu->fgpr[rs1];
            if(log_enable){
            printf("[fmv.x.d] x[%d]:0x%16lx,f[%d]:0x%16lx\n",rd,cpu->gpr[rd],
                        rs1,cpu->fgpr[rs1]);
            }
        }
        break;
    }
    case 0b1111001://fmv.d.x
    {
        if(rs2 == 0 && funct3 == 0){
            cpu->fgpr[rd] = cpu->gpr[rs1];
            if(log_enable){
            printf("[fmv.d.x] x[%d]:0x%16lx,f[%d]:0x%16lx\n",rd,cpu->gpr[rd],
                        rs1,cpu->fgpr[rs1]);
            }
        }
        break;
    }
    default:
        break;
    }
    cpu->pc += 4;
}


void exec_wfi(CPU_State* cpu,uint32_t instr){

    // 检查当前是否有使能的中断待处理
    uint64_t mstatus = (uint64_t)read_csr(cpu, CSR_MSTATUS);
    uint64_t mie = (uint64_t)read_csr(cpu, CSR_MIE);
    uint64_t mip = (uint64_t)read_csr(cpu, CSR_MIP);
    
    uint64_t pending_enabled = mie & mip;




    if (pending_enabled && (mstatus & MSTATUS_MIE)) {
        // 有使能的中断待处理，不等待，继续执行
        printf("[WFI]: Interrupts pending, continuing execution\n");
        cpu->pc += 4;
    } else {
        // 没有中断待处理，进入等待状态
        printf("[WFI]: No enabled interrupts pending\n");
        
        // 在模拟器中，我们有几种选择：
        
        // 选项1：立即产生一个定时器中断（确保系统不卡死）
        if (cpu->mtime >= cpu->mtimecmp) {
            // 如果已经超时，立即设置中断
            uint64_t mip_val = read_csr(cpu, CSR_MIP);
            mip_val |= MIP_MTIP;
            write_csr(cpu, CSR_MIP, mip_val);
            printf("[WFI]: Setting timer interrupt to avoid deadlock\n");
        } else {
            // 选项2：快进到下一个定时器中断
            uint64_t cycles_until_interrupt = cpu->mtimecmp - cpu->mtime;
            if (cycles_until_interrupt < 1000) { // 如果很快就有中断
                cpu->mtime = cpu->mtimecmp; // 快进时间
                uint64_t mip_val = read_csr(cpu, CSR_MIP);
                mip_val |= MIP_MTIP;
                write_csr(cpu, CSR_MIP, mip_val);
                printf("[WFI]: Fast-forwarding to next timer interrupt\n");
            }
        }
        
        // 无论如何都继续执行，避免模拟器卡死
        
        
        // 立即检查是否有中断需要处理
        check_pending_and_take(cpu);
        cpu->pc = 0x800003be;
    }
}

void exec_3b(CPU_State* cpu,uint32_t instr){
    uint8_t funct7 = (instr >> 25) & 0x7F;
    uint8_t funct3 = (instr >> 12) & 0x7;
    uint8_t rs2 = (instr >> 20) & 0x1F;
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rd = (instr >> 7) & 0x1F;

    if(funct7 == 0 && funct3 == 0b001){ // sllw
        int32_t data = (cpu->gpr[rs1] & 0xFFFFFFFF);
        
        uint32_t shamt = (cpu->gpr[rs2] & 0x1F);
        int32_t imm = data << shamt;
        cpu->gpr[rd] = (int64_t)imm;
        cpu->pc += 4;
        if(log_enable){
        printf("[sllw] x[%d]:0x%08x,x[%d]:0x%08x,imm:0x%16lx\n",
               rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2],imm);
        }
    }else if(funct7 == 1 && funct3 == 0b111){ //remuw
        uint32_t divided = (uint32_t)(cpu->gpr[rs1]);
        uint32_t divisor = (uint32_t)(cpu->gpr[rs2]);
        uint32_t value = 0;
        if(divisor == 0){
            value = divided;
        }else{
            value = (divided % divisor);
        }

        int64_t  result = (int64_t)(int32_t)value;
        if(rd != 0){
            cpu->gpr[rd] = result;
        }
        cpu->pc += 4;

        if(log_enable){
            printf("[remuw] divided(rs1:%d):0x%08lx divisor(rs2:%d):0x%08lx,val(rd:%d):0x%08lx\n",
                rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2],rd,result);
        }
    }else if(funct7 == 1 && funct3 == 0b101){ //divuw
        uint32_t divided = (uint32_t)(cpu->gpr[rs1]);
        uint32_t divisor = (uint32_t)(cpu->gpr[rs2]);
        uint32_t value = 0;

        if(divisor == 0){
            value = 0xFFFFFFFF;
        }else{
            value = (divided / divisor);
        }

        int64_t result = (int64_t)(int32_t)value;
        if(rd != 0){
            cpu->gpr[rd] = result;
        }
        cpu->pc += 4;

        if(log_enable){
            printf("[divuw] divided(rs1:%d):0x%08lx divisor(rs2:%d):0x%08lx,val(rd:%d):0x%08lx\n",
                rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2],rd,result);
        }
    }
}

void exec_srl(CPU_State* cpu,uint32_t instr){
    uint8_t rd = (instr >> 7) & 0x1F;
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rs2 = (instr >> 20) & 0x1F;

    uint8_t shamt = cpu->gpr[rs2] & 0x3F;

    if(rd != 0){
        cpu->gpr[rd] = (uint64_t)cpu->gpr[rs1] >> shamt ;
    }
    cpu->pc += 4;
    if(log_enable){
    printf("[srl] x[%d]:0x%16lx,shamt:0x%08x,x[%d]:0x%16lx\n",
           rs1,cpu->gpr[rs1],shamt,rd,cpu->gpr[rd] );
    }
}


void exec_remu(CPU_State *cpu,uint32_t instr){
    uint8_t rd = (instr >> 7) & 0x1F;
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rs2 = (instr >> 20) & 0x1F;

    if(rd != 0){
        if(cpu->gpr[rs2] == 0){
            cpu->gpr[rd] = cpu->gpr[rs1];
        }else{
            cpu->gpr[rd] = (uint64_t)cpu->gpr[rs1] % (uint64_t)cpu->gpr[rs2];
        }
    }

    cpu->pc += 4;
    if(log_enable){
    printf("[remu] x[%d]:0x%016lx,x[%d]:0x%016lx,x[%d]:0x%016lx\n",
            rd,cpu->gpr[rd],rs1,cpu->gpr[rs2],rd,cpu->gpr[rs2]
    );
    }

}


void exec_divu(CPU_State *cpu,uint32_t instr){
    uint8_t rd = (instr >> 7) & 0x1F;
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rs2 = (instr >> 20) & 0x1F;

    if(rd != 0){
        if(cpu->gpr[rs2] == 0){
            cpu->gpr[rd] = 0xFFFFFFFFFFFFFFFF;
        }else{
            cpu->gpr[rd] = (uint64_t)cpu->gpr[rs1] / (uint64_t)cpu->gpr[rs2];
        }
    }

    cpu->pc += 4;
    if(log_enable){
    printf("[remu] x[%d]:0x%016lx,x[%d]:0x%016lx,x[%d]:0x%016lx\n",
            rd,cpu->gpr[rd],rs1,cpu->gpr[rs2],rd,cpu->gpr[rs2]
    );
    }

}