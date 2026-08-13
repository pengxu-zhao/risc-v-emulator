// src/instructions.c
#include "instructions.h"
#include "memory.h"
#include <stdio.h>
#include "trap.h"
#include "trap_vector.h"
#include "mmu.h"
#include "bus.h"
#include "cache.h"
#include <math.h>
#include "softfloat.h"
#include "softfloat_types.h"

extern uint8_t* memory;
extern int log_enable;
extern int j;
extern int rv_exit;

static inline print_all_gpr(CPU_State* cpu){
    fprintf(stderr,"x0~x31 value=========================\n");
    for(uint8_t i = 0; i < 32; i++){
        fprintf(stderr,"x[%d]:0x%016lx\n",i,cpu->gpr[i]);
    }
}

static void write_gpr(CPU_State* cpu,uint8_t rd,uint64_t val){
    if(rd != 0){
        cpu->gpr[rd] = val;
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
            }else{
                // 
            }               
            cpu->pc += 2;
            if(log_enable){
            fprintf(stderr,"[c.addi4spn] sp:0x%16lx + imm:0x%16lx,x[%d]:0x%16lx\n",
                        cpu->gpr[2],imm10,rd,cpu->gpr[rd]);
            }
            break;
        }
        case 0b001://c.fld
        {
            uint8_t rd = (instr >> 2) & 0x7 + 8;
            uint8_t rs1 = (instr >> 7) & 0x7 + 8;
 
            uint8_t imm = ((instr >> 5) & 0x3) << 6 |
                        ((instr >> 10) & 0x7) << 3;

            uint64_t addr = cpu->gpr[rs1] + (uint64_t)(uint32_t)imm;

            uint64_t pa = get_pa(cpu,addr,ACC_LOAD);
            if(pa == 0) return;
            float64_t val;
            val.v = bus_read(&cpu->bus,pa,8);

            if(rd != 0){
                cpu->fgpr[rd] = val.v;
            }

            cpu->pc += 2;

            if(log_enable){
                fprintf(stderr,"[c.fld] x[%d]:0x%16lx,addr:0x%16lx, pa:0x%08lx\n",rd,cpu->fgpr[rd],addr,pa);
            }
            break;
        }
        case 0b101://c.fsd
        {
            uint8_t rs1 = (instr >> 7) & 0x7 + 8;
            uint8_t rs2 = (instr >> 2) & 0x7 + 8;

            uint8_t imm = ((instr >> 5) & 0x3) << 6 |
                        ((instr >> 10) & 0x7) << 3;

            uint64_t addr = cpu->gpr[rs1] + (uint64_t)(uint32_t)imm;

            uint64_t pa = get_pa(cpu,addr,ACC_STORE);
            if(pa == 0) return;
            bus_write(&cpu->bus,pa,cpu->fgpr[rs2],8);

            cpu->pc += 2;

            if(log_enable){
                fprintf(stderr,"[c.fsd] x[%d]:0x%16lx,addr:0x%16lx, pa:0x%08lx\n",rs2,cpu->fgpr[rs2],addr,pa);
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
            uint64_t pa = get_pa(cpu,addr,ACC_LOAD);
            if(pa == 0) return;
            if(rd != 0){
                 
                cpu->gpr[rd] = (int64_t)(int32_t)bus_read(&cpu->bus,pa,4);
            }
            if(log_enable){
                fprintf(stderr,"[c.lw] x[%d]:0x%08lx,addr:0x%16lx, pa:0x%08lx\n",rd,cpu->gpr[rd],addr,pa);
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
        if(pa == 0) return;
        bus_write(&cpu->bus,pa,cpu->gpr[rs2],4);
        if(log_enable)
            fprintf(stderr,"[c.sw after] imm:0x%08lx,pa:0x%08x,rs2 val:%d 0x%08x\n",imm,pa,rs2,cpu->gpr[rs2]);
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
        if(pa == 0) return;
        bus_write(&cpu->bus,pa,cpu->gpr[rs2],8);
        cpu->pc += 2;
        if(log_enable){
        fprintf(stderr,"[c.sd ] vaddr:0x%16lx,pa:0x%16lx\n",addr,pa);
        fprintf(stderr,"[c.sd] x[%d]:0x%16lx,pa:0x%16lx,x[%d]:0x%16lx\n",
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
        if(log_enable){
            printf("[c.ld] vaddr:0x%16lx\n",vaddr);
        }
        uint64_t pa = get_pa(cpu,vaddr,ACC_LOAD);
        if(log_enable)
            printf("c.ld vaddr:0x%16lx, pa:0x%16lx\n",vaddr,pa);
        if(pa == 0) return;
        val = bus_read(&cpu->bus,pa,8);

        if(rd != 0){
            cpu->gpr[rd] = val;
        }
  
        cpu->pc += 2;
        if(log_enable){
            fprintf(stderr,"[c.ld] vaddr:0x%16lx\n",vaddr);
            fprintf(stderr,"[c.ld load 64bits] x[%d]:0x%16lx = load from pa:0x%16lx\n",
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
            fprintf(stderr,"[c.addi] x[%d]:0x%16lx,imm:0x%16lx\n",rd,cpu->gpr[rd],imm);
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
  
            int32_t result = (int32_t)( (int32_t)(cpu->gpr[rd] & 0xFFFFFFFF) + imm);
            cpu->gpr[rd] =(int64_t)result;
            cpu->pc += 2;
            if(log_enable){
            fprintf(stderr,"[c.addiw] x[%d]:0x%16lx,imm:0x%08x\n",rd,cpu->gpr[rd],imm);
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
        fprintf(stderr,"[c.li] x[%d]:0x%16lx = imm:0x%16lx\n",rd,cpu->gpr[rd],imm);
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
                fprintf(stderr,"[c.lui] x[%d]:0x%016lx\n",rd,cpu->gpr[rd]);
                }
                cpu->pc += 2;
            }else{ // c.addi16sp
                uint32_t imm6 = ((instr >> 2) & 0x1) << 5 |
                                ((instr >> 3) & 0x3) << 7 |
                                ((instr >> 5) & 0x1) << 6 |
                                ((instr >> 6) & 0x1) << 4 |
                                ((instr >> 12) & 0x1) << 9;
               

                int64_t imm = (int64_t)((int32_t)(imm6 << 22) >> 22);
                if(log_enable){
                    fprintf(stderr,"[before c.addi16sp] x[2]:0x%08lx,imm:%d\n",cpu->gpr[2],imm);
                    fprintf(stderr,"[imm6] imm6:0x%08lx\n",imm6);
                }
                cpu->gpr[rd] += imm; 
                cpu->pc += 2;
                if(log_enable){
                    fprintf(stderr,"[c.addi16sp] x[rd:%d]:0x%08lx,imm:0x%16lx\n",
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
            if(log_enable){
                printf("[c.srli] before : 0x%16lx\n",cpu->gpr[rd]);
            }
            cpu->gpr[rd] >>= shamt;
            cpu->pc += 2;
            if(log_enable){
            fprintf(stderr,"[c.srli] x[%d]:0x%16lx\n",rd,cpu->gpr[rd]);
            }
            
        }else if(funct2_10_11 == 0b01){//c.srai c.srai64
            uint8_t shamt = (instr >> 2) & 0x1F | 
                            (((instr >> 12) & 0x1) << 5);
             if(log_enable){
                fprintf(stderr,"[before c.srai] x[%d]:0x%08lx,shamt:%d\n",rd,cpu->gpr[rd],shamt);
            }


            cpu->gpr[rd] = (int64_t)cpu->gpr[rd] >> shamt;
            if(log_enable){
                fprintf(stderr,"[after c.srai] x[%d]:0x%08lx,shamt:%d\n",rd,cpu->gpr[rd],shamt);

            }

            cpu->pc += 2;

        }else if(funct2_10_11 == 0b10){  // c.andi
            uint32_t imm6 = (instr >> 2) & 0x1F |
                            ((instr >> 12) & 0x1) << 5;
            int32_t imm = (int32_t)(imm6 << 26) >> 26;
            cpu->gpr[rd] &= imm;

            if(log_enable){
                fprintf(stderr,"x[%d]:0x%08lx,imm:0x%08lx\n",rd,cpu->gpr[rd],imm);
            }

            cpu->pc += 2;

        }else if(funct2_10_11 == 0b11){
            if(funct2_56 == 0b11){ // c.and
                cpu->gpr[rd] &= cpu->gpr[rs2];
                cpu->pc += 2; 
                if(log_enable){
                fprintf(stderr,"[c.and] x[%d]:0x%16lx,x[%d]:0x%16lx\n",rd,cpu->gpr[rd],
                rs2,cpu->gpr[rs2]);
                }
            }else if(funct2_56 == 0b10){ //c.or
                cpu->gpr[rd] |= cpu->gpr[rs2];
                if(log_enable){
                    fprintf(stderr,"[c.or] x[%d]:0x%16lx |= x[%d]:0x%16lx\n",
                        rd,cpu->gpr[rd],rs2,cpu->gpr[rs2]);
                }

                cpu->pc += 2;
            }else if(funct2_56 == 0b00){ 
                uint8_t bit12 = (instr >> 12) & 0x1;
               
                if(bit12 == 0){ //c.sub
                    if(log_enable)
                        printf("[c.sub]before rs1:0x%16lx\n",cpu->gpr[rd]);
                    cpu->gpr[rd] -= cpu->gpr[rs2];
                    cpu->pc += 2;
                    if(log_enable){
                        fprintf(stderr,"[c.sub] x[rd:%d]:0x%08lx,x[rs2:%d]:0x%08lx\n",rd,
                            cpu->gpr[rd],rs2,cpu->gpr[rs2]);
                    }
                }else{ //c.subw
                    int32_t diff = (int32_t)cpu->gpr[rd] - (int32_t)cpu->gpr[rs2];
                    cpu->gpr[rd] = (int64_t)diff;
                    cpu->pc += 2;
                    if(log_enable){
                        fprintf(stderr,"[c.subw] x[rd:%d]:0x%08lx,x[rs2:%d]:0x%08lx\n",rd,
                            cpu->gpr[rd],rs2,cpu->gpr[rs2]);
                    }
                }
            }else if(funct2_56 == 0b01){ 
                uint8_t bit12 = (instr >> 12) & 0x1;
                if(bit12 == 1){//c.addw

                    int32_t sum = (int32_t)cpu->gpr[rd] + (int32_t)cpu->gpr[rs2];
                    cpu->gpr[rd] = (int32_t)sum;
                    cpu->pc += 2;
                    if(log_enable){
                        fprintf(stderr,"[c.addw] x[%d]:0x%16lx = x[%d]:0x%16lx + x[%d]:0x%16lx\n",
                            rd,cpu->gpr[rd],rd,cpu->gpr[rd],rs2,cpu->gpr[rs2]);
                    }
                }else{//c.xor
                    if(rd != 0){
                        cpu->gpr[rd] ^= cpu->gpr[rs2];
                    }
                    cpu->pc += 2;
                    if(log_enable){
                        fprintf(stderr,"[c.xor] x[%d]:0x%16lx ^= x[%d]:0x%16lx\n",
                            rd,cpu->gpr[rd],rs2,cpu->gpr[rs2]);
                    }
                }
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
            fprintf(stderr,"[c.j] imm:0x%16lx\n",imm);
            }
            break;
        }
    case 0b111://c.bnez  not equal to 0,jump to (pc+imm)
    {
        uint32_t imm8 = ((instr >> 3) & 0x3) << 1|
                        ((instr >> 10) & 0x3) << 3|
                        ((instr >> 2) & 0x1) << 5 |
                        ((instr >> 5) & 0x3) << 6 |
                        ((instr >> 12) & 0x1) << 8 ;
        int64_t imm = (int64_t)(((int32_t)imm8 << 23) >> 23);
        uint8_t rs1 = ((instr >> 7) & 0x7) + 8;
        //fprintf(stderr,"-----a5:0x%08x\n",cpu->gpr[15]);

        if(cpu->gpr[rs1] != 0){
            cpu->pc += imm;
        }else{
            cpu->pc += 2;
        }
        if(log_enable){
        fprintf(stderr,"[c.bnez not equal to 0,jump]x[%d]:0x%16lx,imm:0x%16lx\n",rs1,cpu->gpr[rs1],imm);
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
        fprintf(stderr,"[c.beqz rs1==0 jump]x[%d]:0x%16lx, imm:0x%16lx\n",rs1,cpu->gpr[rs1],imm);
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

    if(log_enable){
        printf("funct3:0x%08lx\n",funct3);
    }

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
        fprintf(stderr,"[c.slli] shamt:0x%08x, x[%d]:0x%16lx\n",shamt,rd,cpu->gpr[rd]);
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
        if(pa == 0) return;
        if(rd != 0){
            cpu->gpr[rd] =(int64_t)((int32_t)bus_read(&cpu->bus,pa,4));
        }
        if(log_enable){
            fprintf(stderr,"[c.lwsp]imm:0x%08x,pa:0x%08x,ra:0x%08x\n",imm,pa,cpu->gpr[rd]);
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
        uint64_t val = 0;
        if(log_enable){
            printf("x2:0x%16lx\n",cpu->gpr[2]);
            printf("[c.ldsp]vaddr: 0x%16lx , imm6:0x%08lx,imm:0x%08lx\n",vaddr,imm6,imm);
        }

        uint64_t pa = get_pa(cpu,vaddr,ACC_LOAD);
        if(pa == 0) return;
        val = bus_read(&cpu->bus,pa,8);
        cpu->gpr[rd] = val;
        cpu->pc += 2;
        if(log_enable){
            fprintf(stderr,"[c.ldsp] x[%d]:0x%16lx,vaddr:0x%16lx,pa:0x%08lx\n",
                                        rd,cpu->gpr[rd],vaddr,pa);
        }
        break;
    }

    case 0b110://c.swsp
    {
       
        uint32_t imm = ((instr >> 7) & 0x3) << 6 
                | ((instr >> 9) & 0xF) << 2 ;
           
        uint64_t addr = cpu->gpr[0x2] + imm;//x2 + imm

  
        uint64_t pa = get_pa(cpu,addr,ACC_STORE);
        if(pa == 0) return;
        bus_write(&cpu->bus,pa,cpu->gpr[rs2],4);
        if(log_enable){
            fprintf(stderr,"c.swsp pa : 0x%08x ,rs2 val:%d 0x%08x\n",pa,rs2,cpu->gpr[rs2]);
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
                    //fprintf(stderr,"c.jr rs1:0x%08x\n",cpu->gpr[rs1]);
                    cpu->pc = cpu->gpr[rs1];
                }
                if(log_enable){
                fprintf(stderr,"[c.jr c.ret] pc = x[%d]:0x%16lx\n",rs1,cpu->gpr[rs1]);
                }
            }else{ //c.jalr   

                if(log_enable){
                    fprintf(stderr,"[before c.jalr or c.ret] x[15] = 0x%16lx\n",
                        cpu->gpr[rs1]);
                }

                cpu->gpr[0x1] = cpu->pc+2;
                if(rs1 != 0){
                    cpu->pc = (cpu->gpr[rs1] & ~1ULL); // 将最低位置0
                }
                if(log_enable){
                    printf("x[rs1:%d]:0x%16lx\n",rs1,cpu->gpr[rs1]);
                fprintf(stderr,"[c.jalr or c.ret] x[0x1]:0x%16lx,pc = 0x%16lx\n",
                        cpu->gpr[0x1],cpu->pc);
                } 

            }
        }else if(rs1 != 0){
            if(instr12 == 0){ //c.mv
                cpu->gpr[rs1] = cpu->gpr[rs2];
                cpu->pc += 2;

            if(log_enable){
            fprintf(stderr,"[c.mv] x[%d]:0x%16lx,x[%d]:0x%16lx\n",rs1,cpu->gpr[rs1],
                    rs2,cpu->gpr[rs2]);
            }
            }else{ //c.add
                if(log_enable){
                fprintf(stderr,"[before c.add] x[%d]:0x%16lx,x[%d]:0x%16lx,x[%d]:0x%16lx\n",
                rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2]);
                }
                cpu->gpr[rd] = cpu->gpr[rs1] + cpu->gpr[rs2];
                cpu->pc += 2;
            if(log_enable){
            fprintf(stderr,"[c.add] x[%d]:0x%16lx,x[%d]:0x%16lx,x[%d]:0x%16lx\n",
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

        if(log_enable){

            printf("[c.sdsp] x[2]:0x%16lx, imm:0x%16lx\n",cpu->gpr[2],imm);
        }

        int64_t val = 0;
        if(log_enable){
            fprintf(stderr,"[before c.sdsp] x[%d]:0x%16lx,vaddr:0x%16lx,imm:0x%08lx\n",
                rs2,cpu->gpr[rs2],vaddr,imm);
        }

        uint64_t pa = get_pa(cpu,vaddr,ACC_STORE);

        if(pa == 0) return;
        bus_write(&cpu->bus,pa,cpu->gpr[rs2],8);
        
        cpu->pc += 2;
        if(log_enable){
        fprintf(stderr,"[c.sdsp] addr:0x%16lx,pa:0x%08lx,x[%d]:0x%16lx\n",
                                        vaddr,pa,rs2,cpu->gpr[rs2]);
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
    fprintf(stderr,"[addi] x[%d]:0x%16lx = x[%d]:0x%16lx + imm:0x%16lx\n",
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
    int64_t imm = (int64_t)((int32_t)imm20);

    if (rd != 0) {
        cpu->gpr[rd] = imm;
    }
    if(log_enable){
        fprintf(stderr,"[lui] imm sign-extended to 64,write to rd\n");
    fprintf(stderr,"[lui] x[%d] = imm:0x%16lx\n",rd,cpu->gpr[rd]);
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
          
    int64_t imm = (int64_t)((int32_t)imm20);
    if (rd != 0) {
        cpu->gpr[rd] = cpu->pc + imm;
    }
    
    if(log_enable){
    fprintf(stderr,"[auipc] x[%d]:0x%16lx,imm:0x%08x\n",rd,cpu->gpr[rd],imm);

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
    
    int64_t imm = (int64_t)(((int32_t)imm20 << 11) >> 11);

    if (rd != 0) {
        cpu->gpr[rd] = cpu->pc + 4;
    }

    if(log_enable){
    fprintf(stderr,"[jal] x[%d]:0x%16lx\n",rd,cpu->pc+4);

    }
    cpu->pc += imm;
    if(log_enable){
        printf("current pc:0x%16lx\n",cpu->pc);
        printf("imm:0x%16lx\n",imm);
    }
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
    if(log_enable)
        printf("imm12:0x%16lx\n,imm:0x%16lx\n",imm12,imm);

    if(rd != 0){
        cpu->gpr[rd] = cpu->pc + 4;
    } 

    if(log_enable){
    fprintf(stderr,"[jalr/jr] x[%d]:0x%16lx,pa:0x%16lx\n",rd,cpu->gpr[rd],addr);
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
        fprintf(stderr,"[add] x[%d]:0x%16lx = x[%d]:0x%16lx + x[%d]:0x%16lx\n",
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
        fprintf(stderr,"[xor] x[%d]:0x%16lx = x[%d]:0x%16lx + x[%d]:0x%16lx\n",
                    rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2]);
    }
    cpu->pc += 4;
}

void exec_xori(CPU_State* cpu,uint32_t instr){
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rd = (instr >> 7) & 0x1F;
    int64_t imm = (int64_t)(((int32_t)(instr >> 20) << 20) >> 20) ;

    if(rd != 0){
        cpu->gpr[rd] = cpu->gpr[rs1] ^ imm;
    }
    if(log_enable){
        printf("[xori] x[rd:%d]:0x%16lx,x[rs1:%d]:0x%16lx,imm:0x%16lx\n",
                rd,cpu->gpr[rd],rs1,cpu->gpr[rs1,imm]);
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
    fprintf(stderr,"[sub] x[%d]:0x%16lx = x[%d]:0x%16lx - x[%d]:0x%16lx\n",
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
        fprintf(stderr,"[sltu] x[%d]:0x%16lx = x[%d]:0x%16lx < x[%d]:0x%16lx ?\n",
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

    cpu->pc = (int64_t)cpu->gpr[rs1] >= (int64_t)cpu->gpr[rs2] ? cpu->pc + imm :cpu->pc + 4;
    if(log_enable){
        fprintf(stderr,"[bge]x[rs1:%d]:0x%08lx >= x[rs2:%d]:0x%08lx? imm:0x%08lx\n",
         rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2],imm);
    }

    //fprintf(stderr,"-------- a5:0x%08x a4:0x%08x\n",cpu->gpr[rs1],cpu->gpr[rs2]);
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
    fprintf(stderr,"m12:0x%16lx,m11:0x%16lx,m10_5:0x%16lx,m4_1:0x%16lx,m:0x%16lx\n",
            m12,m11,m10_5,m4_1,m);
    }

    int64_t imm = (int64_t)(((int32_t)imm12 << 19) >> 19);
    
    if((uint64_t)cpu->gpr[rs1] >= (uint64_t)cpu->gpr[rs2]){
        cpu->pc += imm;
    }else{
        cpu->pc += 4;
    }
    if(log_enable){
    fprintf(stderr,"[bgeu] x[%d]:0x%16lx >= x[%d]:0x%16lx\n ",rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2]);
    fprintf(stderr,"[bgeu] imm12:0x%08x,imm:0x%16lx\n",imm12,imm);
    fprintf(stderr,"[bgeu >= jmp] pc:0x%16lx\n",cpu->pc);

    }
}

void exec_blt(CPU_State* cpu,uint32_t instr){
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rs2 = (instr >> 20) & 0x1F;

    uint32_t imm12 = (instr >> 31) << 12 |
                ((instr >> 25) & 0x3F) << 5 |
                ((instr >> 8 ) & 0xF) << 1 |
                ((instr >> 7) & 0x1) << 11;
    int64_t imm = (int64_t)( ((int32_t)imm12 << 20) >> 20);
    cpu->pc = ((int64_t)cpu->gpr[rs1] < (int64_t)cpu->gpr[rs2]) ? cpu->pc+imm:cpu->pc+4;
    if(log_enable){
    fprintf(stderr,"[blt] x[%d]:0x%16lx < x[%d]:0x%16lx\n",rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2]);
    printf("[blt] imm12:0x%08x,imm:0x%16lx\n",imm12,imm);
    printf("[blt < jmp] pc:0x%16lx\n",cpu->pc);
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
                ((instr >> 25) & 0x3F) << 5 |
                ((instr >> 8 ) & 0xF) << 1 |
                ((instr >> 7) & 0x1) << 11;
    

    int64_t imm = (int64_t)( ((int32_t)imm12 << 19) >> 19);
  
    cpu->pc = ((uint64_t)cpu->gpr[rs1] < (uint64_t)cpu->gpr[rs2]) ? cpu->pc+imm:cpu->pc+4;
   
    if(log_enable){
        fprintf(stderr,"x[%d]:0x%08lx < x[%d]:0x%08lx ? \n",rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2]);
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
    fprintf(stderr,"[bne not equal jal to pc+=imm] x[%d]:0x%16lx != x[%d]:0x%16lx\n",rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2]);
    fprintf(stderr,"[bne] old pc:0x%16lx + imm: 0x%16lx]\n",cpu->pc,imm);
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
    fprintf(stderr,"[beq equal jal to pc+=imm] x[%d]:0x%16lx == x[%d]:0x%16lx\n",
        rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2]);
    }

}

//M
void exec_mul(CPU_State* cpu,uint32_t instruction){
    uint8_t rd = (instruction >> 7) & 0x1F;
    uint8_t rs1 = (instruction >> 15) & 0x1F;
    uint8_t rs2 = (instruction >> 20) & 0x1F;
    
    if(rd != 0){
        cpu->gpr[rd] = (cpu->gpr[rs1] * cpu->gpr[rs2]);
    }

    if(log_enable){
        printf("[mul] rd:%d = rs1:%d * rs2:%d ",rd,rs1,rs2);
        printf("[mul] 0x%16lx = 0x%16lx * 0x%16lx\n",cpu->gpr[rd],cpu->gpr[rs1],cpu->gpr[rs2]);
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
    fprintf(stderr,"[div] x[%d]:0x%16lx = x[%d]:0x%16lx / x[%d]:0x%16lx\n",
        rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2]);
    }
}


//S
static void cpu_store8_pa(CPU_State* cpu,uint64_t pa,uint8_t val){
    //memory_write(cpu->mem,addr,val,1);

    bus_write(&cpu->bus,pa,val,1);
}

void cpu_store16_pa(CPU_State *cpu, uint64_t pa, uint16_t val) {

    bus_write(&cpu->bus,pa,val,2);
}

void cpu_store32_pa(CPU_State *cpu, uint64_t pa, uint32_t val) {

    bus_write(&cpu->bus,pa,val,4);
}

void cpu_store64_pa(CPU_State *cpu, uint64_t pa, uint64_t val) {
    
    bus_write(&cpu->bus,pa,val,8);
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
    

    if(log_enable){
        fprintf(stderr,"[store] x[%d]:0x%16lx + imm:0x%16lx = addr:0x%16lx\n",
                rs1,cpu->gpr[rs1],imm,addr);
    }
    uint64_t pa = get_pa(cpu,addr,ACC_STORE);

    if(pa == 0) return;

    switch (funct3)
    {
    case 0x0: //SB
    {
        cpu_store8_pa(cpu, pa, (uint8_t)(value & 0xFF));
        if(log_enable){
        fprintf(stderr,"[Sb store 1 byte] x[%d]:0x%16lx + imm:0x%16lx = pa:0x%16lx,value = x[%d]:0x%16lx\n",
               rs1,cpu->gpr[rs1],imm,pa,rs2,cpu->gpr[rs2] );
        }
        break;
    }
    case 0x1: //SH
       
        cpu_store16_pa(cpu, pa, (uint16_t)(value & 0xFFFF));
        if(log_enable){
            fprintf(stderr,"[sh] pa:0x%08lx,val:0x%08lx\n",pa,(uint16_t)(value & 0xFFFF));
        }
        break;
    case 0x2://SW
        cpu_store32_pa(cpu, pa, value & 0xFFFFFFFF);
        if(pa == 0x80001000){
            if(value == 1)
                cpu->running = false;
            printf("occur sw to 0x%08lx,value:0x%16lx\n",pa,value);
        }

        if(log_enable){
            fprintf(stderr,"[exec_sw] pa:0x%08x,value:0x%08x,x[%d]:0x%16lx\n",pa,value,rs2,cpu->gpr[rs2]);
        }
        break;

    case 0b11://SD
    {
     
        cpu_store64_pa(cpu, pa, (uint64_t)value);

        uint64_t v = bus_read(&cpu->bus,pa,8);

        if(log_enable){
            fprintf(stderr,"read back value:0x%16lx\n",v);
        fprintf(stderr,"[sd store 8bytes]sd value x[%d]:0x%16lx to addr x[%d]:0x%16lx+imm:0x%16lx\n",
                rs2,cpu->gpr[rs2],rs1,cpu->gpr[rs1],imm);
        fprintf(stderr,"[sd 8 bytes] pa:0x%08lx\n",pa);
        }

        if(value == 0 && pa == 0x81203ef8){
            printf("--------[store 0] pc:0x%16lx,j:%ld\n",cpu->pc,j);
           // rv_exit = 1;
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
    //fprintf(stderr,"ECALL instruction at PC: 0x%08x\n", cpu->pc);
    //if(cpu->gpr[17] != 0x1 && j < 44046038)
       // printf(BLUE "execute ECALL pc:0x%16lx,j:%ld,:a6: %d a7:%d\n" RESET,cpu->pc,j,cpu->gpr[16],cpu->gpr[17]);
    /* 选择是从 U/S/M 发出的 ECALL：根据当前 privilege 设置 cause */
 
    uint32_t cause = 0;
    
    if(cpu->v){
        cause = 10;
    }else{
    
    cause = (cpu->privilege == 0 ? EXC_ECALL_U :
                    cpu->privilege == 1 ? EXC_ECALL_S : EXC_ECALL_M);
    }
    cpu->mem_fault.vaddr = cpu->pc; //记录发生 ECALL 时的 PC 作为 faulting address
    if(log_enable){
        fprintf(stderr,"[ECALL] from privilege level %d, cause: %d,faulting address: 0x%016lx\n", cpu->privilege, cause, cpu->mem_fault.vaddr);
        fprintf(stderr,"[ECALL] mtvec:0x%016lx\n", cpu->csr[CSR_MTVEC]);
        fprintf(stderr,"[ECALL] medeleg:0x%16lx\n",cpu->csr[CSR_MEDELEG]);
    }

    if(cpu->privilege != 3 && cpu->csr[CSR_MEDELEG] & (1 << cause)){
        // 如果当前特权级别不是 M 模式，并且 medeleg 中对应位被设置，说明这个异常应该委托给 S 模式处理
       // take_smode_fault(cpu,cause,false);
        take_smode_trap(cpu,cause,false);
    }else{
         // 否则由 M 模式处理
         //take_mmode_fault(cpu,cause,false);
          take_mmode_trap(cpu,cause,false);
    }
    
  
   // fprintf(stderr,"exec_ecall cpu->pc:0x%08x\n",cpu->pc);
   // fprintf(stderr,"after take trap:%u\n",cpu->csr[CSR_MCAUSE]);

}

//ebreak

uint64_t update_mstatus_for_trap(CPU_State* cpu, uint64_t cause) {
    uint64_t mstatus = cpu->csr[CSR_MSTATUS];
    // 保存当前 MIE 到 MPIE
    mstatus = (mstatus & ~MSTATUS_MPIE) | ((mstatus & MSTATUS_MIE) ? MSTATUS_MPIE : 0);
    // 禁止 MIE
    mstatus &= ~MSTATUS_MIE;
    // 设置 MPP 为当前特权级别
    mstatus = (mstatus & ~MSTATUS_MPP_MASK) | (cpu->privilege << MSTATUS_MPP_SHIFT);
    return mstatus;
}


void exec_ebreak(CPU_State* cpu,uint32_t instructions){
    
    // 1. 保存当前 PC
    if (cpu->privilege <= 3) {
        cpu->csr[CSR_MEPC] = cpu->pc;
        cpu->csr[CSR_MCAUSE] = 3;   // breakpoint exception
        // mtval 可以设为当前 pc 或 0，标准未强制
        cpu->csr[CSR_MTVAL] = 0;
        // 更新 mstatus
        cpu->csr[CSR_MSTATUS] = update_mstatus_for_trap(cpu, 3);
    }
    // 如果支持委托（可选），这里检查 medeleg 并可能跳转到 S 模式
    // 简单实现可以全部在 M 模式处理

    // 2. 跳转到陷阱向量
    cpu->privilege = 3;   // 进入机器模式
    cpu->pc = cpu->csr[CSR_MTVEC]; // 跳转到 mtvec 指向的地址
    // 注意：若 mtvec 为向量模式，需要根据 mcause 计算偏移量

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
        fprintf(stderr,"mpp:%d,mstatus:0x%08lx\n",mpp,cpu->csr[CSR_MSTATUS]);
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
        fprintf(stderr,"mpp:%d,cpu->privilege:%d\n",mpp,cpu->privilege);
    }

    mstatus &= ~MSTATUS_MPP_MASK; 
    mstatus |= (0 << MSTATUS_MPP_SHIFT);

    if( (mstatus & MSTATUS_MPV) && (mpp != 3) ){
        cpu->v = true;
    }else{
        cpu->v = false;
    }
    mstatus &= ~MSTATUS_MPV;
    cpu->csr[CSR_MSTATUS] = mstatus;
    cpu->pc = cpu->csr[CSR_MEPC];

    if(log_enable){
        printf("[after mret]:0x%16lx\n",cpu->pc);
    }
   
}

//sfence.vma 
void exec_sfencevma(CPU_State* cpu,uint32_t instruction){

    uint8_t rs1 = (instruction >> 15) & 0x1f;
    uint8_t rd = (instruction >> 7) & 0x1f;
    uint64_t vaddr = cpu->gpr[rs1];
    uint64_t asid = cpu->gpr[rd];

        for (int i = 0; i < TLB_SIZE; i++) {
        TLBEntry *e = &cpu->tlb.entries[i];
        if (!e->valid) continue;

        // 全局页忽略 asid/vaddr？规范：若 rs1=x0 则只比较 rs2 (asid)，但为简单可刷掉匹配 asid 的所有。
        // 通常实现：若 vaddr 非零，只刷特定地址；若 asid 非零，只刷该地址空间。
        // 这里简化：清除所有 asid 匹配的条目（若 asid 非零）。
        if (asid != 0 && e->asid != asid) continue;
        // 若 vaddr 非零，还需检查地址是否在同一页内（需根据 page_size 比较 VPN）
        if (vaddr != 0) {
            uint64_t vpn;
            switch (e->page_size) {
                case PAGE_4KB: vpn = (vaddr >> 12) & 0x3FFFFFF; break;
                case PAGE_2MB: vpn = (vaddr >> 21) & 0x1FFFFF; break;
                case PAGE_1GB: vpn = (vaddr >> 30) & 0x3FF; break;
                default: continue;
            }
            // 直接比较 tag 中的 VPN 部分（低 26 位）
            if ((e->tag & 0x3FFFFFF) != vpn) continue;
        }
        e->valid = 0;
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

static void load_lh(CPU_State* cpu,uint64_t addr,uint8_t rd){
    int32_t val = 0;
    val = (int32_t)(int16_t)bus_read(&cpu->bus,addr,2);
    if(rd != 0){
        cpu->gpr[rd] = val;
    }
}

static void load_lbu(CPU_State* cpu,uint64_t addr,uint8_t rd){
    uint64_t val = 0;
    val = (uint64_t)(uint32_t)bus_read(&cpu->bus,addr,1);
 
    if(rd != 0){
        cpu->gpr[rd] = val; 
    }
    if(log_enable){
        fprintf(stderr,"[lbu] load 1 byte ,zerp-extend to 64 bits,write to rd\n");
        fprintf(stderr,"[lbu] x[%d]:0x%16lx,val:0x%16lx,addr:0x%08lx\n",rd,cpu->gpr[rd],val,addr);
    }
}

static void load_lw(CPU_State* cpu,uint64_t addr,uint8_t rd){
    int64_t val = 0;

    val = (int64_t)(int32_t)bus_read(&cpu->bus,addr,4);
    if(rd != 0){
        cpu->gpr[rd] = val;
    }
    if(log_enable){
        fprintf(stderr,"[lw] x[rd:%d]:0x%08lx,va:0x%08lx,pa:0x%08lx\n",
            rd,cpu->gpr[rd],addr,addr);
    }
}

void exec_sll(CPU_State *cpu,uint32_t instr){
    uint8_t rd = (instr >> 7) & 0x1F;
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rs2 = (instr >> 20) & 0x1F;

    if(rd != 0){
        cpu->gpr[rd] = cpu->gpr[rs1] << (cpu->gpr[rs2] & 0x3F);
    }
    if(log_enable){
        fprintf(stderr,"[sll] x[%d]:0x%16lx = x[%d]:0x%16lx << x[%d]:0x%16lx\n",
                    rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2]);
    }
    cpu->pc += 4;
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

    if(log_enable){
        printf("[before transmit] rs1: x[%d]:0x%16lx, imm:0x%16lx, addr:0x%16lx\n",
                rs1,cpu->gpr[rs1],imm,addr);
    }

    addr = get_pa(cpu,addr,ACC_LOAD);
    if(addr == 0) return;
    switch (funct3)
    {
    case 0x0:
       load_lb(cpu,addr,rd);
       
        break;
    case 0x1: //lh
        load_lh(cpu,addr,rd);
        if(log_enable){
        fprintf(stderr,"[lh load 2 bytes] x[%d]:0x%16lx,addr:0x%16lx\n",rd,cpu->gpr[rd],addr);
        }
        break;
    case 0x2:
    {
        load_lw(cpu,addr,rd);
       // fprintf(stderr,"lw a5 val:0x%08x\n",cpu->gpr[15]);   
        //fprintf(stderr,"sp:0x%08x\n",cpu->gpr[8]);
        
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
        fprintf(stderr,"[ld load 8 bytes] x[%d]:0x%16lx,addr:0x%16lx\n",rd,
        cpu->gpr[rd],addr);
        }
        break;
    }
    case 0b100:
        load_lbu(cpu,addr,rd);
       // fprintf(stderr,"lbu rd:%d,addr:0x%08x,val:0x%08x",rd,addr,cpu->gpr[rd]);
        break;
    case 0b101://lhu

        uint64_t data = bus_read(&cpu->bus,addr,2);
        uint64_t val = data & 0xFFFF;
        if(rd != 0){
            cpu->gpr[rd] = val;
        }
        if(log_enable){
            fprintf(stderr,"[lhu] x[rd:%d]:0x%08lx,addr:0x%08lx\n",rd,cpu->gpr[rd],addr
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

        if(log_enable){
            printf("x[%d]:0x%16lx, addr:0x%16lx\n",rd,cpu->gpr[rd],addr);
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
          fprintf(stderr,"[slli] x[%d]:0x%16lx = x[%d]:0x%16lx << shamt:0x%08lx\n",
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
    fprintf(stderr,"[slti] x[%d]:0x%16lx,x[%d]:0x%16lx,imm:0x%16lx\n",
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
    fprintf(stderr,"[sltiU] x[%d]:0x%16lx,x[%d]:0x%16lx,imm:0x%16lx\n",
            rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],imm);

    }
    cpu->pc += 4;
}

void exec_si(CPU_State* cpu,uint32_t instr){
 
    uint8_t funct7 = (instr >> 26) & 0x3F;
    uint8_t shamt = (instr >> 20) & 0x3F;
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rd = (instr >> 7) & 0x1F;

    switch (funct7)
    {
    case 0x0:
        if(rd != 0){
            cpu->gpr[rd] = (uint64_t)cpu->gpr[rs1] >> shamt; // 逻辑右移 SRLI
        if(log_enable){
            fprintf(stderr,"[srli] x[%d]:0x%16lx,x[%d]:0x%16lx\n",rd,cpu->gpr[rd],rs1,cpu->gpr[rs1]);
        }
        }
        break;
    case 0b010000:
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
        fprintf(stderr,"[and] x[%d]:0x%16lx = x[%d]:0x%16lx + x[%d]:0x%16lx\n",
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
        fprintf(stderr,"[or] x[%d]:0x%08lx = x[%d]:0x%08lx | x[%d]:0x%08lx\n",
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
        fprintf(stderr,"[ori] x[%d]:0x%08lx = x[%d]:0x%08lx | imm:0x%08lx\n",
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
    
    int64_t imm = (int64_t)((int32_t)instr >> 20);

    if(rd != 0){
        cpu->gpr[rd] = cpu->gpr[rs1] & imm;
    }
    if(log_enable){
    fprintf(stderr,"[andi] x[%d]:0x%16lx,x[%d]:0x%16lx,imm:0x%16lx\n",rd,cpu->gpr[rd],
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
                
           // uint64_t old = cpu->csr[csr];
          //  cpu->csr[csr] = cpu->gpr[rs1]; 
            
            if(log_enable){
                printf("new:0x%16lx\n",cpu->gpr[rs1]);
            }

            uint64_t old = read_csr(cpu,csr);
            write_csr(cpu,csr,cpu->gpr[rs1]);
           
            if(log_enable){
                printf("old:0x%16lx\n",old);
            }
                
            if(rd != 0){
                cpu->gpr[rd] = old;
            }
            
            if(log_enable){
                fprintf(stderr,"rd = csr old value,csr = rs1\n");
                if(cpu->v){
                    printf("V csr:0x%16lx\n",cpu->vstvec);
                }
                else{
                fprintf(stderr,"[csrrw] x[rd:%d]:0x%16lx,x[rs1:%d]:0x%16lx,csr[0x%08x]:0x%16lx\n",
                        rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],csr,cpu->csr[csr]);
                }
            }
         
            break;
            }
        case 0b010:  //csrrs
            if(rd != 0){
                cpu->gpr[rd] = cpu->csr[csr];
            }
            if(log_enable){
                fprintf(stderr,"csr[0x%08x] old value:0x%16lx\n",csr,cpu->csr[csr]);
                fprintf(stderr,"rs1[%d] value:0x%16lx\n",rs1,cpu->gpr[rs1]);
            }
    
            if(rs1 != 0){
                cpu->csr[csr] |= cpu->gpr[rs1];
            }


            if(log_enable){
                fprintf(stderr,"[csrrs] rd = csr old value,csr |= rs1\n");
                fprintf("csr new value:0x%16lx\n",cpu->csr[csr]);

        
            }

            break;
        case 0b011: //csrrc
            if(rd != 0){
                cpu->gpr[rd] = cpu->csr[csr];
            }
            cpu->csr[csr] &= ~cpu->gpr[rs1];
            if(log_enable){
                fprintf(stderr," rd = csr old value,csr &= ~rs1\n");
            fprintf(stderr,"[csrrc] x[%d]:0x%16lx,csr[0x%08x] &= ~x[%d]:0x%16lx,val = 0x%16lx\n ",
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
            fprintf(stderr,"[csrrwi] x[%d]:0x%16lx,csr[0x%08x]:0x%16lx\n ",
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
            fprintf(stderr,"[csrrci] x[%d]:0x%16lx,new value:0x%16lx\n",rd,cpu->gpr[rd],
                        cpu->csr[csr]);
            }
            break;
        }case 0b110://csrrsi
        {
           uint8_t imm5 = (instr >> 15) & 0x1F;
           uint64_t old_value = cpu->csr[csr] ;
           uint64_t imm = (uint64_t)(uint32_t)imm5;
           cpu->csr[csr] |= imm;
           if(log_enable)
            fprintf(stderr,"[csrrsi] csr[0x%08lx]:0x%16lx\n",csr,cpu->csr[csr]);
           
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

    uint8_t funct7 = (instr >> 25) & 0x7F;
    uint8_t shamt = (instr >> 20) & 0x1F;


    switch (funct3)
    {
    case 0b000: //addiw
        if(rd != 0){
            cpu->gpr[rd] = (int64_t)((int32_t)cpu->gpr[rs1] + imm);//sext.w
        }
        if(log_enable){
            fprintf(stderr,"[addiw]x[%d]:0x%16lx,x[%d]:0x%16lx,imm:0x%08x\n",
            rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],imm);
        }
        break;
    case 0b001: // slliw

        if(rd != 0){
            cpu->gpr[rd] = (int64_t)((int32_t)cpu->gpr[rs1] << shamt); 
        }
        if(log_enable){
            fprintf(stderr,"[slliw] x[rd:%d]:0x%16lx,x[rs1:%d]:0x%16lx,shamt:0x%08lx\n",
                        rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],shamt);
        }
        break;
    case 0b101: 
        if(funct7 == 0b0000000){//srliw
            if(rd != 0){
                cpu->gpr[rd] = (int64_t)(int32_t)((uint32_t)cpu->gpr[rs1] >> shamt); 
            }
            if(log_enable){
                fprintf(stderr,"[srliw] x[rd:%d]:0x%16lx,x[rs1:%d]:0x%16lx,shamt:0x%08lx\n",
                            rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],shamt);
            }
        }else if(funct7 == 0b0100000){ //sraiw
            if(rd != 0){
                cpu->gpr[rd] = (int64_t)((int32_t)cpu->gpr[rs1] >> shamt); 
            }
            if(log_enable){
                fprintf(stderr,"[sraiw] x[rd:%d]:0x%16lx,x[rs1:%d]:0x%16lx,shamt:0x%08lx\n",
                            rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],shamt);
            }
        }
        break;
    default:
        break;
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
    uint64_t addr1 = cpu->gpr[rs1];
      if(log_enable){
                    fprintf(stderr,"[before AMOADD.W] addr:0x%08lx\n",addr1);
                }
    uint64_t addr = get_pa(cpu,cpu->gpr[rs1],ACC_STORE);
    if(addr == 0) return;
    if(funct3 == 0b010){ // .w
        switch (funct7)
        {
        case 0b100://amoxor.w
        {   
            uint64_t val = cpu->gpr[rs2];
            uint64_t old_val = 0;
            
            old_val = bus_read(&cpu->bus,addr,4); 
            val ^= old_val;
            bus_write(&cpu->bus,addr,val,4);
            if(rd != 0){
                cpu->gpr[rd] = (int64_t)((int32_t)old_val);
            }
            cpu->pc += 4;
            if(log_enable){
                fprintf(stderr,"[AMOXOR.W] x[%d]:0x%16lx,x[%d]:0x%16lx,x[%d]:0x%16lx,old_val:0x%08x\n",
                        rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2],(uint32_t)old_val);
            }
            break;
        }
        
        case 0b00001: //AMOSWAP.W
            {   
                if(cpu->gpr[rs1] % 4 != 0){
                    fprintf(stderr,"addr error\n");
                    cpu->halted = true;
                    return;
                }
                /*1.read old value 
                  2.write new value to old addr 
                  3.write old value to rd
                  
                  atomic, the whole process cannot be interrupted 
                  by other instructions,harts, including interrupts and exceptions.
                  */
                 if(log_enable){
                    fprintf(stderr,"[before AMOSWAP.W] x[%d]:0x%16lx,x[%d]:0x%16lx,x[%d]:0x%16lx,addr:0x%08lx\n",
                            rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2],addr);
                }
                uint32_t tmp = bus_read(&cpu->bus,addr,4);
                bus_write(&cpu->bus,addr,cpu->gpr[rs2],4)   ;
                write_gpr(cpu,rd,(int64_t)(int32_t)tmp);
                

                cpu->pc += 4;
                if(log_enable){
                fprintf(stderr,"[AMOSWAP.W] x[%d]:0x%16lx,x[%d]:0x%16lx,x[%d]:0x%16lx,tmp:0x%08x",
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
            if(log_enable){
                fprintf(stderr,"[AMOADD.W] old val:0x%08x,val:0x%08x\n", (uint32_t)old_val, (uint32_t)val);
                fprintf(stderr,"[AMOADD.W] hpa:0x%08lx\n",addr);
            }

            bus_write(&cpu->bus,addr,val,4);
            if(rd != 0){
                cpu->gpr[rd] = (int64_t)((int32_t)old_val);
            }
            cpu->pc += 4;
            break;
        }
        case 0b00010: //lr.w
        {
            uint64_t val = bus_read(&cpu->bus,addr,4);
            if(rd != 0){
                cpu->gpr[rd] = (int64_t)((int32_t)val);
            }
            cpu->reserv_addr = addr;
            cpu->reserv_valid = true;
            cpu->pc += 4;
            if(log_enable){
                fprintf(stderr,"[LR.W] x[%d]:0x%16lx,addr:0x%16lx\n",rd,cpu->gpr[rd],addr);
            }
            break;
        }
        case 0b00011: //sc.w
        {
            if(!cpu->reserv_valid || cpu->reserv_addr != addr){
                if(rd != 0){
                    cpu->gpr[rd] = 1; // sc失败，rd写1
                }
            }else{
                bus_write(&cpu->bus,addr,cpu->gpr[rs2],4);
                if(rd != 0){
                    cpu->gpr[rd] = 0; // sc成功，rd写0
                }
            }
            cpu->reserv_valid = false;
            cpu->pc += 4;
            if(log_enable){
                fprintf(stderr,"[SC.W] x[%d]:0x%16lx,addr:0x%16lx\n",rd,cpu->gpr[rd],addr);
            }
            break;
        }case 0b01000://amoor.w
        {
            uint64_t val = cpu->gpr[rs2];
            uint64_t old_val = 0;
            
            old_val = bus_read(&cpu->bus,addr,4); 
            val |= old_val;
            bus_write(&cpu->bus,addr,val,4);
            if(rd != 0){
                cpu->gpr[rd] = (int64_t)((int32_t)old_val);
            }
            cpu->pc += 4;
            if(log_enable){
                fprintf(stderr,"[AMOOR.W] x[%d]:0x%16lx,x[%d]:0x%16lx,x[%d]:0x%16lx,old_val:0x%08x\n",
                        rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2],(uint32_t)old_val);
            }
            break;
        }
        case 0b01100://amoand.w
        {
            uint64_t val = cpu->gpr[rs2];
            uint64_t old_val = 0;
            
            old_val = bus_read(&cpu->bus,addr,4); 
            val &= old_val;
            bus_write(&cpu->bus,addr,val,4);
            if(rd != 0){
                cpu->gpr[rd] = (int64_t)((int32_t)old_val);
            }
            cpu->pc += 4;
            if(log_enable){
                fprintf(stderr,"[AMOAND.W] x[%d]:0x%16lx,x[%d]:0x%16lx,x[%d]:0x%16lx,old_val:0x%08x\n",
                        rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2],(uint32_t)old_val);
            }
            break;
        }case 0b10100://amomax.w
        {
            uint64_t val = cpu->gpr[rs2];
            uint64_t old_val = 0;
            
            old_val = bus_read(&cpu->bus,addr,4); 
            val = (int32_t)old_val > (int32_t)val ? old_val : val;
            bus_write(&cpu->bus,addr,val,4);
            if(rd != 0){
                cpu->gpr[rd] = (int64_t)((int32_t)old_val);
            }
            cpu->pc += 4;
            break;   
        }case 0b11100: //amomaxu.w
        {
            uint64_t val = cpu->gpr[rs2];
            uint64_t old_val = 0;
            
            old_val = bus_read(&cpu->bus,addr,4); 
            val = (uint32_t)old_val > (uint32_t)val ? old_val : val;
            bus_write(&cpu->bus,addr,val,4);
            if(rd != 0){
                cpu->gpr[rd] = (int64_t)((int32_t)old_val);
            }
            cpu->pc += 4;
            break;   
        }case 0b10000://amomin.w
        {
            uint64_t val = cpu->gpr[rs2];
            uint64_t old_val = 0;
            
            old_val = bus_read(&cpu->bus,addr,4); 
            val = (int32_t)old_val < (int32_t)val ? old_val : val;
            bus_write(&cpu->bus,addr,val,4);
            if(rd != 0){
                cpu->gpr[rd] = (int64_t)((int32_t)old_val);
            }
            cpu->pc += 4;
            break;
        }case 0b11000: //amominu.w
        {
            uint64_t val = cpu->gpr[rs2];
            uint64_t old_val = 0;
            
            old_val = bus_read(&cpu->bus,addr,4); 
            val = (uint32_t)old_val < (uint32_t)val ? old_val : val;
            bus_write(&cpu->bus,addr,val,4);
            if(rd != 0){
                cpu->gpr[rd] = (int64_t)((int32_t)old_val);
            }
            cpu->pc += 4;
            break;
        }
 
        default:
            break;
        }
    }else if(funct3 == 0b011){//.D
        switch (funct7)
        {
            case 0b100://amoxor.d
            {   
                if(cpu->gpr[rs1] % 8 != 0){
                    fprintf(stderr,"addr error\n");
                    cpu->halted = true;
                    return;
                }
                uint64_t val = cpu->gpr[rs2];
                uint64_t old_val = 0;
                
                old_val = bus_read(&cpu->bus,addr,8); 
                val ^= old_val;
                bus_write(&cpu->bus,addr,val,8);
                if(rd != 0){
                    cpu->gpr[rd] = old_val;
                }
                cpu->pc += 4;
                if(log_enable){
                    fprintf(stderr,"[AMOXOR.D] x[%d]:0x%16lx,x[%d]:0x%16lx,x[%d]:0x%16lx,old_val:0x%16lx\n",
                            rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2],old_val);
                }
                break;
            }

            case 0b00001: //AMOSWAP.D
            {   
                if(cpu->gpr[rs1] % 8 != 0){
                    fprintf(stderr,"addr error\n");
                    cpu->halted = true;
                    return;
                }
                uint64_t tmp = bus_read(&cpu->bus,addr,8);
                bus_write(&cpu->bus,addr,cpu->gpr[rs2],8);
                write_gpr(cpu,rd,tmp);
                

                cpu->pc += 4;
                if(log_enable){
                fprintf(stderr,"[AMOSWAP.D] x[%d]:0x%16lx,x[%d]:0x%16lx,x[%d]:0x%16lx,tmp:0x%16lx",
                        rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2],tmp);
                }
            
            break;
            }
        case 0b00000: //amoadd.d
        {
            
            uint64_t val = cpu->gpr[rs2];
            uint64_t old_val = 0;
            
            old_val = bus_read(&cpu->bus,addr,8); 
            val += old_val;
            bus_write(&cpu->bus,addr,val,8);
            if(rd != 0){
                cpu->gpr[rd] = old_val;
            }
            cpu->pc += 4;
            break;
        }
        case 0b00010: //lr.d
        {
            uint64_t val = bus_read(&cpu->bus,addr,8);
            if(rd != 0){
                cpu->gpr[rd] = val;
            }
            cpu->reserv_addr = addr;
            cpu->reserv_valid = true;
            cpu->pc += 4;
            if(log_enable){
                fprintf(stderr,"[LR.D] x[%d]:0x%16lx,addr:0x%16lx\n",rd,cpu->gpr[rd],addr);
            }
            break;
        }
        case 0b00011: //sc.d
        {
            if(!cpu->reserv_valid || cpu->reserv_addr != addr){
                if(rd != 0){
                    cpu->gpr[rd] = 1; // sc失败，rd写1
                }
            }else{
                bus_write(&cpu->bus,addr,cpu->gpr[rs2],8);
                if(rd != 0){
                    cpu->gpr[rd] = 0; // sc成功，rd写0
                }
            }
            cpu->reserv_valid = false;
            cpu->pc += 4;
            if(log_enable){
                fprintf(stderr,"[SC.D] x[%d]:0x%16lx,addr:0x%16lx\n",rd,cpu->gpr[rd],addr);
            }
            break;
        }
        case 0b01000: //amoor.d
        {
            uint64_t val = cpu->gpr[rs2];
            uint64_t old_val = 0;
            
            old_val = bus_read(&cpu->bus,addr,8); 
            val |= old_val;
            bus_write(&cpu->bus,addr,val,8);
            if(rd != 0){
                cpu->gpr[rd] = old_val;
            }
            cpu->pc += 4;
            break;
        }
        case 0b01100: //amoand.d
        {
            uint64_t val = cpu->gpr[rs2];
            uint64_t old_val = 0;
            
            old_val = bus_read(&cpu->bus,addr,8); 
            val &= old_val;
            bus_write(&cpu->bus,addr,val,8);
            if(rd != 0){
                cpu->gpr[rd] = old_val;
            }
            cpu->pc += 4;
            if(log_enable){
                fprintf(stderr,"[AMOAND.D] x[%d]:0x%16lx,x[%d]:0x%16lx,x[%d]:0x%16lx,old_val:0x%16lx\n",
                        rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2],old_val);
            }
            break;
        }
        case 0b10100://amomax.d
        {
            uint64_t val = cpu->gpr[rs2];
            uint64_t old_val = 0;
            
            old_val = bus_read(&cpu->bus,addr,8); 
            val = (int64_t)old_val > (int64_t)val ? old_val : val;
            bus_write(&cpu->bus,addr,val,8);
            if(rd != 0){
                cpu->gpr[rd] = old_val;
            }
            cpu->pc += 4;
            break;
        }case 0b11100: //amomaxu.d
        {
            uint64_t val = cpu->gpr[rs2];
            uint64_t old_val = 0;


            if (addr & 0x7) {
            // 触发存储地址未对齐异常（AMO 视为 store）
            cpu->mcause = 6;       // Store address misaligned
            return;
            }
            old_val = bus_read(&cpu->bus,addr,8); 
            val = (uint64_t)old_val > (uint64_t)val ? old_val : val;
            bus_write(&cpu->bus,addr,val,8);
            if(rd != 0){
                cpu->gpr[rd] = old_val;
            }
            cpu->pc += 4;
            if(log_enable){
                fprintf(stderr,"[AMOMAXU.D] x[%d]:0x%16lx,x[%d]:0x%16lx,x[%d]:0x%16lx,old_val:0x%16lx\n",
                        rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2],old_val);
            }
            break;
        }case 0b10000://amomin.d
        {
            uint64_t val = cpu->gpr[rs2];
            uint64_t old_val = 0;
            
            old_val = bus_read(&cpu->bus,addr,8); 
            val = (int64_t)old_val < (int64_t)val ? old_val : val;
            bus_write(&cpu->bus,addr,val,8);
            if(rd != 0){
                cpu->gpr[rd] = old_val;
            }
            cpu->pc += 4;
            break;
        }case 0b11000: //amominu.d
        {   
            uint64_t val = cpu->gpr[rs2];
            uint64_t old_val = 0;
            
            old_val = bus_read(&cpu->bus,addr,8); 
            val = (uint64_t)old_val < (uint64_t)val ? old_val : val;
            bus_write(&cpu->bus,addr,val,8);
            if(rd != 0){
                cpu->gpr[rd] = old_val;
            }
            cpu->pc += 4;
            break;
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
    fprintf(stderr,"CPU: Memory barrier completed, pred=0x%x, succ=0x%x\n", pred, succ);
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
static inline void set_fflags(CPU_State *cpu, uint32_t flags) {
    cpu->csr[CSR_FFLAGS] |= flags;
}

// 安全类型双关
static inline uint32_t f32_to_bits(float f) {
    uint32_t u; memcpy(&u, &f, sizeof(u)); return u;
}
static inline float f32_from_bits(uint32_t u) {
    float f; memcpy(&f, &u, sizeof(f)); return f;
}

// 单精度加法核心
uint32_t fadd_s_core(uint32_t a, uint32_t b, uint8_t rm, uint32_t *p_flags) {
    uint32_t flags = 0;

    // 解包
    bool sa = (a >> 31) & 1, sb = (b >> 31) & 1;
    int32_t ea = (a >> 23) & 0xFF, eb = (b >> 23) & 0xFF;
    uint32_t ma = a & 0x7FFFFF, mb = b & 0x7FFFFF;

    // 判断特殊值
    bool inf_a = (ea == 0xFF), inf_b = (eb == 0xFF);
    bool nan_a = inf_a && ma, nan_b = inf_b && mb;
    bool zero_a = (ea == 0) && !ma, zero_b = (eb == 0) && !mb;

    // NaN
    if (nan_a || nan_b) {
        *p_flags = FFLAG_NV;
        return 0x7FC00000;   // 规范 NaN
    }

    // 无穷运算
    if (inf_a || inf_b) {
        if (inf_a && inf_b && sa != sb) {
            *p_flags = FFLAG_NV;
            return 0x7FC00000; // inf-inf
        }
        uint32_t inf_res = inf_a ? a : b;
        // 无穷 + 零 或无穷+同号无穷
        return inf_res;
    }

    // 零运算
    if (zero_a && zero_b) {
        // +0 + -0 的符号由舍入模式决定
        bool sign = (sa & sb) ? 1 : 0; // 同负得负
        if (sa != sb) sign = (rm == RDN) ? 1 : 0;
        return sign << 31;
    }
    if (zero_a) return b;
    if (zero_b) return a;

    // 附加隐含位
    bool a_denorm = (ea == 0);   // 次正规
    bool b_denorm = (eb == 0);
    if (!a_denorm) ma |= 0x800000;
    if (!b_denorm) mb |= 0x800000;

    int32_t rea = ea - 127, reb = eb - 127;
    if (a_denorm) rea = -126;
    if (b_denorm) reb = -126;

    // 向较大指数对齐，使用 56 位宽 (32 位尾数 + 24 位额外精度)
    int32_t exp_diff = rea - reb;
    uint64_t wide_a = (uint64_t)ma << 24;   // 高 24 位有效，低 24 位保护
    uint64_t wide_b = (uint64_t)mb << 24;

    if (exp_diff > 0) {
        if (exp_diff < 48) {
            uint64_t sticky_mask = (1ULL << exp_diff) - 1;
            wide_b = (wide_b >> exp_diff) | ((wide_b & sticky_mask) ? 1 : 0);
        } else {
            wide_b = 1; // sticky 为 1，移位后只剩下 1
        }
        reb = rea;
    } else if (exp_diff < 0) {
        exp_diff = -exp_diff;
        if (exp_diff < 48) {
            uint64_t sticky_mask = (1ULL << exp_diff) - 1;
            wide_a = (wide_a >> exp_diff) | ((wide_a & sticky_mask) ? 1 : 0);
        } else {
            wide_a = 1;
        }
        rea = reb;
    }

    // 有效指数
    int32_t res_exp = rea;
    bool sign_res;
    uint64_t sum_mant;

    if (sa == sb) {
        sum_mant = wide_a + wide_b;
        sign_res = sa;
    } else {
        if (wide_a >= wide_b) {
            sum_mant = wide_a - wide_b;
            sign_res = sa;
        } else {
            sum_mant = wide_b - wide_a;
            sign_res = sb;
        }
    }

    if (sum_mant == 0) {
        *p_flags = 0;
        return (rm == RDN ? 0x80000000 : 0); // -0
    }

    // 规格化：使最高有效位在 bit 47 (即 24位尾数的最高位)
    while (sum_mant < (1ULL << 47)) {
        sum_mant <<= 1;
        res_exp--;
    }
    while (sum_mant >= (1ULL << 48)) {
        sum_mant >>= 1;
        res_exp++;
    }

    // 尾数取高 24 位
    uint64_t mant = sum_mant >> 24;          // 24 bits
    uint64_t remainder = sum_mant & 0xFFFFFF; // 24 bits extra
    bool guard = (remainder >> 23) & 1;
    bool round = (remainder >> 22) & 1;
    bool sticky = (remainder & 0x3FFFFF) != 0;  // 低 22 位
    // 判断是否需要舍入
    bool inc = false;
    switch (rm) {
    case RNE:
        inc = guard && (round || sticky || (mant & 1));
        break;
    case RMM:
        inc = guard;  // 只要有 guard 位就向上
        break;
    case RTZ:
        inc = false;
        break;
    case RDN:
        inc = sign_res && (guard || round || sticky);
        break;
    case RUP:
        inc = !sign_res && (guard || round || sticky);
        break;
    }

    mant += inc ? 1 : 0;
    if (mant >= 0x1000000) {  // 尾数溢出
        mant >>= 1;
        res_exp++;
    }

    // 检查指数范围
    if (res_exp > 127) { // 上溢
        flags |= FFLAG_OF | FFLAG_NX;
        switch (rm) {
        case RDN: return sign_res ? 0xFF800000 : 0x7F7FFFFF;
        case RUP: return sign_res ? 0x807FFFFF : 0x7F800000;
        case RTZ: return sign_res ? 0x807FFFFF : 0x7F7FFFFF;
        default:  return sign_res ? 0xFF800000 : 0x7F800000;
        }
    }

    if (res_exp < -126) { // 下溢到次正规/零
        flags |= FFLAG_UF;
        int shift = -126 - res_exp;
        // 将 mant 右移，同时保留舍入信息
        uint64_t shift_sticky = 0;
        for (int i = 0; i < shift; i++) {
            shift_sticky |= (mant & 1);
            mant >>= 1;
        }
        // 再次舍入（次正规区域）
        bool g2 = (mant & 1); // 移位后最低位为 guard 等效
        bool s2 = shift_sticky ? 1 : 0;
        bool inc2 = false;
        switch (rm) {
        case RNE: inc2 = g2 && (s2 || (mant & 1)); break;
        case RMM: inc2 = g2; break;
        case RTZ: inc2 = false; break;
        case RDN: inc2 = sign_res && (g2 || s2); break;
        case RUP: inc2 = !sign_res && (g2 || s2); break;
        }
        mant += inc2;
        if (mant >= 0x800000) { // 回到了最小正规数
            mant = 0;
            res_exp = -126;
        } else {
            res_exp = -127; // 次正规
            flags |= FFLAG_NX;
            return (sign_res << 31) | mant;
        }
    }

    // 组装正规数/次正规数
    uint32_t result;
    if (res_exp <= -127) { // 次正规
        result = (sign_res << 31) | mant;
    } else {
        uint32_t biased_exp = res_exp + 127;
        result = (sign_res << 31) | (biased_exp << 23) | (mant & 0x7FFFFF);
    }

    if (guard || round || sticky) {
        flags |= FFLAG_NX;
    }

    *p_flags = flags;
    return result;
}

static double half_to_float(uint16_t h) {
    int sign   = (h >> 15) & 1;
    int exp    = (h >> 10) & 0x1F;
    uint32_t mant = h & 0x3FF;
    double val;

    if (exp == 0) {                     // 零 / 次正规
        if (mant == 0) val = 0.0;
        else           val = ldexp((double)mant, -24);   // mant * 2^{-24}
    } else if (exp == 0x1F) {          // 无穷 / NaN
        if (mant == 0) val = INFINITY;
        else           val = NAN;
    } else {                            // 正规数
        val = ldexp((double)(mant | 0x400), exp - 25);   // (1.mant) * 2^{exp-15}
    }
    return sign ? -val : val;
}

static uint16_t float_to_half(double d, uint8_t rm) {
    // 1. NaN 统一返回规范 NaN: 0x7E00
    if (isnan(d)) return 0x7E00;

    int sign = signbit(d);
    double x = fabs(d);

    // 2. 无穷
    if (isinf(x)) return (sign << 15) | 0x7C00;

    // 3. 零
    if (x == 0.0) return sign << 15;

    // 4. 上溢：> 65504
    if (x > 65504.0) {
        if (rm == RDN)      return sign ? 0xFC00 : 0x7BFF;   // -inf / +max
        else if (rm == RUP) return sign ? 0xFBFF : 0x7C00;   // -max / +inf
        else if (rm == RTZ) return sign ? 0xFBFF : 0x7BFF;   // ±max
        else                return (sign << 15) | 0x7C00;     // RNE/RMM → ±inf
    }

    // 5. 下溢：< 最小次正规 2^{-24} ≈ 5.96e-8
    if (x < 0x1p-24) {
        if (rm == RUP)      return sign ? 0x8000 : 0x0001;   // -0 / +min
        else if (rm == RDN) return sign ? 0x8001 : 0x0000;   // -min / +0
        else if (rm == RTZ) return sign << 15;               // ±0
        else { // RNE / RMM
            if (x > 0x1p-25) return sign ? 0x8001 : 0x0001;
            if (x < 0x1p-25) return sign << 15;
            // exactly 0x1p-25
            if (rm == RMM) return sign ? 0x8001 : 0x0001;
            return sign << 15;  // RNE ties to even → 0
        }
    }

    // 6. 正常/次正规范围：用 frexp 对齐到 11 位尾数
    int e;
    double frac = frexp(x, &e);         // x = frac * 2^e,  0.5 <= frac < 1
    int half_exp = e + 14;              // 半精度偏置指数候选值
    double half_frac = frac * 2048.0;   // 11‑bit 定点数 (范围 1024..2047)
    
    // 分离整数和小数部分
    double int_part;
    double rem = modf(half_frac, &int_part);
    int64_t m = (int64_t)int_part;

    // 根据舍入模式和符号决定增量 inc
    int inc = 0;
    if (rm == RTZ) {
        inc = 0;
    } else if (rm == RNE) {
        if (rem > 0.5 || (rem == 0.5 && (m & 1))) inc = 1;
    } else if (rm == RMM) {
        if (rem >= 0.5) inc = 1;
    } else if (rm == RDN) {
        inc = sign ? ((rem > 0.0) ? 1 : 0) : 0;
    } else if (rm == RUP) {
        inc = sign ? 0 : ((rem > 0.0) ? 1 : 0);
    }

    m += inc;
    if (m >= 2048) {     // 尾数溢出，向右规格化
        m >>= 1;
        half_exp++;
    }

    // 7. 处理最终指数：若 half_exp <= 0 则生成次正规数
    if (half_exp <= 0) {
        int shift = 1 - half_exp;          // 需要右移的位数
        uint64_t round_bits = m & ((1ULL << shift) - 1);
        bool half_bit = (shift > 0) && ((round_bits >> (shift - 1)) & 1);
        bool sticky   = (round_bits & ((1ULL << (shift - 1)) - 1)) != 0;
        
        m >>= shift;
        // 根据移位后的 bits 再行舍入（逻辑同上，但这里为简化直接取半调整）
        int s_inc = 0;
        if (rm == RNE)
            s_inc = half_bit && ((m & 1) || sticky);
        else if (rm == RMM)
            s_inc = half_bit;
        else if (rm == RDN)
            s_inc = sign && (half_bit || sticky);
        else if (rm == RUP)
            s_inc = !sign && (half_bit || sticky);
        // RTZ 不需要加

        m += s_inc;
        if (m >= 1024) {   // 次正规尾数溢出回正规数（最小正规数）
            half_exp = 1;
            m = 0;
        }
        return (sign << 15) | (uint16_t)(m & 0x3FF);
    }

    // 正常数：指数在 1..30 之间
    if (half_exp > 30) {   // 再次检查上溢
        return (sign << 15) | 0x7C00;
    }
    uint16_t mant10 = m & 0x3FF;               // 去掉隐含位
    return (sign << 15) | (half_exp << 10) | mant10;
}


static inline void fp_update_fflags(CPU_State *cpu)
{
    cpu->csr[CSR_FFLAGS] |= softfloat_exceptionFlags;

    uint8_t frm = (cpu->csr[CSR_FCSR] >> 5) & 0x7;
    cpu->csr[CSR_FCSR] =
        (frm << 5) |
        (cpu->csr[CSR_FFLAGS] & 0x1f);
}

static inline float32_t f32_unbox(uint64_t fpr_val) {
    float32_t r;
    r.v = (uint32_t)fpr_val;
    return r;
}

static inline int isNaNF32UI(uint32_t a)
{
    return (((a >> 23) & 0xff) == 0xff) && (a & 0x7fffff);
}

// 将 3 位 rm 编码转换为 SoftFloat 舍入常量
// 若 rm 为 0，使用 csr.frm 动态值（调用者负责传入 csr.frm）
static inline uint8_t rm_to_softfloat(uint8_t rm) {
  
    if (rm > 4) rm = 0;  // 降级为 RNE（保留值处理）

    switch (rm) {
        case 0: return softfloat_round_near_even;    // RNE
        case 1: return softfloat_round_minMag;       // RTZ
        case 2: return softfloat_round_min;          // RDN
        case 3: return softfloat_round_max;          // RUP
        case 4: return softfloat_round_near_maxMag;  // RMM
        default: // rm == 5,6,7 动态保留
            // 触发非法指令异常（此处简化，直接调用异常处理）
            fprintf(stderr, "Illegal instruction: invalid rm=0b%03b\n", rm);
    }
}

static inline bool softfloat_isSigNaNF32UI(uint32_t ui) {
    // 1. 指数必须全1 (0xFF)
    if ((ui & 0x7F800000) != 0x7F800000) return false;
    // 2. 尾数不能全0 (否则是无穷大)
    // 3. 尾数最高位 (bit 22) 必须为 0
    return ((ui & 0x007FFFFF) != 0) &&        // 尾数非零
           ((ui & 0x00400000) == 0);          // quiet bit = 0
}

static inline uint32_t softfloat_to_riscv_fflags(void)
{
    uint32_t flags = 0;

    if (softfloat_exceptionFlags & softfloat_flag_inexact)
        flags |= 0x01;        // NX

    if (softfloat_exceptionFlags & softfloat_flag_underflow)
        flags |= 0x02;        // UF

    if (softfloat_exceptionFlags & softfloat_flag_overflow)
        flags |= 0x04;        // OF

    if (softfloat_exceptionFlags & softfloat_flag_infinite)
        flags |= 0x08;        // DZ

    if (softfloat_exceptionFlags & softfloat_flag_invalid)
        flags |= 0x10;        // NV

    return flags;
}


static inline float64_t d_unbox(uint64_t v) {
    float64_t r;
    r.v = v;
    return r;
}

static inline uint64_t d_box(float64_t f) {
    return f.v;
}

static inline uint64_t f32_box(uint32_t v)
{
    return 0xFFFFFFFF00000000ULL | v;
}

static inline uint64_t f16_box(uint16_t v)
{
    return 0xFFFFFFFFFFFF0000ULL | v;
}

static inline uint64_t f64_box(uint64_t v)
{
    return v; // double 不需要 NaN-boxing
}

void exec_float(CPU_State* cpu,uint32_t instr){
    uint8_t funct3 = (instr >> 12) & 0x7;
    uint8_t funct7 = (instr >> 25) & 0x7F;
    uint8_t rd = (instr >> 7) & 0x1F;
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rs2 = (instr >> 20) & 0x1F;

    switch (funct7)
    {
        case 0b0000000: //fadd.s
        {
            if(funct3 == 7){
                funct3 = cpu->csr[CSR_FRM] & 0x7; 
            }
            softfloat_roundingMode = rm_to_softfloat(funct3);
            softfloat_exceptionFlags = 0;
            
            float32_t a = f32_unbox(cpu->fgpr[rs1]);
            float32_t b = f32_unbox(cpu->fgpr[rs2]);
            float32_t result = f32_add(a, b);
            cpu->fgpr[rd] = f32_box(result.v); //FLEN == 64
            
            cpu->csr[CSR_FFLAGS] |= softfloat_to_riscv_fflags();
            
             cpu->csr[CSR_FCSR] =
            ((cpu->csr[CSR_FRM] & 0x7) << 5)
            | (cpu->csr[CSR_FFLAGS] & 0x1f);
                uint32_t exc = softfloat_to_riscv_fflags();
            
            if(log_enable){
                fprintf(stderr,"[fadd.s] f[%d]:%f,f[%d]:%f,result:%f,fflags:0x%x\n",rd,f32_from_bits(a.v),rs2,f32_from_bits(b.v),f32_from_bits(result.v),exc);
            }
            break;
        }
        case 0b0000001: //fadd.d
        {
            if(funct3 == 0b000){
                uint64_t a_bits = cpu->fgpr[rs1];
                uint64_t b_bits = cpu->fgpr[rs2];

                // 2. 转 softfloat
                float64_t a = d_unbox(a_bits);
                float64_t b = d_unbox(b_bits);

                // 3. rounding mode
                softfloat_roundingMode = rm_to_softfloat(funct3);

                softfloat_exceptionFlags = 0;

                // 4. 计算
                float64_t result = f64_add(a, b);

                // 5. 写回（double 不需要 NaN-boxing）
                cpu->fgpr[rd] = d_box(result);

                // 6. 更新 fflags
                cpu->csr[CSR_FFLAGS] |= softfloat_exceptionFlags;
                if(log_enable){
                    fprintf(stderr,"[fadd.d] f[%d]:%lf,f[%d]:%lf,result:%lf\n",rd,a,rs2,b,result);
                }
            }
            break;
        }
        case 0b0000010: //fadd.h
        {
            uint8_t rm = funct3;
            uint16_t a_half = (uint16_t)(cpu->fgpr[rs1] & 0xFFFF);
            uint16_t b_half = (uint16_t)(cpu->fgpr[rs2] & 0xFFFF);
            float a = half_to_float(a_half);
            float b = half_to_float(b_half);
            float result = a + b;
            uint16_t result_half = float_to_half(result,rm);
            cpu->fgpr[rd] = f16_box(result_half);
            if(log_enable){
                fprintf(stderr,"[fadd.h] f[%d]:%f,f[%d]:%f,result:%f\n",rd,a,rs2,b,result);
            }
            
            break;
        }
        case 0b0000011://fadd.q
        {
            if(funct3 == 0b000){
                long double a = *(long double*)&cpu->fgpr[rs1];
                long double b = *(long double*)&cpu->fgpr[rs2];
                long double result = a + b;
                cpu->fgpr[rd] = f64_box(*(uint64_t*)&result); // 注意：这里假设使用64位寄存器存储结果，实际可能需要调整
                if(log_enable){
                    fprintf(stderr,"[fadd.q] f[%d]:%Lf,f[%d]:%Lf,result:%Lf\n",rd,a,rs2,b,result);
                }
            }
            break;
        }
        case 0b0000100: //fsub.s
        {
            if (funct3 == 7)
                funct3 = (cpu->csr[CSR_FCSR] >> 5) & 0x7;

            softfloat_roundingMode =
            rm_to_softfloat(funct3);

            softfloat_exceptionFlags = 0;

            float32_t a = f32_unbox(cpu->fgpr[rs1]);
            float32_t b = f32_unbox(cpu->fgpr[rs2]);

            float32_t result = f32_sub(a, b);

            // RV64 NaN-box
            cpu->fgpr[rd] = f32_box(result.v);
             
            uint32_t exc = softfloat_to_riscv_fflags();

            cpu->csr[CSR_FFLAGS] |= exc;

            cpu->csr[CSR_FCSR] =
                    ((cpu->csr[CSR_FRM] & 0x7) << 5)
                        | (cpu->csr[CSR_FFLAGS] & 0x1f);
            if(log_enable){
                fprintf(stderr,"[fsub.s] f[%d]:%f,f[%d]:%f,result:%f,fflags:0x%x\n",rd,f32_from_bits(a.v),rs2,f32_from_bits(b.v),f32_from_bits(result.v),exc);
            }
            break;
        }
        case 0b0001000://fmul.s
        {

            if (funct3 == 7)
                funct3 = (cpu->csr[CSR_FCSR] >> 5) & 0x7;

            softfloat_roundingMode =
            rm_to_softfloat(funct3);

            softfloat_exceptionFlags = 0;

            float32_t a = f32_unbox(cpu->fgpr[rs1]);
            float32_t b = f32_unbox(cpu->fgpr[rs2]);

            float32_t result = f32_mul(a, b);

            // RV64 NaN-box
            cpu->fgpr[rd] =
                0xffffffff00000000ULL |
                        result.v;

            uint32_t exc = softfloat_to_riscv_fflags();

            cpu->csr[CSR_FFLAGS] |= exc;

            cpu->csr[CSR_FCSR] =
                        ((cpu->csr[CSR_FRM] & 0x7) << 5)
                            | (cpu->csr[CSR_FFLAGS] & 0x1f);
            if(log_enable){
                fprintf(stderr,"[fmul.s] f[%d]:%f,f[%d]:%f,result:%f,fflags:0x%x\n",rd,f32_from_bits(a.v),rs2,f32_from_bits(b.v),f32_from_bits(result.v),exc);
            }
            break;
        }
        case 0b10100:
        {
            if(funct3 == 0){//fmin.s
  
                float32_t a = f32_unbox(cpu->fgpr[rs1]);
                float32_t b = f32_unbox(cpu->fgpr[rs2]);

                softfloat_exceptionFlags = 0;   // 清空本次异常标志

                bool a_isNaN = isNaNF32UI(a.v);
                bool b_isNaN = isNaNF32UI(b.v);
                bool a_isSig = softfloat_isSigNaNF32UI(a.v);
                bool b_isSig = softfloat_isSigNaNF32UI(b.v);

                float32_t res;

                if (a_isNaN || b_isNaN) {
                    // 若任一为信令 NaN，置 NV
                    if (a_isSig || b_isSig) {
                        softfloat_raiseFlags(softfloat_flag_invalid);
                    }

                    // 返回规则
                    if (a_isNaN && b_isNaN) {
                        res.v = 0x7FC00000;          // 规范 qNaN
                    } else if (a_isNaN) {
                        res = b;                     // a 是 NaN，返回 b
                    } else {
                        res = a;                     // b 是 NaN，返回 a
                    }
                } else {
                    // 两者都不是 NaN，比较大小
                    bool lt = f32_lt(a, b);         // a < b
                    bool eq = f32_eq(a, b);         // a == b（包括 +0 == -0）

                    if (lt) {
                        res = a;
                    } else if (f32_lt(b, a)) {
                        res = b;
                    } else {
                        // 相等情况，需特别处理 ±0
                        if ((a.v & 0x7FFFFFFF) == 0) {   // 都是零
                            res.v = a.v | b.v;           // 保留负零（0x80000000）
                        } else {
                            res = a;                     // 任意一个，值相同
                        }
                    }
                }

                cpu->fgpr[rd] = f32_box(res.v);
                fp_update_fflags(cpu);

            }else if(funct3 == 1){//fmax.s
                float32_t a = f32_unbox(cpu->fgpr[rs1]);
                float32_t b = f32_unbox(cpu->fgpr[rs2]);

                softfloat_exceptionFlags = 0;   // 清空本次异常标志

                bool a_isNaN = isNaNF32UI(a.v);
                bool b_isNaN = isNaNF32UI(b.v);
                bool a_isSig = softfloat_isSigNaNF32UI(a.v);
                bool b_isSig = softfloat_isSigNaNF32UI(b.v);

                float32_t res;

                if (a_isNaN || b_isNaN) {
                    // 若任一为信令 NaN，置 NV
                    if (a_isSig || b_isSig) {
                        softfloat_raiseFlags(softfloat_flag_invalid);
                    }

                    // 返回规则（与 FMIN.S 对称）
                    if (a_isNaN && b_isNaN) {
                        res.v = 0x7FC00000;          // 两个 NaN 返回规范 qNaN
                    } else if (a_isNaN) {
                        res = b;                     // a 是 NaN，返回 b（非 NaN）
                    } else {
                        res = a;                     // b 是 NaN，返回 a
                    }
                } else {
                    // 两者都不是 NaN，比较大小
                    bool lt_a = f32_lt(a, b);       // a < b
                    bool lt_b = f32_lt(b, a);       // b < a

                    if (lt_b) {
                        res = a;                     // a > b，返回 a
                    } else if (lt_a) {
                        res = b;                     // b > a，返回 b
                    } else {
                        // 相等情况，处理 ±0：应返回 +0
                        if ((a.v & 0x7FFFFFFF) == 0) {   // 两者都是零
                            res.v = a.v & b.v;           // 与操作保留正零（0x00000000）
                        } else {
                            res = a;                     // 任意一个，值相同
                        }
                    }
                }

                cpu->fgpr[rd] = f32_box(res.v);
                fp_update_fflags(cpu);

            }
            break;
        }
        case 0b1100://fdiv.s
        {
            // 1. 从浮点寄存器中取出操作数（NaN‑boxing 截低 32 位）
            float32_t a = f32_unbox(cpu->fgpr[rs1]);
            float32_t b = f32_unbox(cpu->fgpr[rs2]);

            // 2. 确定有效舍入模式
            uint8_t rm = funct3;
            if (rm == 0x7) {
                rm = (cpu->csr[CSR_FCSR] >> 5) & 0x7;  // 动态舍入，取 fcsr.frm
            }
            softfloat_roundingMode = rm_to_softfloat(rm);

            // 3. 清除全局异常标记，执行除法
            softfloat_exceptionFlags = 0;
            float32_t res = f32_div(a, b);

            // 4. 结果 NaN‑boxing 写入 f[rd]
            cpu->fgpr[rd] = f32_box(res.v);

            // 5. 更新 fflags（累积异常标志）
            fp_update_fflags(cpu);
            
            break;
        }
        case 0b101100:
        {
            if(rs2 == 0){//fsqrt.s

            // 1. 取出源操作数（NaN‑boxing 截低 32 位）
            float32_t a = f32_unbox(cpu->fgpr[rs1]);

            // 2. 确定有效舍入模式
            uint8_t rm = funct3;
            if (rm == 0x7) {
                rm = (cpu->csr[CSR_FCSR] >> 5) & 0x7;  // 动态舍入，取 fcsr.frm
            }
            softfloat_roundingMode = rm_to_softfloat(rm);

            // 3. 清异常并执行平方根运算
            softfloat_exceptionFlags = 0;
            float32_t res = f32_sqrt(a);

            // 4. NaN‑box 后写入 f[rd]
            cpu->fgpr[rd] = f32_box(res.v);

            // 5. 更新 fflags（累积异常）
            fp_update_fflags(cpu);

            }
            break;
        }
        case 0b1010000:
        {
            if(funct3 == 0b010){//feq.s
                float32_t a = f32_unbox(cpu->fgpr[rs1]);
                float32_t b = f32_unbox(cpu->fgpr[rs2]);
                softfloat_exceptionFlags = 0;

                uint32_t res = f32_eq(a, b);
                // NaN → NV + return 0（SoftFloat 已处理，但保险）
                if (isNaNF32UI(a.v) || isNaNF32UI(b.v)) {
                    softfloat_exceptionFlags |= softfloat_flag_invalid;
                    res = 0;
                }
                cpu->gpr[rd] = res;
                fp_update_fflags(cpu);
           
            }else if(funct3 == 0b001){ //flt.s
                float32_t a = f32_unbox(cpu->fgpr[rs1]);
                float32_t b = f32_unbox(cpu->fgpr[rs2]);

                softfloat_exceptionFlags = 0;

                uint32_t res = f32_lt(a, b);

                if (isNaNF32UI(a.v) || isNaNF32UI(b.v)) {
                    softfloat_exceptionFlags |= softfloat_flag_invalid;
                    res = 0;
                }

                cpu->gpr[rd] = res;
                fp_update_fflags(cpu);
            }
            else if(funct3 == 0){//fle.s
                float32_t a = f32_unbox(cpu->fgpr[rs1]);
                float32_t b = f32_unbox(cpu->fgpr[rs2]);

                softfloat_exceptionFlags = 0;

                uint32_t res = f32_le(a, b);

                if (isNaNF32UI(a.v) || isNaNF32UI(b.v)) {
                    softfloat_exceptionFlags |= softfloat_flag_invalid;
                    res = 0;
                }

                cpu->gpr[rd] = res;
                fp_update_fflags(cpu);
            }
            break;
        }
        case 0b1100000:
        {
            if(rs2 == 0){//fcvt.w.s
                // 1. 取出浮点寄存器中的单精度值（NaN‑boxing 截低 32 位）
                float32_t a = f32_unbox(cpu->fgpr[rs1]);

                // 2. 确定有效舍入模式
                uint8_t rm = funct3;               // 指令的静态 rm
                if (rm == 0x7) {
                    rm = (cpu->csr[CSR_FCSR] >> 5) & 0x7;  // 动态舍入，取 fcsr.frm
                }
                softfloat_roundingMode = rm_to_softfloat(rm); // 映射到 SoftFloat

                // 3. 清异常并执行转换（关键调用）
                softfloat_exceptionFlags = 0;
                int32_t result = f32_to_i32(a, softfloat_roundingMode, true); // 第三个参数 true=精确无效检查

                // 4. 写回整数寄存器（RV64 需要符号扩展）
                cpu->gpr[rd] = (int64_t)result;    // RV64 符号扩展，RV32 直接 result

                // 5. 更新 fflags（可能产生 NV 或 NX）
                fp_update_fflags(cpu);

            }else if(rs2 == 1){//fcvt.wu.s
                // 1. 取出浮点值（NaN‑boxing 处理）
                float32_t a = f32_unbox(cpu->fgpr[rs1]);

                // 2. 确定有效舍入模式
                uint8_t rm = funct3;
                if (rm == 0x7) {
                    rm = (cpu->csr[CSR_FCSR] >> 5) & 0x7;
                }
                softfloat_roundingMode = rm_to_softfloat(rm);

                // 3. 清异常并执行转换（关键调用）
                softfloat_exceptionFlags = 0;
                uint32_t result = f32_to_ui32(a, softfloat_roundingMode, true); // 精确无效检查

                // 4. 写回整数寄存器（RV64 零扩展）
                cpu->gpr[rd] = (uint64_t)result;  // RV64，高位自动填 0

                // 5. 更新 fflags（可能产生 NV 或 NX）
                fp_update_fflags(cpu);
            }
            break;
        }
        case 0b1101000:
        {
            if(rs2 == 0){ //fcvt.s.w
                int32_t int_val = (int32_t)cpu->gpr[rs1];
                uint8_t rm = funct3;
                if (rm == 0x7) {
                    rm = (cpu->csr[CSR_FCSR] >> 5) & 0x7;  // 动态舍入，取 fcsr.frm
                }
                softfloat_roundingMode = rm_to_softfloat(rm);
                softfloat_exceptionFlags = 0;
                float32_t res = i32_to_f32(int_val);
                cpu->fgpr[rd] = f32_box(res.v);
                fp_update_fflags(cpu);
                if(log_enable){
                    fprintf(stderr,"csr_fcsr:0x%16lx,fgpr[%d]:0x%16lx\n",cpu->csr[CSR_FCSR],rd,cpu->fgpr[rd]);
                }
            }else if(rs2 == 1){//fcvt.s.wu
                uint32_t src = (uint32_t)cpu->gpr[rs1];      
                uint8_t rm = funct3;
                if (rm == 0x7) {
                    rm = cpu->csr[CSR_FRM] & 0x7;  // 动态舍入，取 fcsr.frm
                }
                softfloat_roundingMode = rm_to_softfloat(rm);
                softfloat_exceptionFlags = 0;
                float32_t res = ui32_to_f32(src);
                cpu->fgpr[rd] = f32_box(res.v);
                fp_update_fflags(cpu);
            }else if(rs2 == 0b10){//fcvt.s.l
                int64_t src = (int64_t)cpu->gpr[rs1];
                        uint8_t rm = funct3;
                if (rm == 0x7) {
                    rm = (cpu->csr[CSR_FCSR] >> 5) & 0x7;  // 动态舍入，取 fcsr.frm
                }
                softfloat_roundingMode = rm_to_softfloat(rm);
                softfloat_exceptionFlags = 0;
                float32_t res = i64_to_f32(src);
                cpu->fgpr[rd] = f32_box(res.v);
                fp_update_fflags(cpu);
            }else if(rs2 == 0b11){//fcvt.s.lu
                uint64_t src = (uint64_t)cpu->gpr[rs1];
                uint8_t rm = funct3;
                if (rm == 0x7) {
                    rm = (cpu->csr[CSR_FCSR] >> 5) & 0x7;  // 动态舍入，取 fcsr.frm
                }
                softfloat_roundingMode = rm_to_softfloat(rm);
                softfloat_exceptionFlags = 0;
                float32_t res = ui64_to_f32(src);
                cpu->fgpr[rd] = f32_box(res.v);
                fp_update_fflags(cpu);
            }
            break;
        }
        case 0b1110000:
        { 
            if(rs2 == 0 && funct3 == 0){//fmv.x.w
               uint32_t bits = (uint32_t)cpu->fgpr[rs1];   // 取低 32 位（NaN‑boxing 已包含高 32 位全 1，截断即得单精度位模式）
                cpu->gpr[rd] = (int32_t)bits;                // 符号扩展（RV64），RV32 则直接赋值
                if(log_enable){
                    fprintf(stderr,"[fmv.x.w] x[%d]:0x%16lx,f[%d]:0x%16lx\n",rd,cpu->gpr[rd],
                            rs1,cpu->fgpr[rs1]);
                }
            }else if(rs2 == 0 && funct3 == 1){ //fclass.s
                float32_t a = f32_unbox(cpu->fgpr[rs1]);

                uint32_t v = a.v;

                bool sign = (v >> 31) & 1;
                uint32_t exp = (v >> 23) & 0xFF;
                uint32_t frac = v & 0x7FFFFF;

                uint32_t class;
                if (exp == 0xFF) {          // 指数全 1
                    if (frac == 0) {        // 尾数全 0 → 无穷
                    class = sign ? (1 << 0) : (1 << 7);   // 负无穷: bit0, 正无穷: bit7
                } else {                // NaN
                // quiet 位是 bit 22
                if (frac & 0x400000) {
                class = (1 << 9);    // quiet NaN: bit9
                } else {
                class = (1 << 8);    // signaling NaN: bit8
                }
                }
                } else if (exp == 0) {      // 指数全 0
                if (frac == 0) {        // 零
                class = sign ? (1 << 3) : (1 << 4);   // -0: bit3, +0: bit4
                } else {                // 非规约数
                class = sign ? (1 << 2) : (1 << 5);   // 负: bit2, 正: bit5
                }
                } else {                    // 规约数
                class = sign ? (1 << 1) : (1 << 6);       // 负: bit1, 正: bit6
                }
                cpu->gpr[rd] = class;
                if(log_enable){
                fprintf(stderr,"[fclass.s] f[%d]:%f,class:0x%02x\n",rd,f32_from_bits(a.v),class);
                }

                }
                break;
        }
        case 0b1111000: //fmv.w.x
        {
            if(rs2 == 0 && funct3 == 0){
                uint32_t origin_bits = cpu->gpr[rs1] & 0xFFFFFFFF;
                if (((origin_bits >> 23) == 0xff) && (origin_bits & 0x7fffff)) {
                    origin_bits = 0x7fc00000;
                }
                cpu->fgpr[rd] = 0xffffffff00000000ULL | (uint64_t)origin_bits;
            if(log_enable){
                fprintf(stderr,"[fmv.w.x] x[%d]:0x%16lx,f[%d]:0x%16lx\n",rd,cpu->gpr[rd],
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
                fprintf(stderr,"[fmv.x.d] x[%d]:0x%16lx,f[%d]:0x%16lx\n",rd,cpu->gpr[rd],
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
                fprintf(stderr,"[fmv.d.x] x[%d]:0x%16lx,f[%d]:0x%16lx\n",rd,cpu->gpr[rd],
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

    static bool is_wfi = false;
    if(cpu->privilege <= 1){
        if(cpu->csr[CSR_MIDELEG] & (1 << 5)){
            if(cpu->csr[CSR_SIE] & SIE_STIE){
                if(cpu->csr[CSR_SIP] & SIP_STIP)
                    cpu->pc += 4;
            }
        }
    }
    

    /*
    pthread_mutex_lock(&cpu->lock);
    cpu->halted = true;
    if(!is_wfi){
        fprintf(stderr,"[WFI]: No enabled interrupts pending j:%ld\n", j);
        is_wfi = true;
    }
    pthread_mutex_unlock(&cpu->lock);
    */
}
void exec_rem(CPU_State *cpu,uint32_t instr){ 
    uint64_t rs1 = (instr >> 15) & 0x1F;
    uint64_t rs2 = (instr >> 20) & 0x1F;
    uint64_t rd = (instr >> 7) & 0x1F;

    int64_t dividend = (int64_t)cpu->gpr[rs1];
    int64_t divisor  = (int64_t)cpu->gpr[rs2];
    int64_t remainder;

    if (divisor == 0) {
        remainder = dividend;
    } else if (dividend == INT64_MIN && divisor == -1) {
        remainder = 0;
    } else {
        remainder = dividend % divisor;
    }

    cpu->gpr[rd] = (uint64_t)remainder;
    if(log_enable){
        fprintf(stderr,"[rem] divided(rs1:%d):0x%16lx divisor(rs2:%d):0x%16lx,val(rd:%d):0x%16lx\n",
            rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2],rd,cpu->gpr[rd]);
    }
     cpu->pc += 4;

}

void exec_3b(CPU_State* cpu,uint32_t instr){
    uint8_t funct7 = (instr >> 25) & 0x7F;
    uint8_t funct3 = (instr >> 12) & 0x7;
    uint8_t rs2 = (instr >> 20) & 0x1F;
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rd = (instr >> 7) & 0x1F;

    if(funct7 == 0 && funct3 == 1){ //0b001 // sllw
        int32_t data = (cpu->gpr[rs1] & 0xFFFFFFFF);
        uint32_t shamt = (cpu->gpr[rs2] & 0x1F);
        int32_t imm = data << shamt;

        if(rd != 0){
            cpu->gpr[rd] = (int64_t)imm;
        }
        cpu->pc += 4;
        if(log_enable){
        fprintf(stderr,"[sllw] x[%d]:0x%08lx,x[%d]:0x%08lx,imm:0x%16x\n",
               rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2],imm);
        }
    }
    else if(funct7 == 1 && funct3 == 7 ){//0b111 //remuw
        uint32_t divided = (uint32_t)(cpu->gpr[rs1]);
        uint32_t divisor = (uint32_t)(cpu->gpr[rs2]);
        uint32_t value = 0;
        //remuw的被除数和除数都是无符号数，结果也是无符号数
        if(divisor == 0){
            value = divided;
        }else{  
            value = divided % divisor;
        }
        int64_t result = (int64_t)(int32_t)value;
        if(rd != 0){
            cpu->gpr[rd] = result;
        }
        cpu->pc += 4;
        if(log_enable){
            fprintf(stderr,"[remuw] divided(rs1:%d):0x%08lx divisor(rs2:%d):0x%08lx,val(rd:%d):0x%08lx\n",
                rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2],rd,result);
        }

        
    }else if(funct7 == 1 && funct3 == 0b110){ //remw
        int32_t divided = (int32_t)(cpu->gpr[rs1] & 0xFFFFFFFF);
        int32_t divisor = (int32_t)(cpu->gpr[rs2] & 0xFFFFFFFF);
        int32_t value = 0;

        if(divisor == 0){
            value = divided;
        }else if(divided == (int32_t)0x80000000 && divisor == -1){
            value = 0;
        }
        else{
            value = divided % divisor;
        }

        int64_t result = (int64_t)(int32_t)value;
        if(rd != 0){
            cpu->gpr[rd] = result;
        }
        cpu->pc += 4;

        if(log_enable){
            fprintf(stderr,"[remw] %%,sign-extended \n");
            fprintf(stderr,"[remw] divided(rs1:%d):0x%08lx divisor(rs2:%d):0x%08lx,val(rd:%d):0x%08lx\n",
                rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2],rd,result);
        }
    }
    else if(funct7 == 1 && funct3 == 5){//0b101 //divuw
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
            fprintf(stderr,"[divuw] divided(rs1:%d):0x%08lx divisor(rs2:%d):0x%08lx,val(rd:%d):0x%08lx\n",
                rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2],rd,result);
        }
    }else if(funct7 == 0 && funct3 == 0){ //addw
        int32_t rs1_val = (int32_t)cpu->gpr[rs1];
        int32_t rs2_val = (int32_t)cpu->gpr[rs2];
        int32_t val = rs1_val + rs2_val;
        int64_t result = (int64_t)val;

        if(rd != 0){
            cpu->gpr[rd] = result;
        }
        cpu->pc += 4;
    }else if(funct7 == 0b0100000 && funct3 == 0){ //subw
        int32_t rs1_val = (int32_t)cpu->gpr[rs1];
        int32_t rs2_val = (int32_t)cpu->gpr[rs2];
        int32_t val = rs1_val - rs2_val;
        int64_t result = (int64_t)val;
        if(rd != 0){
            cpu->gpr[rd] = result;
        }
        cpu->pc += 4;
        if(log_enable){
            fprintf(stderr,"[subw] x[%d]:0x%08lx = x[%d]:0x%08lx - x[%d]:0x%08lx\n",
                    rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2]);
        }
    }
    else if(funct7 == 0b0000001 && funct3 == 0b000){ //mulw
        int32_t val1 = (int32_t)(cpu->gpr[rs1] & 0xFFFFFFFF);
        int32_t val2 = (int32_t)(cpu->gpr[rs2] & 0xFFFFFFFF);

        int64_t mul_result = (int64_t)val1 * (int64_t)val2;
        int64_t result = (int64_t)(int32_t)(mul_result & 0xFFFFFFFF);

        if(rd != 0){
            cpu->gpr[rd] = (uint64_t)result;
        }
        cpu->pc += 4;

        if(log_enable){
            fprintf(stderr,"[mulw] x[%d]:0x%08lx = x[%d]:0x%08lx * x[%d]:0x%08lx\n",
                    rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2]);
        }

    }else if(funct7 == 0b0000001 && funct3 == 0b100){ //divw
        int32_t divided = (int32_t)(cpu->gpr[rs1] & 0xFFFFFFFF);
        int32_t divisor = (int32_t)(cpu->gpr[rs2] & 0xFFFFFFFF);
        int32_t value = 0;

        if(divisor == 0){
            value = 0xFFFFFFFF;
        }else if(divided == (int32_t)0x80000000 && divisor == -1){
            value = 0x80000000;
        }
        else{
            value = divided / divisor;
        }

        int64_t result = (int64_t)(int32_t)value;
        if(rd != 0){
            cpu->gpr[rd] = result;
        }
        cpu->pc += 4;

        if(log_enable){
            fprintf(stderr,"[divw] divided(rs1:%d):0x%08lx divisor(rs2:%d):0x%08lx,val(rd:%d):0x%08lx\n",
                rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2],rd,result);
        }
    }else if(funct7 == 0b0100000 && funct3 == 0b101){//sraw
        int32_t data = (int32_t)(cpu->gpr[rs1] & 0xFFFFFFFF);
        uint32_t shamt = (cpu->gpr[rs2] & 0x1F);
        int32_t imm = data >> shamt;
        if(rd != 0){
            cpu->gpr[rd] = (int64_t)imm;
        }
        cpu->pc += 4;
        if(log_enable){
        fprintf(stderr,"[sraw] x[%d]:0x%08lx,x[%d]:0x%08lx,imm:0x%16x\n",
               rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2],imm);
        }
    }else if(funct7 == 0 && funct3 == 0b101){//srlw  
        int32_t data = (int32_t)(cpu->gpr[rs1] & 0xFFFFFFFF);
        uint32_t shamt = (cpu->gpr[rs2] & 0x1F);
        int32_t imm = (uint32_t)data >> shamt;

        if(rd != 0){
            cpu->gpr[rd] = (int64_t)imm;
        }
        cpu->pc += 4;
        if(log_enable){
        fprintf(stderr,"[srlw] x[%d]:0x%08lx,x[%d]:0x%08lx,imm:0x%16x\n",
               rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2],imm);
        }
    }
    else{
        if(log_enable){
            fprintf(stderr,"Unknown 3b instruction at pc:0x%08lx funct7=0x%02x,funct3=0x%01x\n",cpu->pc,funct7,funct3);
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
    fprintf(stderr,"[srl] x[%d]:0x%16lx,shamt:0x%08x,x[%d]:0x%16lx\n",
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
    fprintf(stderr,"[remu] x[%d]:0x%016lx,x[%d]:0x%016lx,x[%d]:0x%016lx\n",
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
    fprintf(stderr,"[divu] x[%d]:0x%016lx,x[%d]:0x%016lx,x[%d]:0x%016lx\n",
            rd,cpu->gpr[rd],rs1,cpu->gpr[rs2],rd,cpu->gpr[rs2]
    );
    }
}

void exec_slt(CPU_State *cpu,uint32_t instr){
    uint8_t rd = (instr >> 7) & 0x1F;
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rs2 = (instr >> 20) & 0x1F;

    if(rd != 0){
        cpu->gpr[rd] = ((int64_t)cpu->gpr[rs1] < (int64_t)cpu->gpr[rs2]) ? 1 : 0;
    }

    cpu->pc += 4;
    if(log_enable){
    fprintf(stderr,"[slt] x[%d]:0x%016lx,x[%d]:0x%016lx,x[%d]:0x%016lx\n",
            rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2]
    );
    }
}
void exec_mulh(CPU_State *cpu,uint32_t instr){
    uint8_t rd = (instr >> 7) & 0x1F;
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rs2 = (instr >> 20) & 0x1F;

    if(rd != 0){
        int64_t val1 = (int64_t)cpu->gpr[rs1];
        int64_t val2 = (int64_t)cpu->gpr[rs2];
        __int128 mul_result = (__int128)val1 * (__int128)val2;
        cpu->gpr[rd] = (uint64_t)(mul_result >> 64);
    }

    cpu->pc += 4;
    if(log_enable){
    fprintf(stderr,"[mulh] x[%d]:0x%016lx,x[%d]:0x%016lx,x[%d]:0x%016lx\n",
            rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2]
    );
    }
}

void exec_mulhsu(CPU_State *cpu,uint32_t instr){
    uint8_t rd = (instr >> 7) & 0x1F;
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rs2 = (instr >> 20) & 0x1F;

    if(rd != 0){
        int64_t val1 = (int64_t)cpu->gpr[rs1];
        uint64_t val2 = cpu->gpr[rs2];
        __int128 mul_result = (__int128)val1 * (__int128)val2;
        cpu->gpr[rd] = (uint64_t)(mul_result >> 64);
    }

    cpu->pc += 4;
    if(log_enable){
    fprintf(stderr,"[mulhsu] x[%d]:0x%016lx,x[%d]:0x%016lx,x[%d]:0x%016lx\n",
            rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2]
    );
    }
}

void exec_mulhu(CPU_State *cpu,uint32_t instr){
    uint8_t rd = (instr >> 7) & 0x1F;
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rs2 = (instr >> 20) & 0x1F;

    if(rd != 0){
        uint64_t val1 = cpu->gpr[rs1];
        uint64_t val2 = cpu->gpr[rs2];
        __uint128_t mul_result = (__uint128_t)val1 * (__uint128_t)val2;
        cpu->gpr[rd] = (uint64_t)(mul_result >> 64);
    }

    cpu->pc += 4;
        if(log_enable){
        fprintf(stderr,"[mulhu] x[%d]:0x%016lx,x[%d]:0x%016lx,x[%d]:0x%016lx\n",
                rd,cpu->gpr[rd],rs1,cpu->gpr[rs1],rs2,cpu->gpr[rs2]
        );
    }
}

void exec_sra(CPU_State *cpu,uint32_t instr){
    uint8_t rd = (instr >> 7) & 0x1F;
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rs2 = (instr >> 20) & 0x1F;

    uint8_t shamt = cpu->gpr[rs2] & 0x3F;

    if(rd != 0){
        cpu->gpr[rd] = (int64_t)cpu->gpr[rs1] >> shamt ;
    }
    cpu->pc += 4;
    if(log_enable){
    fprintf(stderr,"[sra] x[%d]:0x%16lx,shamt:0x%08x,x[%d]:0x%16lx\n",
           rs1,cpu->gpr[rs1],shamt,rd,cpu->gpr[rd] );
    }
}

void exec_sret(CPU_State *cpu,uint32_t instr){
    
    if(cpu->privilege < 1){
        return;
    }

    // 1. 从 sepc 寄存器获取返回地址 -> pc
    uint64_t sepc = 0;
    if(cpu->v == false)
        sepc = cpu->csr[CSR_SEPC];
    else
        sepc = cpu->vsepc;

    if(log_enable){
        fprintf(stderr,"[sret] sepc:0x%16lx\n",sepc);
    }

    cpu->pc = sepc;

    // 2. 从 sstatus 寄存器spp恢复特权级
    uint64_t sstatus = 0;
    if(cpu->v == false)
        sstatus = cpu->csr[CSR_SSTATUS];
    else
        sstatus = cpu->vsstatus;
    
    uint64_t spp = (sstatus >> 8) & 0x1;  // SPP 位
    cpu->privilege = (spp == 1) ? 1 : 0;

    if(cpu->v == false){
        cpu->v = !!(cpu->csr[HSTATUS] & HSTATUS_SPV);
        cpu->csr[HSTATUS] &= ~(HSTATUS_SPV);
    }

     // 3.清除 SPP 位（设置为 0，表示来自 U 模式）
    sstatus &= ~(1L << 8);
  
    //  4. 从 sstatus 寄存器spie恢复 SIE 位（SPIE → SIE）
    uint64_t spie = (sstatus >> 5) & 0x1;  // SPIE 位
    sstatus &= ~(1L << 1);  // 清除 SIE 位
    sstatus |= (spie << 1); // 用 SPIE 恢复 SIE

     // 5.清除 SPIE 位
    sstatus &= ~(1L << 5);
    
    
    if(log_enable){
        printf("[sret] pri:%d,V:%d\n",cpu->privilege,cpu->v);
    }

    // 更新 sstatus
    if(cpu->v == false)
        cpu->csr[CSR_SSTATUS] = sstatus;
    else
        cpu->vsstatus = sstatus;

}


void exec_flw(CPU_State *cpu,uint32_t instr){
    uint8_t rd  = (instr >> 7) & 0x1F;
    uint8_t rs1 = (instr >> 15) & 0x1F;

    
    int32_t imm = (int32_t)instr >> 20;

    int64_t addr = (int64_t)cpu->gpr[rs1] + imm;

    if(log_enable){
        fprintf(stderr,"[before flw] Loading float value from address: 0x%16lx into f[%d]\n", addr, rd);
    }

    int64_t pa = get_pa(cpu, addr, ACC_LOAD);
    if(pa == 0) return;
    if(log_enable){
        fprintf(stderr,"[flw] Physical address: 0x%16lx\n", pa);
    }

    uint32_t data = bus_read(&cpu->bus, pa, 4);
    
    if (rd != 0) {
        cpu->fgpr[rd] = 0xffffffff00000000ULL | (uint64_t)data;
    }

    cpu->pc += 4;
    if(log_enable){
        fprintf(stderr,"[flw] Loaded float value: 0x%16lx from address: 0x%16lx into f[%d]\n",
                cpu->fgpr[rd], addr, rd);
    }
 
}

void exec_43(CPU_State *cpu,uint32_t instr){//fmadd.s

    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rs2 = (instr >> 20) & 0x1F;
    uint8_t rs3 = (instr >> 27) & 0x1F;
    uint8_t rd = (instr >> 7) & 0x1F;
    uint8_t funct3 = (instr >> 12) & 0x7;

    // 1. 取三个源操作数（NaN‑boxing 截低 32 位）
    float32_t a = f32_unbox(cpu->fgpr[rs1]);
    float32_t b = f32_unbox(cpu->fgpr[rs2]);
    float32_t c = f32_unbox(cpu->fgpr[rs3]);

    // 2. 确定有效舍入模式
    uint8_t rm = funct3;
    if (rm == 0x7) {
        rm = (cpu->csr[CSR_FCSR] >> 5) & 0x7;  // 动态舍入
    }
    softfloat_roundingMode = rm_to_softfloat(rm);

    // 3. 清异常，执行融合乘加
    softfloat_exceptionFlags = 0;
    float32_t res = f32_mulAdd(a, b, c);

    // 4. 结果 NaN‑box 写入 f[rd]
    cpu->fgpr[rd] = f32_box(res.v);

    // 5. 更新 fflags（可能产生 NV, NX, OF, UF, DZ 等）
    fp_update_fflags(cpu);
    cpu->pc += 4;
}

void exec_4f(CPU_State *cpu,uint32_t instr){//fnmadd.s
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rs2 = (instr >> 20) & 0x1F;
    uint8_t rs3 = (instr >> 27) & 0x1F;
    uint8_t rd = (instr >> 7) & 0x1F;
    uint8_t funct3 = (instr >> 12) & 0x7;

    // 1. 取三个源操作数（NaN‑boxing）
    float32_t a = f32_unbox(cpu->fgpr[rs1]);
    float32_t b = f32_unbox(cpu->fgpr[rs2]);
    float32_t c = f32_unbox(cpu->fgpr[rs3]);

    // 2. 确定有效舍入模式
    uint8_t rm = funct3;
    if (rm == 0x7) {
        rm = (cpu->csr[CSR_FCSR] >> 5) & 0x7;  // 动态舍入
    }
    softfloat_roundingMode = rm_to_softfloat(rm);

    // 3. 清异常，执行融合乘加 a*b + c
    softfloat_exceptionFlags = 0;
    float32_t res = f32_mulAdd(a, b, c);

    // 4. 取负：翻转符号位（对 NaN 也可）
    res.v ^= 0x80000000;

    // 5. 结果 NaN‑box 写入 f[rd]
    cpu->fgpr[rd] = f32_box(res.v);

    // 6. 更新 fflags
    fp_update_fflags(cpu);

    cpu->pc += 4;
}

void exec_47(CPU_State *cpu,uint32_t instr){ //fmsub.s

    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rs2 = (instr >> 20) & 0x1F;
    uint8_t rs3 = (instr >> 27) & 0x1F;
    uint8_t rd = (instr >> 7) & 0x1F;
    uint8_t funct3 = (instr >> 12) & 0x7;

    // 1. 取三个源操作数（NaN‑boxing）
    float32_t a = f32_unbox(cpu->fgpr[rs1]);
    float32_t b = f32_unbox(cpu->fgpr[rs2]);
    float32_t c = f32_unbox(cpu->fgpr[rs3]);

    // 2. 对 c 取负（翻转符号位，对 0/NaN 均安全）
    c.v ^= 0x80000000;

    // 3. 确定有效舍入模式
    uint8_t rm = funct3;
    if (rm == 0x7) {
        rm = (cpu->csr[CSR_FCSR] >> 5) & 0x7;
    }
    softfloat_roundingMode = rm_to_softfloat(rm);

    // 4. 清异常，执行融合操作 a*b + (-c)
    softfloat_exceptionFlags = 0;
    float32_t res = f32_mulAdd(a, b, c);

    // 5. NaN‑box 后写入 f[rd]
    cpu->fgpr[rd] = f32_box(res.v);

    // 6. 更新 fflags
    fp_update_fflags(cpu);

    cpu->pc += 4;
}

void exec_4b(CPU_State *cpu,uint32_t instr){//fnmsub.s
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rs2 = (instr >> 20) & 0x1F;
    uint8_t rs3 = (instr >> 27) & 0x1F;
    uint8_t rd = (instr >> 7) & 0x1F;
    uint8_t funct3 = (instr >> 12) & 0x7;

    // 1. 取三个源操作数（NaN‑boxing）
    float32_t a = f32_unbox(cpu->fgpr[rs1]);
    float32_t b = f32_unbox(cpu->fgpr[rs2]);
    float32_t c = f32_unbox(cpu->fgpr[rs3]);

    // 2. 对 a 取负（翻转符号位，对 0/NaN 均安全）
    a.v ^= 0x80000000;

    // 3. 确定有效舍入模式
    uint8_t rm = funct3;
    if (rm == 0x7) {
        rm = (cpu->csr[CSR_FCSR] >> 5) & 0x7;
    }
    softfloat_roundingMode = rm_to_softfloat(rm);

    // 4. 清异常，执行融合操作 (-a)*b + c
    softfloat_exceptionFlags = 0;
    float32_t res = f32_mulAdd(a, b, c);

    // 5. NaN‑box 后写入 f[rd]
    cpu->fgpr[rd] = f32_box(res.v);

    // 6. 更新 fflags
    fp_update_fflags(cpu);

    cpu->pc += 4;
}


void exec_27(CPU_State *cpu,uint32_t instr){
    uint8_t rs1 = (instr >> 15) & 0x1F;
    uint8_t rs2 = (instr >> 20) & 0x1F;
    uint8_t funct3 = (instr >> 12) & 0x7;
    uint16_t imm12 = ((instr >> 7) & 0x1F) |
                    ((instr >> 25) & 0x3F) << 5;
    int64_t imm = (int64_t)(int32_t)imm12;

    uint64_t addr = cpu->gpr[rs1] + imm; 

    uint64_t pa = get_pa(cpu, addr, ACC_STORE);
    if(pa == 0) return;

    if(funct3 == 0b010){ //fsw

    //  从浮点寄存器中取出 32 位值（NaN‑boxing 截低 32 位）
    uint32_t data = (uint32_t)cpu->fgpr[rs2];         // 直接取低 32 位
    
    bus_write(&cpu->bus,pa,data,4);
    if(log_enable){
        fprintf(stderr,"[fsw] addr:0x%16lx,data:0x%08x\n",addr,data);
    }
    }else if(funct3 == 0b011){ //fsd

        //  从浮点寄存器中取出 64 位值
        uint64_t data = cpu->fgpr[rs2];    

        bus_write(&cpu->bus,pa,data,8);
        if(log_enable){
            fprintf(stderr,"[fsd] addr:0x%16lx,data:0x%016lx\n",addr,data);
        }
    }
    cpu->pc += 4;
}

void exec_hfence(CPU_State* cpu,uint32_t instr){

    uint8_t rs2 = (instr >> 7) & 0x1F;
    uint8_t vmid = cpu->gpr[rs2];

     for (int i = 0; i < TLB_SIZE; i++) {
        TLBEntry *e = &cpu->tlb.entries[i];
        if (!e->valid) continue;

        // 匹配 VMID（若 vmid=0 表示全局刷新，所有包含该 vmid 的条目均失效）
        if (vmid != 0 && e->vmid != vmid) continue;
        // 若指定了 GPA，需进一步检查：但由于条目存的是 GVA，无法直接匹配 GPA，可以忽略。
        // 简单实现：只要 vmid 匹配，全部刷掉，以确保正确性。
        e->valid = 0;
     }

     cpu->pc += 4;
}