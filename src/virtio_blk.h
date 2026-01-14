// virtio_blk.h
#ifndef VIRTIO_BLK_H
#define VIRTIO_BLK_H

#include "common.h"


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
    int interrupt_status;
} virtio_blk_device;




// 磁盘操作状态跟踪结构
struct disk_operation {
    uint16_t head_desc_idx;      // 描述符链的头部索引
    uint64_t start_time;         // 开始时间（模拟器周期数）
    uint64_t completion_time;    // 完成时间（模拟器周期数）
    int completed;               // 是否已完成
    uint32_t type;               // 操作类型：读或写
    uint64_t sector;             // 扇区号
    uint64_t data_phys_addr;     // 数据物理地址
    
    // 链表链接（用于管理待处理操作）
    LIST_ENTRY(disk_operation) entriess;
};

// 声明操作列表
LIST_HEAD(disk_op_list, disk_operation);


void virtio_blk_init(const char *disk_image_path);
uint32_t virtio_mmio_read(void *opaque,uint64_t offset,uint8_t size);
void virtio_mmio_write(void *opaque,uint64_t offset, uint32_t value,uint8_t size) ;
void virtio_blk_raise_interrupt(void);  
void virtio_disk_update(uint64_t current_cycle);
#endif