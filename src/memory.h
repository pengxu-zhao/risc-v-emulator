
// src/memory.h
#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#include <stddef.h>
#include "common.h"
typedef struct {
    uint8_t *data;
    uint64_t size;
    // 挂起的内存操作
    struct {
        uint64_t *load_addrs;
        int load_count;
        int load_capacity;
        
        struct {
            uint64_t addr;
            uint64_t value;
            int size;
            bool valid;
        } *store_ops;
        int store_count;
        int store_capacity;
    } pending;
} RAMDevice;


// 内存操作函数
void init_memory();
void write32(uint64_t addr,uint64_t val);
uint64_t memory_read(uint8_t* memory, uint64_t address, size_t size);
void memory_write(uint8_t* memory, uint64_t address, uint64_t value, size_t size);
void memory_load_binary(uint8_t* memory, const char* filename, uint64_t load_address);

// 地址转换函数
static inline uint64_t physical_address(uint64_t virtual_addr) {
    return virtual_addr - MEMORY_BASE;
}

static inline int is_valid_address(uint64_t address) {
    return address >= MEMORY_BASE && address < (MEMORY_BASE + MEMORY_SIZE);
}

uint64_t ram_read(void *opaque, uint64_t offset, unsigned size);
void ram_write(void *opaque, uint64_t offset, uint64_t value, unsigned size);
void* kalloc_sim(RAMDevice* ram);
void memory_sync_read(void *opaque, uint64_t addr);
void memory_sync_write(void *opaque, uint64_t addr, uint64_t value);
void memory_synchronize(void* opaque);



#endif // MEMORY_H