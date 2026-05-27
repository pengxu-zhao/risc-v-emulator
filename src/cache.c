#include "cache.h"
#include "bus.h"

extern uint8_t* memory;

cache_t *L1;
cache_t *L2;
cache_t *L3;

static void mem_read_block(uint64_t addr, uint8_t *buf, int len)
{
    memcpy(buf, memory + (addr - MEMORY_BASE), len);
}

static void mem_write_block(uint64_t addr, uint8_t *buf, int len)
{
    memcpy(memory + (addr - MEMORY_BASE), buf, len);
}


static inline int log2_int(int x)
{
    int r = 0;

    while ((1 << r) < x)
        r++;

    return r;
}

static inline uint64_t _offset(struct cache *c, uint64_t addr)
{
    return addr & (c->line_size - 1);
}

static inline uint64_t _index(struct cache *c, uint64_t addr)
{   
    int offset_bits = log2_int(c->line_size);
    int index_bits  = log2_int(c->sets);
    return (addr >> offset_bits) & ((1ULL << index_bits) - 1);
}

static inline uint64_t _tag(struct cache *c, uint64_t addr)
{
    int offset_bits = log2_int(c->line_size);
    int index_bits  = log2_int(c->sets);
    return addr >> (offset_bits + index_bits);
}

static cache_t *cache_create(int level,
                             int cache_size,
                             int ways,
                             int line_size)
{
    cache_t *c = calloc(1, sizeof(cache_t));

    c->level = level;

    c->ways = ways;
    c->line_size = line_size;

    c->sets = cache_size / (ways * line_size);

    c->set = calloc(c->sets, sizeof(cache_set_t));

    for (int i = 0; i < c->sets; i++) {
        c->set[i].lines = calloc(ways, sizeof(cache_line_t));
    }

    return c;
}

static struct cache_line_t *find_line(cache_t *c,
                                     uint64_t index,
                                     uint64_t tag)
{
    cache_set_t *set = &c->set[index];

    for (int i = 0; i < c->ways; i++) {
        cache_line_t *line = &set->lines[i];
        if (line->valid && line->tag == tag) {
            return line ;
        }
    }
    return NULL;
}

static struct cache_line_t *select_victim(cache_t *c,uint64_t index)
{
    cache_set_t *set = &c->set[index];
    for (int i = 0; i < c->ways; i++) {
        
        if (!set->lines[i].valid) {
            return &set->lines[i];
        }
    }
    int way = rand() % c->ways;
    return &set->lines[way];
}

static uint64_t cache_read_block(cache_t *c, uint64_t base, uint8_t *buffer,uint8_t size) {

    uint64_t val = 0;
    // lowest level cache, directly read from memory
    if(!c){
       // printf("cache read block all level miss: base=0x%16lx\n", base);
        memcpy(&val,memory + (base - MEMORY_BASE), size);
        return val;
    }

    uint64_t offset = _offset(c, base);
    uint64_t index = _index(c, base);
    uint64_t tag = _tag(c, base);

    cache_line_t *line = find_line(c, index, tag);

    if(line) {
        // Cache hit
        c->hit++;
       // printf("cache read block hit: level=%d index=%lu tag=0x%lx\n", c->level, index, tag);
        memcpy(buffer, line->data, c->line_size);
        return 0;
    }

    c->miss++;
    // Cache miss, read from next level
    cache_read_block(c->next, base, buffer, size);
    // Update cache line
    line = select_victim(c, index);
    //write-back
    if(line->valid && line->dirty) {
        uint64_t victim_addr = 
            (line->tag << (log2_int(c->line_size) + log2_int(c->sets))) | (index << log2_int(c->line_size));
        uint64_t old_base = victim_addr & ~(c->line_size - 1);
        mem_write_block(old_base, line->data, c->line_size);
    }
    //fill new line
    mem_read_block(base & ~(c->line_size - 1), line->data, c->line_size);
    line->valid = true;
    line->dirty = false;
    line->tag = tag;

}

uint64_t cache_read(cache_t *c, uint64_t addr,uint8_t size){ 
    uint64_t idx = _index(c, addr);
    uint64_t tag = _tag(c, addr);
    uint64_t off = _offset(c, addr);
    uint64_t val = 0;
    cache_line_t *line = find_line(c, idx, tag);

    if (line) {
        c->hit++;
       // printf("cache read hit: level=%d idx=%lu tag=0x%lx\n", c->level, idx, tag);
        for(int i = 0; i < size; i++) {
            val |= ((uint64_t)line->data[off + i]) << (i * 8);
        }

        return val;
    }
    c->miss++;

    uint8_t buf[LINE_SIZE];

    uint64_t base = addr & ~(LINE_SIZE - 1);

    cache_read_block(c->next, base, buf, size);
    cache_line_t *victim = select_victim(c, idx);

    // write-back
    if (victim->valid && victim->dirty) {

        uint64_t victim_addr = 
            (victim->tag << (log2_int(c->line_size) + log2_int(c->sets))) | (idx << log2_int(c->line_size));
        uint64_t old_base = victim_addr & ~(c->line_size - 1);
        mem_write_block(old_base, victim->data, c->line_size);
    

        if (c->next)
            cache_read_block(c->next,
                             old_base,
                             victim->data,
                             size);
        else
            mem_write_block(old_base,
                            victim->data,
                            LINE_SIZE);
    }

    memcpy(victim->data, buf, LINE_SIZE);

    victim->valid = true;
    victim->dirty = false;
    victim->tag   = tag;

    for(int i = 0; i < size; i++) {
        val |= ((uint64_t)victim->data[off + i]) << (i * 8);
    }

    return val;

}

void cache_write(cache_t *c, uint64_t addr, uint64_t value,uint8_t size) {
    uint64_t idx = _index(c, addr);
    uint64_t tag = _tag(c, addr);
    uint64_t off = _offset(c, addr);

    cache_line_t *line = find_line(c, idx, tag);

    // miss -> allocate
    if (!line) {

        c->miss++;

        uint8_t buf[LINE_SIZE];

        uint64_t base = addr & ~(LINE_SIZE - 1);

        cache_read_block(c->next, base, buf, size);

        cache_line_t *victim = select_victim(c, idx);

        // write-back old line
        if (victim->valid && victim->dirty) {

        uint64_t victim_addr = 
            (victim->tag << (log2_int(c->line_size) + log2_int(c->sets))) | (idx << log2_int(c->line_size));
        uint64_t old_base = victim_addr & ~(c->line_size - 1);


        mem_write_block(old_base, victim->data, c->line_size);
    

            if (c->next)
                cache_read_block(c->next,
                                 old_base,
                                 victim->data,
                                 size);
            else
                mem_write_block(old_base,
                                victim->data,
                                LINE_SIZE);
        }

        memcpy(victim->data, buf, LINE_SIZE);

        victim->valid = true;
        victim->dirty = false;
        victim->tag   = tag;

        line = victim;

    } else {
        c->hit++;
    }

    memcpy(line->data + off, &value, size);

    line->dirty = true;
}


void init_cache() {

     L1 = cache_create(1,
                               32 * 1024,
                               4,
                               64);

    L2 = cache_create(2,
                               256 * 1024,
                               8,
                               64);

    L3 = cache_create(3,
                               8 * 1024 * 1024,
                               16,
                               64);

    L1->next = L2;
    L2->next = L3;
    L3->next = NULL; // memory
}