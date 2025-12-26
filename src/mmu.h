#ifndef MMU_H
#define MMU_H
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "cpu.h"
#include "memory.h"

#define PAGE_SIZE        4096u
#define PAGE_OFFSET_BITS 12u
#define VPN_BITS         10u     // per-level for Sv32
#define PTE_SIZE         4u      // bytes

#define SV32_LEVELS      2
#define SV39_LEVELS      3

// PTE flag bits (Sv32): V R W X U G A D (low bits)
#define PTE_V  (1u << 0)
#define PTE_R  (1u << 1)
#define PTE_W  (1u << 2)
#define PTE_X  (1u << 3)
#define PTE_U  (1u << 4)
#define PTE_G  (1u << 5)
#define PTE_A  (1u << 6)
#define PTE_D  (1u << 7)

// satp mode for RV32: 0 = BARE, 1 = SV32
#define SATP_MODE_MASK   (1u << 31)
#define SATP_PPN_MASK    ((1u << 22) - 1)//= 0x003fffff  // RV32: PPN is bits [21:0]


enum {  
    ACC_FETCH = 0, //取指
    ACC_LOAD = 1, //读
    ACC_STORE = 2 //写
};

enum {
    MMU_OK = 0,
    MMU_FAULT_PAGE,    // page-fault (permissions / invalid PTE / misaligned superpage / A/D)
    MMU_FAULT_ACCESS,  // access-fault (PMA/PMP) - emulator may treat same as page-fault
    MMU_FAULT_PASSTHRU // used internally
};

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