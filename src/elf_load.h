#ifndef ELF_LOAD_H
#define ELF_LOAD_H

#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <elf.h>
#include <errno.h>
#include "cpu.h"
#include "memory.h"
#include "mmu.h"

int load_elf32_bare(const char *path, uint8_t *mem, size_t mem_size, uint32_t mem_base, CPU_State *cpu);
void load_elf32_virt(CPU_State* cpu,const char *filename, uint32_t *entry_point);

int load_elf64_SBI(const char *filename, uint64_t *entry_point) ;
#endif