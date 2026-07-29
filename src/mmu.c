#include "mmu.h"
#include "trap.h"
#include "cpu.h"
extern int j ;
extern int log_enable;

// fault codes returned by translate


int tlb_lookup(CPU_State *cpu, uint64_t va, int acc,uint64_t *pa,uint16_t asid);
void tlb_insert(CPU_State *cpu, uint64_t va, uint64_t pa, uint8_t* flags,uint16_t asid,
            PageSize psize);

static inline void handle_fault(CPU_State *cpu, FaultCtx *f)
{

    uint64_t ret = 0;
    uint64_t cause = 0;
    cpu->mem_fault.valid = 1;
    cpu->mem_fault.vaddr = f->va;
    cpu->mem_fault.acc_type = f->acc_type;
    bool is_stage2 = false;

    if(f->src == MMU_FAULT_PAGE){
        switch (f->acc_type) {
            case ACC_FETCH:
                cause = 12;
                break;
            case ACC_LOAD:
                cause = 13;
                break;
            case ACC_STORE:
                cause = 15;
                break;
        }
    }else if(f->src >= GSTAGE_FAULT_INST){
        is_stage2 = true;
        switch (f->acc_type) {
        case ACC_FETCH:
            cause = 20;
            break;
        case ACC_LOAD:
            cause = 21;
            break;
        case ACC_STORE:
            cause = 23;
            break;
        }
    }
    cpu->mem_fault.cause = cause;

    cpu->csr[CSR_STVAL] = f->va;
    cpu->csr[HTVAL] = f->va;

    if(!is_stage2 && cpu->v){
        cpu->csr[HSTATUS] |= HSTATUS_GVA;
    }else{
        cpu->csr[HSTATUS] &= ~HSTATUS_GVA;
    }

    cpu->csr[HTINST] = 0;

    if(log_enable){
        printf("[H] :%d,V:%d\n",cpu->h_extension,cpu->v);
        printf("Hideleg:0x%16lx,Hedeleg:0x%16lx\n",cpu->csr[HIDELEG],cpu->csr[HEDELEG]);
    }
    if(cpu->h_extension && cpu->v){
        if(cause >= 12 && cause <= 15 && (cpu->csr[HEDELEG] & (1 << cause))){
            take_vsmode_trap(cpu,cause,false);
        }else{
            take_smode_trap(cpu,cause,false);
        }
    }else{
        if((1 << cause) & cpu->csr[MIDELEG_MEI]){
            take_smode_trap(cpu,cause,false);
        }else{
            take_mmode_trap(cpu,cause,false);
        }
    }

}

// -------------------- 物理内存访问（直接使用物理地址，不走 mmu） --------------------
static inline int phys_ok(CPU_State *cpu, uint64_t pa, uint64_t len) {
    // bounds check (you can hook platform-specific PMA checks here)

    if ((uint64_t)pa + len > MEMORY_BASE + MEMORY_SIZE) return 0;
    return 1;
}

uint64_t phys_read_u32(CPU_State *cpu, uint64_t pa) {
    if (!phys_ok(cpu, pa, 4)) {
        fprintf(stderr, "phys_read_u32 OOB pa=0x%08x,cpu->mem:0x%08x\n", pa,cpu->mem);
        return 0;
    }
    // little-endian
    uint64_t v = cpu->mem[pa] | (cpu->mem[pa+1] << 8) | (cpu->mem[pa+2] << 16) | (cpu->mem[pa+3] << 24);
    return v;  //这里是把内存中的4个字节的内容取出来拼接成一个32位的数据表示出来
}

void phys_write_u32(CPU_State *cpu, uint64_t pa, uint64_t v) {
    if (!phys_ok(cpu, pa, 4)) { fprintf(stderr, "phys_write_u32 OOB pa=0x%08x\n", pa); return; }
    cpu->mem[pa+0] = v & 0xff;
    cpu->mem[pa+1] = (v>>8) & 0xff;
    cpu->mem[pa+2] = (v>>16) & 0xff;
    cpu->mem[pa+3] = (v>>24) & 0xff;
}

// helper to set/clear bits atomically in PTE (emulator approximation)
static void phys_atomic_or_u32(CPU_State *cpu, uint64_t pa, uint64_t mask) {
    uint64_t old = phys_read_u32(cpu, pa);
    uint64_t nw = old | mask; // 这里的mask是置位某些位用的，例如accessed，dirty位，在计算机中这是由硬件完成的
    if (nw != old) phys_write_u32(cpu, pa, nw);
}
static void phys_atomic_or_u64(CPU_State *cpu, uint64_t pa, uint64_t mask){
    uint64_t old = 0;
    
    old = bus_read(&cpu->bus,pa,8);
    
    uint64_t nw = old | mask;
    if(nw != old){
        for(int j = 0; j < 8; j++){
            bus_write(&cpu->bus,pa,nw,8);
        }
    }
}

uint64_t phys_read_u64(CPU_State *cpu, uint64_t pa){
    uint64_t value = 0;
    
    value = bus_read(&cpu->bus,pa,8);
    
    return value;
}

int sv39_translate(CPU_State* cpu,uint64_t va,int acc_type,uint64_t *out_pa,uint8_t* flags,
                PageSize *psize){
    /*
    38        30 29        21 20        12 11         0
    +----------+-----------+-----------+------------+
    |  第2级    |   第1级   |   第0级   |  页面偏移   |
    |  (L2)    |   (L1)    |   (L0)    | (offset)   |
    +----------+-----------+-----------+------------+
        9位        9位         9位         12位

    satp:
    63      60 59      44 43                                0
    +--------+----------+-----------------------------------+
    | MODE   |   ASID   |              PPN                  |
    +--------+----------+-----------------------------------+
    4位       16位                44位

    pte:
    63      54 53      28 27      19 18      10 9   8 7 6 5 4 3 2 1 0
    +--------+----------+----------+----------+-----+-------------+
    | Reserved|   PPN[2] |   PPN[1] |   PPN[0] | RSW | D A G U X W R V |
    +--------+----------+----------+----------+-----+-------------+
    10位       26位        9位        9位      2位    1 1 1 1 1 1 1 1

    */
    static int depth = 0;
   // PageSize psize = PAGE_4KB;
    cpu->satp = read_csr(cpu,CSR_SATP);//cpu->csr[CSR_SATP];
    cpu->sum = (cpu->csr[CSR_SSTATUS] & SSTATUS_SUM) ? 1 : 0;
    #ifdef MMU_LOG
    if(log_enable){
        if(cpu->v){
            printf("sv39 use vsatp:0x%16lx\n",cpu->satp);
        }
        else{
            printf("sv39 use satp:0x%16lx\n",cpu->satp);
        }
    }
    #endif

    if( (cpu->satp >> 60 ) != 8){ //0:bare 8:sv39
        *out_pa = va;
        return FAULT_NONE;
    }

    uint64_t satp_ppn = cpu->satp & (( 1ULL << 44 ) - 1 );
    uint64_t table_addr = (satp_ppn << 12);

 
    int i = SV39_LEVELS - 1;

    while(1){
        uint64_t vpn_i = 0;

        switch (i)
        {
            case 2: vpn_i = (va >> 30) & 0x1FF; break;
            case 1: vpn_i = (va >> 21) & 0x1FF; break;
            case 0: vpn_i = (va >> 12) & 0x1FF; break;
            default: break;
        }
       

        uint64_t pte_addr = vpn_i *8 + table_addr;
        uint64_t pte = 0;

        uint64_t pte_addr_hpa = 0;
        uint8_t  G_state_ret = 0;
        #ifdef MMU_LOG
        if(log_enable){
            printf("V:%d\n",cpu->v);
            printf("pte_addr_gpa:0x%16lx\n",pte_addr);
        }
        #endif
        if(cpu->v){
            G_state_ret = gstage_translate(cpu,pte_addr,ACC_LOAD,&pte_addr_hpa,flags);
            pte = phys_read_u64(cpu,pte_addr_hpa);
        }
        else{
            pte = phys_read_u64(cpu,pte_addr);
        }
        #ifdef MMU_LOG
        if(log_enable){
            printf(" get PTE G-state-ret:%d\n",G_state_ret);
            printf("[sv39] table addr:0x%16lx,pte_addr_hpa:0x%16lx,pte:0x%16lx\n",
            table_addr,pte_addr_hpa,pte);
        }
        #endif

        if((pte & PTE_V) == 0) {
            return MMU_FAULT_PAGE;  
        }
        if(pte & PTE_W && !(pte & PTE_R)){
            return MMU_FAULT_PAGE;
        }

        int is_leaf = ((pte & PTE_R) != 0) || 
                        ((pte & PTE_X) != 0) || ((pte & PTE_W) != 0);

     
        if(!is_leaf){
            if (--i < 0) return MMU_FAULT_PAGE;
            uint64_t next_ppn = (pte >> 10) & ((1 << 44) - 1);
            table_addr = next_ppn << 12;
            continue;
        }
      
        if (cpu->privilege == 0) { // user mode
            if ((pte & PTE_U) == 0) {
                return MMU_FAULT_PAGE;}
        } else if (cpu->privilege == 1) { // supervisor
            
            if ((pte & PTE_U) != 0) {
                
                if (!cpu->sum) {
                    return MMU_FAULT_PAGE;}
                if (acc_type == ACC_FETCH) return MMU_FAULT_PAGE;
            }
        }
    
        if (acc_type == ACC_LOAD && !(pte & PTE_R)) {
            if (!(cpu->mxr && (pte & PTE_X))) {
                return MMU_FAULT_PAGE;}
        }
        if (acc_type == ACC_FETCH && !(pte & PTE_X)) {
            return MMU_FAULT_PAGE;}
        if (acc_type == ACC_STORE && !(pte & PTE_W)) {
            return MMU_FAULT_PAGE;}

        if(i > 0){
            uint64_t ppn = (pte >> 10) & ((1UL << 44) - 1);
            if (i == 2) {
            // L2级别：检查1GB大页对齐（PPN[1:0]必须为0）
                if ((ppn & ((1UL << 18) - 1)) != 0) {  // 检查低18位
                    
                    return MMU_FAULT_PAGE;  // 1 GiB大页未对齐
                }
            } else if (i == 1) {
                // L1级别：检查2 MiB大页对齐（PPN[0]必须为0）
                if ((ppn & ((1UL << 9) - 1)) != 0) {   // 检查低9位
                    
                    return MMU_FAULT_PAGE;  // 2 MiB大页未对齐
                }
            }
        }
        uint64_t must_set = 0;

        // 检查并设置访问位（任何访问都设置）
        if ((pte & PTE_A) == 0) {
            must_set |= PTE_A;
        }
        // 检查并设置脏位（只有存储操作设置）
        if (acc_type == ACC_STORE && (pte & PTE_D) == 0) {
            must_set |= PTE_D;
        }
        // 硬件对页表的自动修改行为
        /*if (must_set != 0) {
            // 使用原子操作设置位，确保多核安全
            phys_atomic_or_u64(cpu, pte_addr, must_set);  // 注意：应该是64位
            // 重新读取PTE值以保持一致性
            pte = phys_read_u64(cpu,pte_addr);
           //  printf("Updated PTE at 0x%08lx: set bits 0x%08lx, new PTE: 0x%016lx\n", 
          //          pte_addr, must_set, pte);
         //   printf("[must set] pte:%0x16lx\n",pte);
        } */

        uint64_t pa = 0;
        uint64_t pgoff = va & 0xFFF;

        /*
        页表项 (PTE) 格式：
        63          54 53        28 27        19 18        10 9   8 7 6 5 4 3 2 1 0
        +------------+------------+------------+------------+-----+---------------+
        |   Reserved |   PPN[2]   |   PPN[1]   |   PPN[0]   | RSW | D A G U X W R V |
        +------------+------------+------------+------------+-----+---------------+

        对于 1 GiB 大页：（在 L2 级别）
        - PPN[1] 和 PPN[0] 必须为 0（对齐要求）
        - 物理地址 = (PPN[2] << 30) | (va_low_30_bits)
        

        对于 2 MiB 大页：（在 L1 级别）
        - PPN[0] 必须为 0（对齐要求）
        - 物理地址 = ((PPN[2]:PPN[1]) << 21) | (va_low_21_bits)
        
        */

        switch (i)
        {
            case 2:  
                pa = (((pte >> 28) & 0x3FFFFFF) << 30) | (va & 0x3FFFFFF); 
                *psize = PAGE_1GB;
                break;
            case 1:  
                pa = (((pte >> 19) & 0x7FFFFFFFF) << 21) | (va & 0x1FFFFF);  
                *psize = PAGE_2MB;
                break;
            case 0: {
                    uint64_t pa_ppn = (pte >> 10) & ((1 << 44) - 1);
                    pa = (pa_ppn << 12) | pgoff;
                    *psize = PAGE_4KB;
                    break;
                    }
            default:            break;
        }
      //  printf("[pa] 0x%16lx\n",pa);
        if (!phys_ok(cpu, pa, 1)) {  
            return MMU_FAULT_PAGE;
        }
        *out_pa = pa;
        uint8_t pte_flag = pte & ((1 << 7) - 1);
        *flags = pte_flag;
        uint64_t asid = cpu->asid;
        bool need_stage2 = !!(cpu->v && (((cpu->csr[HGATP] >> 60) & 0XF ) != 0) );
        if(!need_stage2)
            tlb_insert(cpu,va,pte,flags,asid,*psize);
        return MMU_OK;
    }

}


/*
 * Stage-2 地址翻译 (G-stage)
 * 输入: gpa, acc_type (ACC_FETCH/ACC_LOAD/ACC_STORE)
 * 输出: *spa (成功), 返回值: 0 成功, 或异常码 20/21/23
 */
int gstage_translate(CPU_State *cpu, uint64_t gpa, int acc_type, uint64_t *spa,uint8_t* flags) {
    uint64_t hgatp = cpu->csr[HGATP];
    int mode = (hgatp >> 60) & 0xF;
    #ifdef MMU_LOG
    if(log_enable){
        printf("G-stage hgatp:0x%16lx\n",hgatp);
    }
    #endif
    // Bare 模式：GPA 即 SPA
    if (mode == 0) {
        *spa = gpa;
        return 0;
    }

    // 仅支持 Sv39x4 (mode=8)
    if (mode != 8) {
        return (acc_type == ACC_FETCH) ? GSTAGE_FAULT_INST :
               (acc_type == ACC_LOAD)  ? GSTAGE_FAULT_LOAD :
                                         GSTAGE_FAULT_STORE;
    }

    uint64_t root_ppn = hgatp & 0x0000FFFFFFFFFFFFULL; // 44-bit PPN
    uint64_t table_addr = root_ppn << 12;
    


    int i = SV39_LEVELS - 1;  // 2
    while (1) {
        // 提取当前级别的 VPN
        uint64_t vpn_i;
        switch (i) {
            case 2: vpn_i = (gpa >> 30) & 0x1FF; break;
            case 1: vpn_i = (gpa >> 21) & 0x1FF; break;
            case 0: vpn_i = (gpa >> 12) & 0x1FF; break;
            default:
                return (acc_type == ACC_FETCH) ? GSTAGE_FAULT_INST :
                       (acc_type == ACC_LOAD)  ? GSTAGE_FAULT_LOAD :
                                                 GSTAGE_FAULT_STORE;
        }

        uint64_t pte_addr = vpn_i * 8 + table_addr;
        uint64_t pte = phys_read_u64(cpu, pte_addr);
        #ifdef MMU_LOG
        if(log_enable){
            printf("i:%d,pte_addr:0x%16lx,pte:0x%16lx\n",i,pte_addr,pte);
            printf("pte_v:%d,pte_w && !pte_R:%d",pte&PTE_V,(pte & PTE_W) && !(pte & PTE_R));
        }
        #endif
        // PTE 必须有效
        if (!(pte & PTE_V)) {
           
            return (acc_type == ACC_FETCH) ? GSTAGE_FAULT_INST :
                   (acc_type == ACC_LOAD)  ? GSTAGE_FAULT_LOAD :
                                             GSTAGE_FAULT_STORE;
        }

        // 非法组合 W=1 且 R=0
        if ((pte & PTE_W) && !(pte & PTE_R)) {
            return (acc_type == ACC_FETCH) ? GSTAGE_FAULT_INST :
                   (acc_type == ACC_LOAD)  ? GSTAGE_FAULT_LOAD :
                                             GSTAGE_FAULT_STORE;
        }

        // 判断是否为叶节点（R/W/X 任一置位）
        int is_leaf = (pte & PTE_R) || (pte & PTE_X) || (pte & PTE_W);
        #ifdef MMU_LOG
        if(log_enable){
            printf("is_leaf:%d\n",is_leaf);
        }
        #endif
        if (!is_leaf) {
            if (--i < 0){
                if(log_enable){
                    printf("after --i:%d\n",i);
                }
                return (acc_type == ACC_FETCH) ? GSTAGE_FAULT_INST :
                       (acc_type == ACC_LOAD)  ? GSTAGE_FAULT_LOAD :
                                                 GSTAGE_FAULT_STORE;
            }
            uint64_t next_ppn = (pte >> 10) & ((1ULL << 44) - 1);
            table_addr = next_ppn << 12;
            #ifdef MMU_LOG
            if(log_enable){
                printf("next_ppn:0x%16lx,table_addr:0x%16lx\n",next_ppn,table_addr);
            }
            #endif

            continue;
        }


        // ---------- 权限检查（仅 R/W/X）----------
        if (acc_type == ACC_LOAD && !(pte & PTE_R))
            return GSTAGE_FAULT_LOAD;
        if (acc_type == ACC_FETCH && !(pte & PTE_X))
            return GSTAGE_FAULT_INST;
        if (acc_type == ACC_STORE && !(pte & PTE_W))
            return GSTAGE_FAULT_STORE;

        // ---------- 大页对齐检查 ----------
        if (i > 0) {
            uint64_t ppn_field = (pte >> 10) & ((1ULL << 44) - 1);
            if (i == 2) { // 1 GiB 页，低 18 位必须为 0
                if (ppn_field & ((1ULL << 18) - 1))
                    return (acc_type == ACC_FETCH) ? GSTAGE_FAULT_INST :
                           (acc_type == ACC_LOAD)  ? GSTAGE_FAULT_LOAD :
                                                     GSTAGE_FAULT_STORE;
            } else if (i == 1) { // 2 MiB 页，低 9 位必须为 0
                if (ppn_field & ((1ULL << 9) - 1))
                    return (acc_type == ACC_FETCH) ? GSTAGE_FAULT_INST :
                           (acc_type == ACC_LOAD)  ? GSTAGE_FAULT_LOAD :
                                                     GSTAGE_FAULT_STORE;
            }
        }

        // ---------- 硬件自动更新 A/D 位 ----------
        /*
        uint64_t must_set = 0;
        if (!(pte & PTE_A)) must_set |= PTE_A;
        if (acc_type == ACC_STORE && !(pte & PTE_D)) must_set |= PTE_D;
        if (must_set) {
            phys_write_u64(cpu, pte_addr, pte | must_set);
            pte = phys_read_u64(cpu, pte_addr); // 刷新 pte
        } */

        // ---------- 计算最终物理地址 (SPA) ----------
        uint64_t pa = 0;
        uint64_t pgoff = gpa & 0xFFF;
        #ifdef MMU_LOG
        if(log_enable)
            printf("pgoff:0x%16lx,i:%d,pte:0x%16lx\n",pgoff,i,pte);
        #endif
        switch (i) {
            case 2:  // 1 GiB 大页
                pa = (((pte >> 28) & 0x3FFFFFF) << 30) | (gpa & 0x3FFFFFF);
                break;
            case 1:  // 2 MiB 大页
                pa = (((pte >> 19) & 0x7FFFFFFFF) << 21) | (gpa & 0x1FFFFF);
                break;
            case 0:  // 4 KiB 页
                pa = (((pte >> 10) & ((1ULL << 44) - 1)) << 12) | pgoff;
                break;
        }
        #ifdef MMU_LOG
        if(log_enable)
            printf("pa:0x%16lx\n",pa);
        #endif
        *spa = pa;
        *flags = pte & ((1 << 7) - 1);
        return 0; // 成功
    }
}

//--------------TLB---------------

static inline int tlb_check(CPU_State* cpu, int acc, TLBEntry *e) {
    // 1. 页面无效 → 直接失败
    if ((e->flags & PTE_V) == 0) {
        return TLB_FAULT;
    }

    // 2. U/S 权限检查（RISC-V 标准）
    if (cpu->privilege == 0) { // U 模式
        // U 模式只能访问 PTE_U=1 的页面
        if (!(e->flags & PTE_U)) {
            return TLB_FAULT;
        }
    } else if(cpu->privilege == 1){ // S 模式
        // S 模式 + SUM=0 时，禁止访问用户页面
        if (!(cpu->sum) && (e->flags & PTE_U)) {
            return TLB_FAULT;
        }
    }

    // 3. 执行权限检查（必须和 U 权限一起检查）
    if (acc == ACC_FETCH) {
        if (!(e->flags & PTE_X)) {
            return TLB_FAULT;
        }
        // U 模式取指必须是 U 页面（你之前漏了！）
        if (cpu->privilege == 0 && !(e->flags & PTE_U)) {
            return TLB_FAULT;
        }
    }

    // 4. 读权限检查（包含 MXR 正确逻辑）
    if (acc == ACC_LOAD) {
        if (!(e->flags & PTE_R)) {
            // MXR：可执行 = 可读
            if (!cpu->mxr || !(e->flags & PTE_X)) {
                return TLB_FAULT;
            }
        }
    }

    // 5. 写权限检查
    if (acc == ACC_STORE && !(e->flags & PTE_W)) {
        return TLB_FAULT;
    }

    // 6. A/D 位置位（硬件自动更新）
    if (!(e->flags & PTE_A)) {
        e->flags |= PTE_A;
    }
    if (acc == ACC_STORE && !(e->flags & PTE_D)) {
        e->flags |= PTE_D;
    }

    return TLB_OK;
}

int tlb_lookup(CPU_State *cpu, uint64_t va, int acc, uint64_t *pa, uint16_t asid) {
    uint64_t vpn = 0;
    uint64_t page_off = 0;
    uint64_t match_tag = 0;
    uint64_t shift = 0;

    // 计算当前有效的 VMID
    uint64_t hgatp = cpu->csr[HGATP];
    uint16_t vmid = 0;
    bool stage2_active = !!(cpu->v && ((hgatp >> 60) & 0xF) != 0);
    if (stage2_active) {
        // V=1 且 hgatp 非 Bare，使用 hgatp 的 ASID 域作为 VMID
        vmid = (uint16_t)(hgatp & 0xFFFF);
    }
    #ifdef TLB_LOG
    if(log_enable){
        printf("[tbl_looup] vmid:0x%16lx\n",vmid);
    }
    #endif

    for (int i = 0; i < TLB_SIZE; i++) {
        TLBEntry *e = &cpu->tlb.entries[i];

        if (!e->valid) continue;

        // 根据条目记录的页面大小计算 VPN 和偏移
        switch (e->page_size) {
            case PAGE_4KB:
                shift = 12;
                vpn = (va >> 12) & 0x7FFFFFF;
                page_off = va & 0xFFF;
                break;
            case PAGE_2MB:
                shift = 21;
                vpn = (va >> 21) & 0x3FFFF;
                page_off = va & 0x1FFFFF;
                break;
            case PAGE_1GB:
                shift = 30;
                vpn = (va >> 30) & 0x1FF;
                page_off = va & 0x3FFFFFFF;
                break;
            default:
                continue;   // 无效大小
        }

        // 构造期望的 tag：统一格式
        match_tag = vpn | ((uint64_t)asid << 26) | ((uint64_t)vmid << 48)| (uint64_t)(e->page_size) << 62 ;
        #ifdef TLB_LOG
        if(log_enable){
            printf("[tlb_lookup] vpn:0x%16lx,asid:0x%16lx,vmid:0x%16lx",
                    vpn,asid,vmid);
            printf("[tlb_lookup] match_tag:0x%16lx\n",match_tag);
        }
        #endif

        // 匹配 tag
        if (e->global) {
            // 全局页：只对比 VPN（忽略 ASID/VMID）
            // 注意：全局页应确保在所有地址空间中映射相同
            if ((e->tag & 0x3FFFFFF) != vpn) continue;   // 取出低 26 位 VPN
        } else {
            // 非全局页：tag 必须完全一致（包含 ASID、VMID）
            if (e->tag != match_tag) continue;
        }
        if (stage2_active && e->vmid == 0) continue; // 忽略单阶段条目

        // 命中，更新 LRU
        e->last_used = cpu->tlb_cnt++;

        // 权限检查（与原来一致）
        TLBResult f = tlb_check(cpu, acc, e);
        if (f != TLB_OK) return f;
        #ifdef TLB_LOG
        if(log_enable){
            printf("[tlb_lookup] e->ppn:0x%16lx\n",e->ppn);
        }
        #endif

        // 返回物理地址
        *pa = (e->ppn << shift) | page_off;
        return TLB_OK;
    }

    return TLB_MISS;
}
void tlb_insert(CPU_State *cpu, uint64_t va, uint64_t pte, uint8_t* flags, uint16_t asid,
                    PageSize psize) {
    uint64_t vpn = 0;
    uint64_t ppn = 0;
    switch (psize)
    {
    case PAGE_4KB:
        vpn = (va >> 12) & 0x3FFFFFF;
        ppn = (pte >> 10) & 0xFFFFFFFFFFF;
        break;
    case PAGE_2MB:
        vpn = (va >> 21) & 0x1FFFFF;
        ppn = (pte >> 19) & 0x7FFFFFFFF;
        break;
    case PAGE_1GB:
        vpn = (va >> 30) & 0x3FF;
        ppn = (pte >> 28) & 0x3FFFFFF;
        break;
    default:
        break;
    }
                        
    bool global = (pte >> 5) & 0x1;  // G位

    // LRU 替换
    int replace_idx = 0;
    uint64_t min_used = cpu->tlb.entries[0].last_used;

    for (int i = 1; i < TLB_SIZE; i++) {
        if (!cpu->tlb.entries[i].valid) {
            replace_idx = i;
            break;
        }
        if (cpu->tlb.entries[i].last_used < min_used) {
            min_used = cpu->tlb.entries[i].last_used;
            replace_idx = i;
        }
    }

    TLBEntry* entry = &cpu->tlb.entries[replace_idx];
    entry->valid = true;
    entry->global = global;
    entry->ppn = ppn;
    entry->asid = asid;
    entry->last_used = cpu->tlb_cnt++;
    entry->page_size = psize;

    if (global) {
        // 全局页：只存 VPN
        entry->tag = vpn;
    } else {
        // 非全局页：tag = VPN + ASID（不同进程永远不会冲突）
        entry->tag = vpn | ((uint64_t)asid << 26);  // ASID 放在高位
    }

    // 权限位
    entry->flags = 0;
    if (pte & 0x1) entry->flags |= PTE_V;
    if (pte & 0x2) entry->flags |= PTE_R;
    if (pte & 0x4) entry->flags |= PTE_W;
    if (pte & 0x8) entry->flags |= PTE_X;
    if (pte & 0x10) entry->flags |= PTE_U;
    if (pte & 0x40) entry->flags |= PTE_A;
    if (pte & 0x80) entry->flags |= PTE_D;
}


void tlb_flush(CPU_State* cpu){

    for(int i = 0; i < TLB_SIZE; i++){
        TLBEntry *e = &cpu->tlb.entries[i];
        e->valid = 0;
    }
    cpu->tlb.next_replace = 0;
}
/*
 * tlb_insert_2stage - 插入 GVA -> HPA 映射到 TLB
 * cpu:      CPU 状态
 * gva:      Guest 虚拟地址
 * spa_ppn:  SPA 的物理页号 (spa >> 12)
 * merged_flags: 合并后的权限位 (Stage-1 权限 & Stage-2 权限)，格式同 PTE 低 8 位
 * asid:     Stage-1 地址空间 ID (来自 vsatp 或 satp)
 * vmid:     Stage-2 虚拟机 ID (来自 hgatp 的 ASID 字段)
 * psize:    页面大小 (PAGE_4KB / PAGE_2MB / PAGE_1GB)
 */
void tlb_insert_2stage(CPU_State *cpu, uint64_t gva, uint64_t hpa,
                       uint8_t merged_flags, uint16_t asid, uint16_t vmid,
                       PageSize psize) {
    // 计算 VPN（根据页面大小）
    #ifdef TLB_LOG
    if(log_enable){
        printf("[tlb_insert]gva:0x%16lx,hpa_ppn:0x%16lx, psize:0x%16lx\n",
                gva,hpa,psize);
    }
    #endif
    uint64_t vpn,hpa_ppn;
    uint64_t sv39 = gva & 0x7FFFFFFFFF;
    switch (psize) {
        case PAGE_4KB:
            vpn = (sv39 >> 12) & 0x7FFFFFF;
            hpa_ppn = hpa >> 12 ;
            break;
        case PAGE_2MB:
            vpn = (sv39 >> 21) & 0x3FFFF;
            hpa_ppn = hpa >> 21;
            break;
        case PAGE_1GB:
            vpn = (sv39 >> 30) & 0x1FF;
            hpa_ppn = hpa >> 30;
            break;
        default:
            return;
    }

    #ifdef TLB_LOG
    if(log_enable){
        printf("[tlb insert] vpn:0x%16lx,asid:0x%16lx,vmid:0x%16lx\n",
                vpn,asid,vmid);
    }
    #endif

    // 构造 tag
    uint64_t tag = vpn | ((uint64_t)asid << 26) | ((uint64_t)vmid << 48) | (uint64_t)psize << 62;
    bool global = (merged_flags & PTE_G) != 0;  // 注意：PTE_G 在合并权限中可能被清除，实践中一般不用

    // LRU 替换（与原函数相同）
    int replace_idx = 0;
    uint64_t min_used = cpu->tlb.entries[0].last_used;
    for (int i = 0; i < TLB_SIZE; i++) {
        if (!cpu->tlb.entries[i].valid) {
            replace_idx = i;
            break;
        }
        if (cpu->tlb.entries[i].last_used < min_used) {
            min_used = cpu->tlb.entries[i].last_used;
            replace_idx = i;
        }
    }

    TLBEntry *entry = &cpu->tlb.entries[replace_idx];
    entry->valid = true;
    entry->global = global;
    entry->ppn = hpa_ppn;               // 直接存 SPA 的物理页号
    entry->asid = asid;
    entry->vmid = vmid;                 // 记录虚拟机 ID
    entry->page_size = psize;
    entry->flags = merged_flags;        // 合并后的完整权限
    entry->last_used = cpu->tlb_cnt++;  // LRU 计数器

    // tag 处理
    if (global) {
        // 全局页：只靠 VPN 匹配（通常不需要，因为 G 位在虚拟化下一般禁用）
        entry->tag = vpn;
    } else {
        entry->tag = tag;               // 非全局：VPN + ASID + VMID
    }
    #ifdef TLB_LOG
    if(log_enable){
        printf("[tlb_inset] tag:0x%16lx\n",entry->tag);
    }
    #endif
}


void map_vaddr_to_paddr(CPU_State* cpu,uint64_t vaddr,uint64_t paddr,uint64_t size,uint8_t flags,uint16_t asid){

    uint64_t page_size = 0x1000;
    for(uint64_t va = vaddr; va < vaddr + size;va += page_size){

        //sv32_translate(cpu,va,ACC_STORE,paddr,flags);
        
        //tlb_insert(cpu,va,paddr,flags,asid);
        paddr += page_size;
    }

}

void take_smode_fault(CPU_State *cpu, uint64_t cause, bool is_interrupt) {
    // 1. 设置 scause 寄存器
    write_csr(cpu, CSR_SCAUSE, cause);

    // 2. 设置 stval 寄存器（通常是导致异常的地址）
    write_csr(cpu, CSR_STVAL, cpu->mem_fault.vaddr);

    // 3. 切换到 S 模式并跳转到异常处理程序
    
    write_csr(cpu, CSR_SEPC, cpu->pc); // 保持原来的 PC

    uint64_t sstatus = read_csr(cpu,CSR_SSTATUS);
    // 4.把当前sie保存到spie
    if (sstatus & SSTATUS_SIE) {
        sstatus |= SSTATUS_SPIE;
    } else {
        sstatus &= ~SSTATUS_SPIE;
    }
    // 5. 关闭中断（清SIE位）
    sstatus &= ~SSTATUS_SIE;

    // 4. 设置SPP位为当前特权级（0=U, 1=S）
    uint64_t spp = (cpu->privilege == 1) ? 1 : 0; // 1=S, 0=U
    sstatus = (sstatus & ~SSTATUS_SPP) | (spp << 8);
    
    write_csr(cpu,CSR_SSTATUS,sstatus);
    // 7. 设置特权级为S
    if(cpu->privilege != 1)
        cpu->privilege = 1;

    // 跳转到 S 模式的异常处理程序
    cpu->pc = cpu->csr[CSR_STVEC] & ~0x3ULL; 
    if(log_enable)
        printf("[hd S mode trap] jump to: 0x%16lx\n",cpu->pc);
}

void take_mmode_fault(CPU_State *cpu, uint64_t cause, bool is_interrupt) {
    // 1. 设置 mcause 寄存器
    write_csr(cpu, CSR_MCAUSE, cause);

    // 2. 设置 mtval 寄存器（通常是导致异常的地址）
    write_csr(cpu, CSR_MTVAL, cpu->mem_fault.vaddr);
    // 3. 切换到 M 模式并跳转到异常处理程序
    
    write_csr(cpu, CSR_MEPC, cpu->pc); // 保持原来的 PC
    if(log_enable){
        printf("[hardware M mode trap] csr_mepc:0x%16lx\n",cpu->csr[CSR_MEPC]);
    }
    uint64_t mstatus = read_csr(cpu,CSR_MSTATUS);
    // 4.把当前mie保存到mpie
    if (mstatus & MSTATUS_MIE) {
        mstatus |= MSTATUS_MPIE;
    } else {
        mstatus &= ~MSTATUS_MPIE;
    }
    // 5. 关闭中断（清MIE位）
    mstatus &= ~MSTATUS_MIE;

    // 4. 设置MPP位为当前特权级
    uint64_t mpp = cpu->privilege ;
    mstatus = (mstatus & ~MSTATUS_MPP_MASK) | (mpp << 11);
    
    write_csr(cpu,CSR_MSTATUS,mstatus);
    // 7. 设置特权级为M
    if(cpu->privilege != 3)
        cpu->privilege = 3;

    // 跳转到 M 模式的异常处理程序
    cpu->pc = cpu->csr[CSR_MTVEC] & ~0x3ULL; 
    if(log_enable)
        printf("[hd M mode trap] jump to: 0x%16lx\n",cpu->pc);
}

uint64_t get_pa(CPU_State *cpu,uint64_t gva,int acc_type){
    uint64_t gpa = 0;
    uint64_t satp = read_csr(cpu,CSR_SATP);
    uint8_t stage1_flags = 0;
    uint8_t stage2_flags = 0;
    uint64_t hgatp = cpu->csr[HGATP];
    PageSize psize;
    cpu->asid = (cpu->v ? cpu->vsatp : cpu->csr[CSR_SATP]) & 0xFFFF;
    #ifdef MMU_LOG
    if(log_enable){
        if(cpu->v)
            printf("[get_pa] vsatp:0x%16lx,pri:%d\n",satp,cpu->privilege);
        else
            printf("[get_pa] satp:0x%16lx,pri:%d\n",satp,cpu->privilege);

    }
    #endif

    if (((satp >> 60) & 0xF) == 0 || cpu->privilege == 3) { // M 模式 || SATP 模式为 BARE
        gpa = gva;
        #ifdef MMU_LOG
        if(log_enable)
            printf("gva :0x%16lx,gpa:0x%16lx\n",gva,gpa);
        #endif
        if (cpu->v && ((hgatp >> 60) & 0xF) != 0) {
            if(log_enable)
                printf("V:%d,hgatp:0x%16lx\n",cpu->v,hgatp);
            uint64_t hpa;
            uint8_t stage2_flags;
            int res = gstage_translate(cpu, gpa, acc_type, &hpa, &stage2_flags);
            if (res != MMU_OK) {
                FaultCtx f = { .src = res, .acc_type = acc_type, .va = gpa };
                handle_fault(cpu, &f);
                return 0;
            }
            return hpa;
        }
        return gpa; // 真正不需要 Stage‑2
        
    }
    int result = tlb_lookup(cpu,gva,acc_type,&gpa,cpu->asid);
    if(log_enable)
        printf("[TLB result]:%d\n",result);

    if(result == TLB_OK){
        return gpa;
    }
    /*
    if(result == TLB_FAULT){
         FaultCtx f = {
            .src = TLB_FAULT,
            .acc_type = acc_type,
            .va = gva
        };
        handle_fault(cpu, &f);
        return 0;
    } */

    result = sv39_translate(cpu,gva,acc_type,&gpa,&stage1_flags,&psize);
    
    if(result != MMU_OK){
        FaultCtx f = {
            .src = result,
            .acc_type = acc_type,
            .va = gva
        };
        handle_fault(cpu, &f);
        return 0;
    }
    if(cpu->v && (((hgatp >> 60) & 0xff) != 0)){
        uint64_t hpa;

        int result = gstage_translate(cpu,gpa,acc_type,&hpa,&stage2_flags);

        if(result != MMU_OK){
             FaultCtx f = {
            .src = result,
            .acc_type = acc_type,
            .va = gpa
            };

         handle_fault(cpu,&f);
         return 0;
        }

        uint8_t merged_flags = stage1_flags & stage2_flags;
        uint16_t asid = (cpu->v ? cpu->vsatp : cpu->satp) & 0xFFFF; 
        uint16_t vmid = cpu->csr[HGATP] & 0xFFFF; 
        

        tlb_insert_2stage(cpu,gva,hpa,merged_flags,asid,vmid,psize);
        return hpa;
    }
   
    return gpa;
}