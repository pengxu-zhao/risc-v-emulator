#ifndef MMU_H
#define MMU_H

#include "common.h"
#include "cpu.h"


int sv39_translate(CPU_State *cpu, uint64_t va, int acc_type, uint64_t *out_pa,uint8_t* flags);
int sv32_translate(CPU_State *cpu, uint64_t va, int acc_type, uint64_t *out_pa,uint8_t* flags);
int tlb_lookup(CPU_State *cpu, uint64_t va, int acc,uint64_t *pa,uint16_t asid);
void handle_page_fault(CPU_State *cpu, uint64_t va, int acc);
void map_vaddr_to_paddr(CPU_State* cpu,uint64_t vaddr,uint64_t paddr,uint64_t size,uint8_t flags,uint16_t asid);
void tlb_flush(CPU_State* cpu);
void init_page_table(CPU_State *cpu);
uint64_t phys_read_u32(CPU_State *cpu, uint64_t pa);
void phys_write_u32(CPU_State *cpu, uint64_t pa, uint64_t v);
uint64_t get_pa(CPU_State *cpu,uint64_t vaddr,int acc_type);
#endif