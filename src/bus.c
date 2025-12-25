
#include "bus.h"
#include "memory.h"

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
    printf("[BUS] MMIO registered: base=0x%x size=0x%lx\n", base, size);
}

// bus.c
uint64_t bus_read(Bus *bus, uint64_t addr, unsigned size) {
    for (int i = 0; i < bus->region_count; i++) {
        MMIORegion *r = &bus->regions[i];
        if (addr >= r->base && addr < r->base + r->size) {
            uint64_t offset = addr - r->base;
           // printf("[bus_read] offset:0x%16lx,size:%d\n",offset,size);
            return r->read(r->opaque, offset, size);
        }
    }
    // 默认从内存读取
    /*
    extern uint8_t *memory;
    uint64_t val = 0;
    memcpy(&val, &memory[addr - MEMORY_BASE], size);
    return val; 
    */
}

void bus_write(Bus *bus, uint64_t addr, uint64_t val, unsigned size) {
    for (int i = 0; i < bus->region_count; i++) {
        MMIORegion *r = &bus->regions[i];
        if (addr >= r->base && addr < r->base + r->size) {
            uint64_t offset = addr - r->base;
            r->write(r->opaque, offset, val, size);
            //printf("[bus write] offset:0x%08x,val:0x%08x,size:%d\n",offset,val,size);
            return;
        }
    }
    // 默认写内存
    /*
    extern uint8_t *memory;
    memcpy(&memory[addr - MEMORY_BASE], &val, size);*/
}
