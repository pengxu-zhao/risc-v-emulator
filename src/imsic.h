#ifndef IMSIC_H
#define IMSIC_H

#include "common.h"

// 初始化 IMSIC 设备
void imsic_init(imsic_t *imsic, uint32_t num_harts,
                void (*set_ext_irq)(uint32_t, imsic_mode_t, bool));

// 释放资源
void imsic_cleanup(imsic_t *imsic);

// 获取某个 hart 的某个模式的文件指针
imsic_file_t *imsic_get_file(imsic_t *imsic, uint32_t hart_id, imsic_mode_t mode);

// MMIO 读处理，返回读取的 32 位值。offset 为相对于文件基址的字节偏移。
uint64_t imsic_read(imsic_file_t *file, uint64_t offset, unsigned size);

// MMIO 写处理，value 为 32 位写入值。offset 为相对于文件基址的字节偏移。
void imsic_write(imsic_file_t *file, uint64_t offset, uint32_t value, unsigned size);

// 评估并更新该文件的中断线状态（应在每次影响挂起或使能的写操作后调用）
void imsic_update_irq(imsic_t *imsic, imsic_file_t *file);

void set_ext_irq(uint32_t hart_id, imsic_mode_t mode, bool level);
#endif