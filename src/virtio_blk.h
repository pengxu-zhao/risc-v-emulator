// virtio_blk.h
#ifndef VIRTIO_BLK_H
#define VIRTIO_BLK_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define VIRTIO_MMIO_BASE    0x10001000ULL
#define VIRTIO_MMIO_SIZE    0x1000

// virtio-blk 请求类型
#define VIRTIO_BLK_T_IN     0   // 读
#define VIRTIO_BLK_T_OUT    1   // 写

// 请求结构（guest -> device）
typedef struct {
    uint32_t type;      // IN 或 OUT
    uint32_t reserved;
    uint64_t sector;    // 扇区号（512字节为单位）
} virtio_blk_req;

// 描述符链：req + data(512B) + status(1B)

// 设备状态
typedef struct {
    uint64_t disk_size_sectors;  // fs.img 大小 / 512
    uint8_t *disk_data;          // 直接指向加载的 fs.img 内存（可选：也可以用文件）

    // 队列相关（xv6 只用 queue 0）
    uint16_t queue_num;          // 驱动设置的队列大小（xv6 用 8）
    uint64_t desc_addr;          // 描述符表物理地址
    uint64_t driver_addr;        // avail ring 物理地址
    uint64_t device_addr;        // used ring 物理地址

    uint16_t last_used_idx;      // 用于写入 used ring
    int queue_ready;             // 1 表示队列已就绪
    int status;
} virtio_blk_device;

// 全局设备实例
static virtio_blk_device dev = {0};

void virtio_blk_init(const char *disk_image_path);
uint32_t virtio_mmio_read(void *opaque,uint64_t offset,uint8_t size);
void virtio_mmio_write(void *opaque,uint64_t offset, uint32_t value,uint8_t size) ;
void virtio_blk_raise_interrupt(void);  

#endif