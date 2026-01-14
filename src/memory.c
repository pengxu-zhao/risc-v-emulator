// src/memory.c
#include "memory.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "emulator_api.h"
#include "cpu.h"

uint8_t* memory = NULL;
extern int log_enable;

void init_memory(){

    // 分配内存
     memory = (uint8_t*)malloc(MEMORY_SIZE);

   // memory = (uint8_t*)aligned_alloc(4096, MEMORY_SIZE);
    if (!memory) {
        printf("Failed to allocate %ld GB memory\n", MEMORY_SIZE / (1024 * 1024 * 1024));
        return ;
    }
    
    if (!memory) {
        printf("Failed to allocate aligned memory\n");
        return;
    }

    // 初始化内存为0
    printf("Zeroing memory...\n");
    memset(memory, 0, MEMORY_SIZE);
    printf("[0x100000]:0x%08lx\n",memory[0x100000]);
}
extern CPU_State cpu[MAX_CORES];
extern int j;
uint64_t memory_read(uint8_t* memory, uint64_t address, size_t size) {
    if (memory == NULL) {
        printf("Read ERROR: memory pointer is NULL!\n");
        return 0;
    }
        
    uint64_t offset = 0;
    if(address >= MEMORY_BASE){
        offset = address - MEMORY_BASE; //physical_address(address);
    }else if(address >= 0x10001000ULL){
        offset = address - 0x10001000ULL;    
    }else if(address >= CLINT_BASE_ADDR){
        offset = address - CLINT_BASE_ADDR;
    }else if(address >= PLIC_BASE){
        offset = address - PLIC_BASE;
    }


    if (offset + size > MEMORY_SIZE) {
        printf("fetch Read ERROR: Memory read out of bounds: address=0x%08x, offset=0x%08x, size=%zu\n", 
               address, offset, size);
        printf("j:%ld\n",j);
        cpu[0].halted = true;
        return 0;
    }
    
    uint64_t value = 0;
    //memcpy(&value, memory + phys_addr, size);
     for (int i = 0; i < size; i++) {
        value |=  (memory[offset+i] & 0xFF ) << (i * 8);  // 小端序
    }


    return value;
}

void memory_write(uint8_t* memory, uint64_t address, uint64_t value, size_t size) {
    if (memory == NULL) {
        printf("ERROR: memory pointer is NULL!\n");
        return;
    }
    
    uint64_t phys_addr = 0;
    if(address >= MEMORY_BASE){
        phys_addr = address - MEMORY_BASE; //physical_address(address);
    }else if(address >= 0x10001000ULL){
        phys_addr = address - 0x10001000ULL;    
    }

    uint64_t vl = phys_addr + size;
    printf("vl:0x%llx\n",vl);
    if (phys_addr + size > MEMORY_SIZE) {
        printf("ERROR: Memory write out of bounds: address=0x%llx, phys=0x%08x, size=%zu\n", 
               address, phys_addr, size);

        return;
    }
    memcpy(memory + phys_addr, &value, size);
    
}

void memory_load_binary(uint8_t* memory, const char* filename, uint64_t load_address) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        printf("Failed to open file: %s\n", filename);
        return;
    }
    
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    uint64_t phys_addr = physical_address(load_address);
    if (phys_addr + (size_t)file_size > MEMORY_SIZE) {
        printf("ERROR: Binary too large for memory: load_address=0x%08x, size=%ld\n", 
               load_address, file_size);
        fclose(file);
        return;
    }
    
    size_t bytes_read = fread(&memory[phys_addr], 1, (size_t)file_size, file);
    fclose(file);
    
    if (bytes_read == (size_t)file_size) {
        printf("Loaded %ld bytes from %s to 0x%08x (phys: 0x%08x)\n", 
               file_size, filename, load_address, phys_addr);
    } else {
        printf("Failed to read all bytes from %s\n", filename);
    }
}


uint64_t ram_read(void *opaque, uint64_t offset, unsigned size) {
    RAMDevice *ram = (RAMDevice *)opaque;
    uint64_t val = 0;
    if (offset + size > ram->size) {
        printf("[RAM] read out of range: offset=0x%lx size=%u\n", offset, size);
        return 0;
    }

    if(offset >= ram->size){
        fprintf(stderr, "ERROR: RAM read at offset 0x%lx exceeds RAM size 0x%lx\n",
                offset, ram->size);
        return 0;
    }
    COMPILER_BARRIER();
    for(int i = 0; i < size; i++){
        uint64_t vl = 0;
        vl = ram->data[offset+i];
        val |= (vl << 8*i);
    }

    return val;
}

void ram_write(void *opaque, uint64_t offset, uint64_t value, unsigned size) {
    RAMDevice *ram = (RAMDevice *)opaque;
    if (offset + size > ram->size) {
        printf("[RAM] write out of range: offset=0x%lx size=%u\n", offset, size);
        return;
    }

    for(int i = 0; i < size; i++ ){
        uint8_t val =  (value >> 8*i) & 0xFF;
        ram->data[offset+i] = val;
      //  printf("ram->data[0x%08lx]:0x%08lx\n",offset+i,val);
    }

    COMPILER_BARRIER();
}


