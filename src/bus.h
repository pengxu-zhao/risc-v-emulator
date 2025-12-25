#ifndef BUS_H
#define BUS_H

#include <inttypes.h>

typedef struct {
    uint64_t base;     // 起始物理地址
    uint64_t size;     // 区间大小
    void *opaque;      // 指向设备对象 (比如 UARTDevice*)
    uint64_t (*read)(void *opaque, uint64_t offset, unsigned size);
    void (*write)(void *opaque, uint64_t offset, uint64_t value, unsigned size);
} MMIORegion;

#define MAX_MMIO_REGIONS 8

typedef struct {
    MMIORegion regions[MAX_MMIO_REGIONS];
    int region_count;
} Bus;


void bus_register_mmio(Bus *bus, uint64_t base, uint64_t size,
                       uint64_t (*read)(void*, uint64_t, unsigned),
                       void (*write)(void*, uint64_t, uint64_t, unsigned),
                       void *opaque);
uint64_t bus_read(Bus *bus, uint64_t addr, unsigned size);
void bus_write(Bus *bus, uint64_t addr, uint64_t val, unsigned size);
#endif