#ifndef DTS_H
#define DTS_H
#include <stdint.h>


#define FDT_MAGIC 0xd00dfeed

// 设备树头结构
struct fdt_header {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

// 设备树令牌
#define FDT_BEGIN_NODE 0x00000001
#define FDT_END_NODE   0x00000002
#define FDT_PROP       0x00000003
#define FDT_NOP        0x00000004
#define FDT_END        0x00000009

// 内存范围
#define MEMORY_BASE    0x80000000
#define MEMORY_SIZE    0x100000000  // 4GB

// 设备地址
#define UART0_BASE     0x10000000
#define RTC_BASE       0x10100000
#define PLIC_BASE      0x0c000000
#define CLINT_BASE     0x02000000

// 函数声明
uint32_t cpu_to_fdt32(uint32_t x);
uint64_t cpu_to_fdt64(uint64_t x);
void create_complete_device_tree(uint8_t *memory, uint64_t dtb_addr);
void verify_dtb_for_opensbi(uint8_t *memory, uint64_t dtb_addr);

#endif