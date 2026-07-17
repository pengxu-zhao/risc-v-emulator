#ifndef CACHE_H
#define CACHE_H
#include "common.h"

#define LINE_SIZE   64
#define CACHE_SIZE  (4 * 1024)   // 4KB L1
#define WAYS        4
#define SETS        (CACHE_SIZE / LINE_SIZE / WAYS)

#define OFFSET_BITS 6
#define INDEX_BITS  (12 - OFFSET_BITS) 

/*
    addr[5:0] -> cache line offset
    addr[6:11] -> cache line index
*/

typedef struct cache_line {
    bool valid;
    bool dirty;
    uint64_t tag;
    uint8_t data[LINE_SIZE];
} cache_line_t;

typedef struct cache_set {
        cache_line_t *lines;
    }cache_set_t;

typedef struct cache {
    int level;          // 1 / 2 / 3

    int sets;
    int ways;
    int line_size;

    int hit;
    int miss;

    cache_set_t *set;

    struct cache *next; // 下一级 cache 或 memory
} cache_t;



void init_cache();
uint64_t cache_read(cache_t *c, uint64_t addr,uint8_t size);
void cache_write(cache_t *c, uint64_t addr, uint64_t value,uint8_t size);

#endif