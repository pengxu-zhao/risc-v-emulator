
#include "bus.h"
#include "cache.h"
#include "cpu.h"
extern int j;
extern CPU_State cpu[MAX_CORES];
extern *L1;
extern uint8_t *memory;
extern int rv_exit;

void bus_register_mmio(Bus *bus, uint64_t base, uint64_t size,
                       uint64_t (*read)(void*, uint64_t, unsigned),
                       void (*write)(void*, uint64_t, uint64_t, unsigned),
                       void *opaque)
{
    int n = bus->region_count++;
    bus->regions[n].base = base;
    bus->regions[n].size = size;
    bus->regions[n].read = read;
    bus->regions[n].write = write;
    bus->regions[n].opaque = opaque;
    //printf("[BUS] MMIO registered: base=0x%x size=0x%lx\n", base, size);
}

// bus.c
uint64_t bus_read(Bus *bus, uint64_t addr, unsigned size) {
    for (int i = 0; i < bus->region_count; i++) {
        MMIORegion *r = &bus->regions[i];
        if (addr >= r->base && addr < r->base + r->size) {
            if(bus->cache_enabled && r->base == MEMORY_BASE){
                return cache_read(L1, addr, size);
            }
            uint64_t offset = addr - r->base;
            return r->read(r->opaque, offset, size);
        }
    }
    if(addr > MEMORY_BASE && addr + size - 1 < MEMORY_BASE + MEMORY_SIZE){
        uint64_t val = 0;
        memcpy(&val, &memory[addr - MEMORY_BASE], size);
        return val; 
    }
    //printf("[bus_read]addr:0x%16lx not in any mmio region\n",addr);
}


void bus_write(Bus *bus, uint64_t addr, uint64_t val, unsigned size) {
    for (int i = 0; i < bus->region_count; i++) {
        MMIORegion *r = &bus->regions[i];
        if (addr >= r->base && addr < r->base + r->size) {
            if(bus->cache_enabled && r->base == MEMORY_BASE){
                cache_write(L1, addr, val, size);
                return;
            }
            uint64_t offset = addr - r->base;
            r->write(r->opaque, offset, val, size);
            return;
        }
    }


    // 默认写内存
    if(addr >= MEMORY_BASE && addr + size - 1 < MEMORY_BASE + MEMORY_SIZE){
        memcpy(&memory[addr - MEMORY_BASE], &val, size);
        return;
    }

    printf("[bus_write]addr:0x%16lx not in any mmio region j:%d,pc:0x%16lx\n",addr,j,cpu[0].pc);

    //cpu[0].halted = true; // 遇到非法访问时停止 CPU
}
