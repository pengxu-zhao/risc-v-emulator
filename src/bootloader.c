

#include "bootloader.h"
extern uint8_t* memory;
int load_bin(const char *filename, uint64_t load_addr)
{
    FILE *f = fopen(filename, "rb");
    if (!f) {
        perror("fopen");
        return -1;
    }

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    rewind(f);

    printf("Loading %s\n", filename);
    printf("addr = 0x%lx\n", load_addr);
    printf("size = 0x%lx\n", size);

    if (load_addr < MEMORY_BASE ||
        load_addr + size > MEMORY_BASE + MEMORY_SIZE) {
        printf("out of memory range\n");
        fclose(f);
        return -1;
    }

    uint8_t *dst =
        memory + (load_addr - MEMORY_BASE);

    size_t n = fread(dst, 1, size, f);

    fclose(f);

    if (n != size) {
        printf("read failed\n");
        return -1;
    }

    printf("load success\n");

    return 0;
}