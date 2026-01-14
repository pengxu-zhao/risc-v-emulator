// virtio_blk.c
#include "virtio_blk.h"
#include <stdlib.h>
#include "cpu.h"
#include "bus.h"
#include "plic.h"
#include "memory.h"
#include "mmu.h"
extern uint8_t* memory;
extern Bus bus;
extern CPU_State cpu[MAX_CORES];
// 全局设备实例
virtio_blk_device dev;

struct disk_op_list pending_ops;  // 待处理的磁盘操作列表

static void inline phys_write(uint64_t addr,uint64_t value, uint8_t size){
    memory_write(memory,addr,value,size);
}

static uint64_t inline phys_read(uint64_t addr,uint8_t size){
    return memory_read(memory,addr,size);
}

uint8_t* phys_read_raw(uint64_t addr) {
    if (addr < MEMORY_BASE || addr >= MEMORY_BASE + MEMORY_SIZE) {
        fprintf(stderr, "phys_read_raw: address 0x%lx out of DRAM range\n", addr);
        return NULL;
    }

    uint64_t offset = addr - MEMORY_BASE;
    return &memory[offset];
}

uint8_t* phys_write_raw(uint64_t addr) {
    if (addr < MEMORY_BASE || addr >= MEMORY_BASE + MEMORY_SIZE) {
        fprintf(stderr, "phys_write_raw: address 0x%lx out of DRAM range\n", addr);
        return NULL;
    }

    uint64_t offset = addr - MEMORY_BASE;
    return &memory[offset];
}


// 从磁盘镜像文件加载整个 fs.img 到内存（简单方式）
void virtio_blk_init(const char *disk_image_path) {
    FILE *f = fopen(disk_image_path, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open disk image: %s\n", disk_image_path);
        exit(1);
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    dev.disk_data = malloc(size);
    if (!dev.disk_data) {
        perror("malloc disk");
        exit(1);
    }

    if (fread(dev.disk_data, 1, size, f) != (size_t)size) {
        perror("read disk");
        exit(1);
    }
    fclose(f);

    dev.disk_size_sectors = size / 512;
    if (size % 512 != 0) {
        fprintf(stderr, "Warning: disk image size not multiple of 512\n");
    }
    dev.driver_addr = 0x10001000ULL;  // VirtIO MMIO 基址，必须！
    dev.queue_num = 8;                // xv6 NUM=8
    dev.queue_ready = false;          // 初始化未完成，等 xv6 配置
    dev.desc_addr = 0;
   
    dev.device_addr = 0;
    
    printf("virtio-blk: loaded %s, %lu sectors\n", disk_image_path, dev.disk_size_sectors);
}

// 读取 avail ring 中的 next idx
static uint16_t get_avail_idx() {

    printf("[get_avail_idx] driver_addr:0x%08lx\n",dev.driver_addr);
    uint16_t idx = phys_read(dev.driver_addr + 2, 2);  // avail->idx (uint16_t offset 2)
    return idx;
}

// 处理一个完成的请求：写入 used ring
static void complete_request(uint16_t desc_idx, uint8_t status) {
    uint64_t used_ring = dev.device_addr;
    uint16_t used_idx = dev.last_used_idx;

    // used_elem: id (uint16_t), len (uint32_t)
    phys_write(used_ring + 4 + (used_idx % dev.queue_num) * 8 + 0, desc_idx, 2);
    phys_write(used_ring + 4 + (used_idx % dev.queue_num) * 8 + 4, 1, 4);  // len=1 (status byte)

    dev.last_used_idx++;

    // 更新 used->idx
    phys_write(used_ring + 2, dev.last_used_idx, 2);
  //  printf("[complete_request]\n");
    // 触发中断
    virtio_blk_raise_interrupt();
}


static void complete_disk_operation(struct disk_operation *op) {
    printf("[VIRTIO] Completing operation for desc %u\n", op->head_desc_idx);
    
    // 1. 遍历描述符链，处理数据
    uint16_t desc_idx = op->head_desc_idx;
    uint64_t req_addr = 0, data_addr = 0;
    
    while (1) {
        uint64_t desc_base = dev.desc_addr + desc_idx * 16;
        
        uint64_t addr = get_pa(&cpu[0],phys_read(desc_base + 0, 8),ACC_LOAD);
        uint32_t len = phys_read(desc_base + 8, 4);
        uint16_t flags = phys_read(desc_base + 12, 2);
        uint16_t next = phys_read(desc_base + 14, 2);
        
        // 第一个描述符是 virtio_blk_req
        if (desc_idx == op->head_desc_idx) {
            req_addr = addr;
        } else if (len == 512) {
            // 数据描述符
            data_addr = addr;
        }
        
        if (!(flags & VRING_DESC_F_NEXT)) {
            // 最后一个描述符是状态
            // 写状态 0 表示成功
            phys_write(addr, 0, 1);
            break;
        }
        
        desc_idx = next;
    }
    
    // 2. 处理磁盘 I/O
    if (req_addr && data_addr) {
        uint32_t type = phys_read(req_addr, 4);
        uint64_t sector = phys_read(req_addr + 8, 8);
        uint64_t offset = sector * 512;
        
        if (type == VIRTIO_BLK_T_IN) {
            // 读操作：磁盘 -> 内存
            memcpy(memory + (data_addr - MEMORY_BASE), dev.disk_data + offset, 512);
        } else if (type == VIRTIO_BLK_T_OUT) {
            // 写操作：内存 -> 磁盘
            memcpy(dev.disk_data + offset, memory + (data_addr - MEMORY_BASE), 512);
        }
    }
    
    // 3. 更新 used ring
    // used->ring[used_idx % queue_num].id = head_desc_idx
    // used->ring[used_idx % queue_num].len = 数据长度
    uint64_t used_addr = dev.driver_addr + 0x1000; // used 在 avail 之后
    uint16_t used_idx = phys_read(used_addr + 2, 2);
    uint64_t used_ring_offset = 4 + (used_idx % dev.queue_num) * 8;
    
    phys_write(used_addr + used_ring_offset, op->head_desc_idx, 2); // id
    phys_write(used_addr + used_ring_offset + 4, 512, 4); // len
    
    // 更新 used->idx
    used_idx++;
    phys_write(used_addr + 2, used_idx, 2);
    
    // 4. 触发中断
    dev.interrupt_status = 1;
    plic_set_irq(VIRTIO_IRQ,1);
    
    printf("[VIRTIO] Operation completed, interrupt triggered\n");
}
// 在模拟器主循环中定期调用
void virtio_disk_update(uint64_t current_cycle) {
    struct disk_operation *op, *next_op;
    // 手动安全遍历
    for (op = LIST_FIRST(&pending_ops); op != NULL; op = next_op) {
        // 提前保存下一个指针
        next_op = LIST_NEXT(op, entriess);
        
        if (!op->completed && current_cycle >= op->completion_time) {
            printf("[DISK] Operation completed for desc %u\n", op->head_desc_idx);
            
            complete_disk_operation(op);
            LIST_REMOVE(op, entriess);
            free(op);
        }
    }
}

// 异步处理磁盘操作
static void start_async_disk_operation(uint16_t head_desc_idx) {
    // 创建异步操作结构
    struct disk_operation *op = malloc(sizeof(struct disk_operation));
    op->head_desc_idx = head_desc_idx;
    op->start_time = get_cpu_cycle(&cpu[0]);
    op->completion_time = op->start_time + DISK_LATENCY_CYCLES;
    op->completed = 0;
    
    // 添加到待处理列表
    LIST_INSERT_HEAD(&pending_ops, op, entriess);
    
    printf("[VIRTIO] Started async op for desc %u, completes at cycle %lu\n",
           head_desc_idx, op->completion_time);
}

// 处理队列中的所有请求
static void process_queue() {
    printf("ready:%d\n",dev.queue_ready);
    if (!dev.queue_ready) return;
    
    printf("driver_addr:0x%08lx\n",dev.driver_addr);
    uint16_t last_avail = get_avail_idx();

    static uint16_t prev_avail = 0;
    printf("last_avail:0x%08lx\n",last_avail);
    while (prev_avail != last_avail) {
        uint16_t avail_idx = prev_avail % dev.queue_num;
        uint16_t desc_idx = phys_read(dev.driver_addr + 4 + avail_idx * 2, 2);  // avail->ring[]

        start_async_disk_operation(desc_idx);
        prev_avail++;
    }
}

uint32_t virtio_mmio_read(void *opaque,uint64_t offset,uint8_t size) {
    printf("[virtio_mmio_read] offset:0x%08lx\n",offset);
    switch (offset) {
        case 0x000: return 0x74726976;            // MagicValue
        case 0x004: return 2;                     // Version (modern)
        case 0x008: return 2;                     // DeviceID (block)
        case 0x00c: return 0x554d4551;            // VendorID (QEMU)
        case 0x010: return (1ULL << 5);           // DeviceFeatures (支持 RO 等)
        case 0x014: return (1ULL << 5);           // DeviceFeaturesSel 用后返回
        case 0x020: return 0;                     // DriverFeatures
        case 0x034: return 8;                     // QueueNumMax (xv6 用 8)
        case 0x044: return dev.queue_ready;       // QueueReady
        case 0x060: return dev.interrupt_status;                     // InterruptStatus (处理完后清0)
        case 0x070: return dev.status;
        case 0x080: return dev.desc_addr & 0xffffffffULL;
        case 0x084: return dev.desc_addr >> 32;
        case 0x090: return dev.driver_addr & 0xffffffffULL;
        case 0xfc:  return 0x1;                   // ConfigGeneration
        default:
            if (offset >= 0x100) return 0;        // config space (不实现)
            return 0;
    }
}

void virtio_mmio_write(void *opaque,uint64_t offset, uint32_t value,uint8_t size) {
   
    switch (offset) {
        case 0x038: // QueueNum
            dev.queue_num = value;
            break;

        case 0x044: // QueueReady
            dev.queue_ready = value & 1;
            if (dev.queue_ready) dev.last_used_idx = 0;
            break;
        case 0x050:// QueueNotify
         //  printf("[0x050]driver_addr:0x%08lx\n",dev.driver_addr);
            if (value == 0) process_queue();
            break;
        case 0x070: 
            dev.status = value;
            break;

        case 0x080: // QueueDescLow
            dev.desc_addr = (dev.desc_addr & ~0xffffffffULL) | value;
            break;
        case 0x084: // QueueDescHigh
            dev.desc_addr = (dev.desc_addr & 0xffffffffULL) | ((uint64_t)value << 32);
            break;

        case 0x090: // QueueDriverLow (avail ring)
            dev.driver_addr = (dev.driver_addr & ~0xffffffffULL) | value;
            printf("[0x90]dev.driver_addr:0x%08lx, value:0x%08lx\n",dev.driver_addr,value);
            break;
        case 0x094: // QueueDriverHigh
            dev.driver_addr = (dev.driver_addr & 0xffffffffULL) | ((uint64_t)value << 32);
            printf("[0x94]dev.driver_addr:0x%08lx, value:0x%08lx\n",dev.driver_addr,value);
            break;

        case 0x0a0: // QueueDeviceLow (used ring)
            dev.device_addr = (dev.device_addr & ~0xffffffffULL) | value;
            printf("[0xa0]dev.driver_addr:0x%08lx, value:0x%08lx\n",dev.driver_addr,value);
            break;
        case 0x0a4: // QueueDeviceHigh
            dev.device_addr = (dev.device_addr & 0xffffffffULL) | ((uint64_t)value << 32);
            printf("[0xa4]dev.driver_addr:0x%08lx, value:0x%08lx\n",dev.driver_addr,value);
            break;

        // 忽略所有其他写，包括可能的 InterruptACK (xv6 不写)
        default:
            break;
    }
}

void virtio_blk_raise_interrupt(void) {
    // 设置InterruptStatus寄存器（告诉驱动有中断）
   
    dev.interrupt_status |= 0x3;

    if(dev.interrupt_status & 0x3){
     //   cpu[0].csr[CSR_SSTATUS] = 0x2;
    }

    plic_set_irq(VIRTIO_IRQ,1);

}