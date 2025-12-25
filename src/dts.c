#include "dts.h"
#include "cpu.h"

// 字节序转换（RISC-V是小端，设备树要求大端）
uint32_t cpu_to_fdt32(uint32_t x) {
    return x;
    /*
    return ((x & 0x000000FF) << 24) |
           ((x & 0x0000FF00) << 8) |
           ((x & 0x00FF0000) >> 8) |
           ((x & 0xFF000000) >> 24);
      */     
}

uint64_t cpu_to_fdt64(uint64_t x) {
    return x;
    /*
    return ((x & 0x00000000000000FFULL) << 56) |
           ((x & 0x000000000000FF00ULL) << 40) |
           ((x & 0x0000000000FF0000ULL) << 24) |
           ((x & 0x00000000FF000000ULL) << 8) |
           ((x & 0x000000FF00000000ULL) >> 8) |
           ((x & 0x0000FF0000000000ULL) >> 24) |
           ((x & 0x00FF000000000000ULL) >> 40) |
           ((x & 0xFF00000000000000ULL) >> 56); */
}

// 写入令牌
static void write_token(uint8_t *memory, uint64_t base, uint32_t *pos, uint32_t token) {
    uint32_t be_token = cpu_to_fdt32(token);
    memcpy(&memory[base + *pos], &be_token, 4);
    *pos += 4;
}

// 写入字符串
static void write_string(uint8_t *memory, uint64_t base, uint32_t *pos, const char *str) {
    strcpy((char*)&memory[base + *pos], str);
    *pos += strlen(str) + 1;
    // 对齐到4字节
    while (*pos % 4 != 0) {
        memory[base + *pos] = 0;
        (*pos)++;
    }
}

// 写入属性（字符串值）
static void write_property_string(uint8_t *memory, uint64_t base, uint32_t *pos, 
                                 const char *name, const char *value) {
    write_token(memory, base, pos, FDT_PROP);
    
    uint32_t len = strlen(value) + 1;
    uint32_t be_len = cpu_to_fdt32(len);
    memcpy(&memory[base + *pos], &be_len, 4);
    *pos += 4;
    
    // 在简化版本中，我们假设所有字符串都在字符串表中偏移0
    uint32_t nameoff = 0;
    uint32_t be_nameoff = cpu_to_fdt32(nameoff);
    memcpy(&memory[base + *pos], &be_nameoff, 4);
    *pos += 4;
    
    strcpy((char*)&memory[base + *pos], value);
    *pos += len;
    
    // 对齐到4字节
    while (*pos % 4 != 0) {
        memory[base + *pos] = 0;
        (*pos)++;
    }
}

// 写入属性（32位整数）
static void write_property_u32(uint8_t *memory, uint64_t base, uint32_t *pos, 
                              const char *name, uint32_t value) {
    write_token(memory, base, pos, FDT_PROP);
    
    uint32_t be_len = cpu_to_fdt32(4);
    memcpy(&memory[base + *pos], &be_len, 4);
    *pos += 4;
    
    uint32_t nameoff = 0;
    uint32_t be_nameoff = cpu_to_fdt32(nameoff);
    memcpy(&memory[base + *pos], &be_nameoff, 4);
    *pos += 4;
    
    uint32_t be_value = cpu_to_fdt32(value);
    memcpy(&memory[base + *pos], &be_value, 4);
    *pos += 4;
}

// 写入属性（64位整数）
static void write_property_u64(uint8_t *memory, uint64_t base, uint32_t *pos, 
                              const char *name, uint64_t value) {
    write_token(memory, base, pos, FDT_PROP);
    
    uint32_t be_len = cpu_to_fdt32(8);
    memcpy(&memory[base + *pos], &be_len, 4);
    *pos += 4;
    
    uint32_t nameoff = 0;
    uint32_t be_nameoff = cpu_to_fdt32(nameoff);
    memcpy(&memory[base + *pos], &be_nameoff, 4);
    *pos += 4;
    
    uint64_t be_value = cpu_to_fdt64(value);
    memcpy(&memory[base + *pos], &be_value, 8);
    *pos += 8;
}

// 写入属性（寄存器范围）
static void write_property_reg(uint8_t *memory, uint64_t base, uint32_t *pos, 
                              const char *name, uint64_t *reg, int count) {
    write_token(memory, base, pos, FDT_PROP);
    
    uint32_t len = count * 2 * sizeof(uint64_t);
    uint32_t be_len = cpu_to_fdt32(len);
    memcpy(&memory[base + *pos], &be_len, 4);
    *pos += 4;
    
    uint32_t nameoff = 0;
    uint32_t be_nameoff = cpu_to_fdt32(nameoff);
    memcpy(&memory[base + *pos], &be_nameoff, 4);
    *pos += 4;
    
    for (int i = 0; i < count * 2; i++) {
        uint64_t be_reg = cpu_to_fdt64(reg[i]);
        memcpy(&memory[base + *pos], &be_reg, 8);
        *pos += 8;
    }
}

// 写入属性（空值）
static void write_property_null(uint8_t *memory, uint64_t base, uint32_t *pos, 
                               const char *name) {
    write_token(memory, base, pos, FDT_PROP);
    
    uint32_t be_len = cpu_to_fdt32(0);
    memcpy(&memory[base + *pos], &be_len, 4);
    *pos += 4;
    
    uint32_t nameoff = 0;
    uint32_t be_nameoff = cpu_to_fdt32(nameoff);
    memcpy(&memory[base + *pos], &be_nameoff, 4);
    *pos += 4;
}

// 创建设备树
void create_complete_device_tree(uint8_t *memory, uint64_t dtb_addr) {
    printf("正在创建设备树 (4GB内存配置)...\n");

    printf("=== 设备树调试信息 ===\n");
    printf("内存指针: %p\n", memory);
    printf("设备树地址: 0x%lx\n", MEMORY_BASE + dtb_addr);
    printf("内存总大小: 0x%lx\n", MEMORY_SIZE);
    
    // 检查参数有效性
    if (!memory) {
        printf("❌ 错误: memory指针为NULL\n");
        return;
    }
    
    if (MEMORY_BASE + dtb_addr >= MEMORY_SIZE) {
        printf("❌ 错误: 设备树地址超出内存范围\n");
        return;
    }
    
    // 设备树布局
    uint32_t header_size = sizeof(struct fdt_header);
    uint32_t struct_offset = header_size;
    uint32_t strings_offset = struct_offset + 2048;  // 预留2KB给结构
    uint32_t total_size = strings_offset + 512;      // 预留512B给字符串
    
    // 1. 设备树头
    struct fdt_header header = {
        .magic = FDT_MAGIC,
        .totalsize = cpu_to_fdt32(total_size),
        .off_dt_struct = cpu_to_fdt32(struct_offset),
        .off_dt_strings = cpu_to_fdt32(strings_offset),
        .off_mem_rsvmap = cpu_to_fdt32(0),
        .version = cpu_to_fdt32(17),
        .last_comp_version = cpu_to_fdt32(16),
        .boot_cpuid_phys = cpu_to_fdt32(0),
        .size_dt_strings = cpu_to_fdt32(512),
        .size_dt_struct = cpu_to_fdt32(2048),
    };


    printf("设备树头信息:\n");
    printf("  magic: 0x%x\n", header.magic);
    printf("  totalsize: %u\n", header.totalsize);
    
    // 写入前的内存状态
    printf("写入前内存[0x%lx]: ", dtb_addr);
    for (int i = 0; i < 8; i++) {
        printf("%02x ", memory[dtb_addr + i]);
    }
    printf("\n");
    
    
    memcpy(&memory[dtb_addr], &header, header_size);
    uint32_t pos = struct_offset;
    


    // 2. 根节点
    write_token(memory, dtb_addr, &pos, FDT_BEGIN_NODE);
    write_string(memory, dtb_addr, &pos, "");
    
    // 兼容性
    write_property_string(memory, dtb_addr, &pos, "compatible", "ucbbar,spike-bare-dev");
    write_property_string(memory, dtb_addr, &pos, "model", "ucbbar,spike-bare");
    
    // 3. CPU配置
    write_token(memory, dtb_addr, &pos, FDT_BEGIN_NODE);
    write_string(memory, dtb_addr, &pos, "cpus");
    write_property_u32(memory, dtb_addr, &pos, "phandle", 0x1);
    write_property_u32(memory, dtb_addr, &pos, "riscv,isa", 0);
    write_property_u32(memory, dtb_addr, &pos, "timebase-frequency", 10000000);
    
    // CPU 0
    write_token(memory, dtb_addr, &pos, FDT_BEGIN_NODE);
    write_string(memory, dtb_addr, &pos, "cpu@0");
    write_property_string(memory, dtb_addr, &pos, "device_type", "cpu");
    write_property_string(memory, dtb_addr, &pos, "status", "okay");
    write_property_string(memory, dtb_addr, &pos, "compatible", "riscv");
    write_property_string(memory, dtb_addr, &pos, "riscv,isa", "rv64imafdc");
    write_property_string(memory, dtb_addr, &pos, "mmu-type", "riscv,sv48");
    write_property_u32(memory, dtb_addr, &pos, "reg", 0);
    write_property_u32(memory, dtb_addr, &pos, "clock-frequency", 1000000000);
    
    // 中断控制器
    write_token(memory, dtb_addr, &pos, FDT_BEGIN_NODE);
    write_string(memory, dtb_addr, &pos, "interrupt-controller");
    write_property_u32(memory, dtb_addr, &pos, "phandle", 0x2);
    write_property_null(memory, dtb_addr, &pos, "interrupt-controller");
    write_property_string(memory, dtb_addr, &pos, "compatible", "riscv,cpu-intc");
    write_property_u32(memory, dtb_addr, &pos, "#interrupt-cells", 1);
    write_token(memory, dtb_addr, &pos, FDT_END_NODE);
    
    write_token(memory, dtb_addr, &pos, FDT_END_NODE); // 结束cpu@0
    write_token(memory, dtb_addr, &pos, FDT_END_NODE); // 结束cpus
    
    // 4. 内存配置 (4GB)
    write_token(memory, dtb_addr, &pos, FDT_BEGIN_NODE);
    write_string(memory, dtb_addr, &pos, "memory@80000000");
    write_property_string(memory, dtb_addr, &pos, "device_type", "memory");
    uint64_t mem_reg[] = {MEMORY_BASE, MEMORY_SIZE};
    write_property_reg(memory, dtb_addr, &pos, "reg", mem_reg, 1);
    write_token(memory, dtb_addr, &pos, FDT_END_NODE);
    
    // 5. 系统总线
    write_token(memory, dtb_addr, &pos, FDT_BEGIN_NODE);
    write_string(memory, dtb_addr, &pos, "soc");
    write_property_string(memory, dtb_addr, &pos, "compatible", "simple-bus");
    write_property_string(memory, dtb_addr, &pos, "ranges", "");
    write_property_string(memory, dtb_addr, &pos, "bus-type", "amba");
    
    // 6. CLINT (Core Local Interruptor)
    write_token(memory, dtb_addr, &pos, FDT_BEGIN_NODE);
    write_string(memory, dtb_addr, &pos, "clint@2000000");
    write_property_string(memory, dtb_addr, &pos, "compatible", "riscv,clint0");
    uint64_t clint_reg[] = {CLINT_BASE, 0x10000};
    write_property_reg(memory, dtb_addr, &pos, "reg", clint_reg, 1);
    write_property_u32(memory, dtb_addr, &pos, "interrupts-extended", 0x2000001);
    write_token(memory, dtb_addr, &pos, FDT_END_NODE);
    
    // 7. PLIC (Platform Level Interrupt Controller)
    write_token(memory, dtb_addr, &pos, FDT_BEGIN_NODE);
    write_string(memory, dtb_addr, &pos, "plic@c000000");
    write_property_string(memory, dtb_addr, &pos, "compatible", "riscv,plic0");
    write_property_string(memory, dtb_addr, &pos, "interrupt-controller", "");
    uint64_t plic_reg[] = {PLIC_BASE, 0x4000000};
    write_property_reg(memory, dtb_addr, &pos, "reg", plic_reg, 1);
    write_property_u32(memory, dtb_addr, &pos, "riscv,ndev", 53);
    write_property_u32(memory, dtb_addr, &pos, "interrupts-extended", 0x2000009);
    write_property_u32(memory, dtb_addr, &pos, "phandle", 0x3);
    write_token(memory, dtb_addr, &pos, FDT_END_NODE);
    
    // 8. UART0
    write_token(memory, dtb_addr, &pos, FDT_BEGIN_NODE);
    write_string(memory, dtb_addr, &pos, "uart@10000000");
    write_property_string(memory, dtb_addr, &pos, "compatible", "ns16550a");
    uint64_t uart_reg[] = {UART0_BASE, 0x100};
    write_property_reg(memory, dtb_addr, &pos, "reg", uart_reg, 1);
    write_property_u32(memory, dtb_addr, &pos, "clock-frequency", 1843200);
    write_property_u32(memory, dtb_addr, &pos, "interrupt-parent", 0x3);
    write_property_u32(memory, dtb_addr, &pos, "interrupts", 10);
    write_property_string(memory, dtb_addr, &pos, "reg-shift", "0");
    write_property_string(memory, dtb_addr, &pos, "reg-io-width", "1");
    write_token(memory, dtb_addr, &pos, FDT_END_NODE);
    
    // 9. RTC
    write_token(memory, dtb_addr, &pos, FDT_BEGIN_NODE);
    write_string(memory, dtb_addr, &pos, "rtc@10100000");
    write_property_string(memory, dtb_addr, &pos, "compatible", "google,goldfish-rtc");
    uint64_t rtc_reg[] = {RTC_BASE, 0x1000};
    write_property_reg(memory, dtb_addr, &pos, "reg", rtc_reg, 1);
    write_property_u32(memory, dtb_addr, &pos, "interrupt-parent", 0x3);
    write_property_u32(memory, dtb_addr, &pos, "interrupts", 11);
    write_token(memory, dtb_addr, &pos, FDT_END_NODE);
    
    write_token(memory, dtb_addr, &pos, FDT_END_NODE); // 结束soc
    
    // 10. 选择引导CPU
    write_property_string(memory, dtb_addr, &pos, "stdout-path", "/soc/uart@10000000");
    
    write_token(memory, dtb_addr, &pos, FDT_END_NODE); // 结束根节点
    write_token(memory, dtb_addr, &pos, FDT_END);
    
    printf("✅ 设备树创建完成 (大小: %u bytes)\n", total_size);
    printf("   内存: 0x%lx - 0x%lx (%lu MB)\n", 
           MEMORY_BASE, MEMORY_BASE + MEMORY_SIZE - 1, MEMORY_SIZE / (1024 * 1024));

    printf("=== 设备树头结构调试 ===\n");
    printf("结构体大小: %zu bytes\n", sizeof(struct fdt_header));
    printf("字段偏移检查:\n");
    
    printf("magic: 值=0x%08x\n", header.magic);
    printf("totalsize: 值=0x%08x (原始值=%u)\n", header.totalsize, total_size);
    printf("off_dt_struct: 值=0x%08x\n", header.off_dt_struct);
    printf("off_dt_strings: 值=0x%08x\n", header.off_dt_strings);
    
    // 写入后的内存状态
    printf("写入后内存[0x%lx]: ", dtb_addr);
    for (int i = 0; i < 8; i++) {
        printf("%02x ", memory[dtb_addr + i]);
    }
    printf("\n");
    
    printf("✅ 设备树创建完成\n");



    // 检查实际内存布局
    uint8_t *header_bytes = (uint8_t*)&header;
    printf("实际内存内容:\n");
    for (int i = 0; i < 40; i++) {
        printf("%02x ", header_bytes[i]);
        if ((i + 1) % 8 == 0) printf("\n");
    }
    printf("\n");
}
void verify_dtb_for_opensbi(uint8_t *memory, uint64_t dtb_addr) {
    uint32_t *dtb = (uint32_t*)(memory + dtb_addr);
    
    printf("=== OpenSBI设备树验证 ===\n");
    printf("设备树位置: 0x%lx\n",MEMORY_BASE + dtb_addr);
    
    // 直接读取内存中的大端值
    printf("内存中的原始值 (大端):\n");
    printf("  magic: 0x%08x %s\n", dtb[0], dtb[0] == 0xd00dfeed ? "✅" : "❌");
    printf("  totalsize: 0x%08x\n", dtb[1]);
    printf("  off_dt_struct: 0x%08x\n", dtb[2]);
    printf("  off_dt_strings: 0x%08x\n", dtb[3]);
    
}