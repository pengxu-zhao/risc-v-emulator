// virtio_blk.c
#include "virtio_blk.h"
#include <stdlib.h>
#include "cpu.h"
#include "bus.h"
#include "plic.h"
#include "memory.h"
extern uint8_t* memory;

static void inline phys_write(uint64_t addr,uint64_t value, uint8_t size){
    memory_write(&memory,addr,value,size);
}

static uint64_t inline phys_read(uint64_t addr,uint8_t size){
    return memory_read(&memory,addr,size);
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

    printf("virtio-blk: loaded %s, %lu sectors\n", disk_image_path, dev.disk_size_sectors);
}

// 读取 avail ring 中的 next idx
static uint16_t get_avail_idx() {
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

    // 触发中断
    virtio_blk_raise_interrupt();
}

// 处理队列中的所有请求
static void process_queue() {
    if (!dev.queue_ready) return;

    uint16_t last_avail = get_avail_idx();

    static uint16_t prev_avail = 0;
    while (prev_avail != last_avail) {
        uint16_t avail_idx = prev_avail % dev.queue_num;
        uint16_t desc_idx = phys_read(dev.driver_addr + 4 + avail_idx * 2, 2);  // avail->ring[]

        // 读取描述符链（xv6 用 3 个描述符：req -> data -> status）
        uint64_t desc_base = dev.desc_addr + desc_idx * 16;

        uint64_t req_addr  = phys_read(desc_base + 0, 8);
        uint64_t data_addr = phys_read(desc_base + 16, 8);
        uint64_t stat_addr = phys_read(desc_base + 32, 8);

        uint16_t req_flags = phys_read(desc_base + 8, 2);
        uint32_t req_len   = phys_read(desc_base + 12, 4);

        if (req_flags & 2) {  // VRING_DESC_F_NEXT
            // 读取请求
            virtio_blk_req req;
            req.type    = phys_read(req_addr + 0, 4);
            req.reserved= phys_read(req_addr + 4, 4);
            req.sector  = phys_read(req_addr + 8, 8);

            uint64_t offset = req.sector * 512;
            if (offset + 512 <= dev.disk_size_sectors * 512) {
                if (req.type == VIRTIO_BLK_T_IN) {
                    // 读：从磁盘 -> guest 内存
                    memcpy((void*)phys_read_raw(data_addr), dev.disk_data + offset, 512);
                } else if (req.type == VIRTIO_BLK_T_OUT) {
                    // 写：guest 内存 -> 磁盘
                    memcpy(dev.disk_data + offset, (void*)phys_read_raw(data_addr), 512);
                }
            }

            // 写 status = 0 (OK)
            phys_write(stat_addr, 0, 1);
        }

        complete_request(desc_idx, 0);

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
        case 0x060: return 1;                     // InterruptStatus (处理完后清0)
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
            break;
        case 0x094: // QueueDriverHigh
            dev.driver_addr = (dev.driver_addr & 0xffffffffULL) | ((uint64_t)value << 32);
            break;

        case 0x0a0: // QueueDeviceLow (used ring)
            dev.device_addr = (dev.device_addr & ~0xffffffffULL) | value;
            break;
        case 0x0a4: // QueueDeviceHigh
            dev.device_addr = (dev.device_addr & 0xffffffffULL) | ((uint64_t)value << 32);
            break;

        // 忽略所有其他写，包括可能的 InterruptACK (xv6 不写)
        default:
            break;
    }
}

void virtio_blk_raise_interrupt(void) {
    // 设置 InterruptStatus
    //bus_write(VIRTIO_MMIO_BASE + 0x060, 1, 4);
    plic_set_irq(VIRTIO_IRQ,1);

}