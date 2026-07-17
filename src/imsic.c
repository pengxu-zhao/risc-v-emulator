#include "imsic.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "imsic.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "cpu.h"

extern CPU_State cpu[MAX_CORES];
imsic_t g_imsic;
// 内部：从挂起位中找出优先级最高（号码最小）且大于阈值的挂起中断号
// 返回 0 表示无符合条件的中断
static uint32_t imsic_highest_pending(const imsic_file_t *file)
{
    if (!file->eidelivery)
        return 0;

    // eip 的 bit 0 恒为 0（保留），从 bit 1 开始检查
    for (uint32_t int_id = 1; int_id < IMSIC_MAX_INTS; int_id++) {
        if (int_id <= file->eithreshold)
            continue;   // 身份号必须大于阈值

        uint32_t word = int_id / 32;
        uint32_t bit  = int_id % 32;
        if (file->eip[word] & (1u << bit))
            return int_id;
    }
    return 0;
}

// 初始化设备
void imsic_init(imsic_t *imsic, uint32_t num_harts,
                void (*set_ext_irq)(uint32_t, imsic_mode_t, bool))
{
    imsic->num_harts = num_harts;
    imsic->mfiles = (imsic_file_t *)calloc(num_harts, sizeof(imsic_file_t));
    imsic->sfiles = (imsic_file_t *)calloc(num_harts, sizeof(imsic_file_t));
    imsic->set_ext_irq = set_ext_irq;

    for (uint32_t i = 0; i < num_harts; i++) {
        imsic->mfiles[i].hart_id = i;
        imsic->mfiles[i].mode    = IMSIC_M_MODE;
        imsic->sfiles[i].hart_id = i;
        imsic->sfiles[i].mode    = IMSIC_S_MODE;
    }
}

void imsic_cleanup(imsic_t *imsic)
{
    free(imsic->mfiles);
    free(imsic->sfiles);
    imsic->mfiles = NULL;
    imsic->sfiles = NULL;
}

imsic_file_t *imsic_get_file(imsic_t *imsic, uint32_t hart_id, imsic_mode_t mode)
{
    if (hart_id >= imsic->num_harts)
        return NULL;
    return (mode == IMSIC_M_MODE) ? &imsic->mfiles[hart_id] : &imsic->sfiles[hart_id];
}

// ---------- MMIO 读操作 ----------
uint64_t imsic_read(imsic_file_t *file, uint64_t offset,unsigned size)
{
    if (!file)
        return 0;

    if(size != IMSIC_FILE_SIZE){
        fprintf(stderr, "IMSIC read size mismatch: expected %u, got %u\n", IMSIC_FILE_SIZE, size);
        return 0;
    }

    switch (offset) {
    case IMSIC_EIDELIVERY:
        return file->eidelivery ? 1 : 0;

    case IMSIC_EITHRESHOLD:
        return file->eithreshold;

    default:
        if (offset >= IMSIC_EIP_BASE && offset < IMSIC_EIP_BASE + (IMSIC_MAX_INTS / 8)) {
            uint32_t idx = (offset - IMSIC_EIP_BASE) / 4;
            return file->eip[idx];
        }
        // 其它偏移：读零
        if (offset >= IMSIC_FILE_SIZE)
            fprintf(stderr, "IMSIC read out of range: offset 0x%lx\n", offset);
        return 0;
    }
}

// ---------- MMIO 写操作 ----------
void imsic_write(imsic_file_t *file, uint64_t offset, uint32_t value,unsigned size)
{
    if (!file)
        return;

    if(size != IMSIC_FILE_SIZE){
        fprintf(stderr, "IMSIC write size mismatch: expected %u, got %u\n", IMSIC_FILE_SIZE, size);
        return;
    }

    switch (offset) {
    case IMSIC_EIDELIVERY:
        file->eidelivery = (value & 1) ? true : false;
        break;

    case IMSIC_EITHRESHOLD:
        file->eithreshold = value & 0xFF;   // 只有低 8 位有效
        break;

    case IMSIC_SETIPNUM: {
        uint32_t id = value & 0xFF;         // 身份号 0-255
        if (id > 0 && id < IMSIC_MAX_INTS) {
            uint32_t word = id / 32;
            uint32_t bit  = id % 32;
            file->eip[word] |= (1u << bit);
        }
        break;
    }

    case IMSIC_CLRIPNUM: {
        uint32_t id = value & 0xFF;
        if (id > 0 && id < IMSIC_MAX_INTS) {
            uint32_t word = id / 32;
            uint32_t bit  = id % 32;
            file->eip[word] &= ~(1u << bit);
        }
        break;
    }

    default:
        // eip 区域，支持写 1 清 0
        if (offset >= IMSIC_EIP_BASE && offset < IMSIC_EIP_BASE + (IMSIC_MAX_INTS / 8)) {
            uint32_t idx = (offset - IMSIC_EIP_BASE) / 4;
            file->eip[idx] &= ~value;   // AIA 规范：写 1 的位被清 0
        }
        // 其它偏移：无操作
        break;
    }
}

// ---------- 中断评估与更新 ----------
void imsic_update_irq(imsic_t *imsic, imsic_file_t *file)
{
    if (!imsic || !file || !imsic->set_ext_irq)
        return;

    bool has_pending = (imsic_highest_pending(file) != 0);
    imsic->set_ext_irq(file->hart_id, file->mode, has_pending);
}


// 定义中断线回调
void set_ext_irq(uint32_t hart_id, imsic_mode_t mode, bool level) {

    if(level){
        if (mode == IMSIC_M_MODE)
            cpu[hart_id].csr[CSR_MIP] |= MIP_MEIP;
        else
            cpu[hart_id].csr[CSR_MIP] |= MIP_SEIP;
    } else {
        if (mode == IMSIC_M_MODE)
            cpu[hart_id].csr[CSR_MIP] &= ~MIP_MEIP;
        else
            cpu[hart_id].csr[CSR_MIP] &= ~MIP_SEIP; 
    }
}
