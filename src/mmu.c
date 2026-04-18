#include "mmu.h"
extern int j ;
extern int log_enable;

// fault codes returned by translate


int tlb_lookup(CPU_State *cpu, uint64_t va, int acc,uint64_t *pa,uint16_t asid);
void tlb_insert(CPU_State *cpu, uint64_t va, uint64_t pa, uint8_t* flags,uint16_t asid);

static inline void handle_fault(CPU_State *cpu, FaultCtx *f)
{
    cpu->mem_fault.valid = 1;
    cpu->mem_fault.vaddr = f->va;
    cpu->mem_fault.acc_type = f->acc_type;

    // ===== 关键：统一转 ISA fault =====
    switch (f->acc_type) {
        case ACC_FETCH:
            cpu->mem_fault.cause = FAULT_INST_PAGE;
            break;
        case ACC_LOAD:
            cpu->mem_fault.cause = FAULT_LOAD_PAGE;
            break;
        case ACC_STORE:
            cpu->mem_fault.cause = FAULT_STORE_PAGE;
            break;
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

int sv39_translate(CPU_State* cpu,uint64_t va,int acc_type,uint64_t *out_pa,uint8_t* flags){
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
    cpu->satp = cpu->csr[CSR_SATP];
   

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

        pte = phys_read_u64(cpu,pte_addr);
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
                
                if (!cpu->sum) return MMU_FAULT_PAGE;
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
            //case 2:  pa = (((pte >> 28) & 0x3FFFFFF) << 30) | (va & 0x3FFFFFF); break;
            //case 1:  pa = (((pte >> 19) & 0x7FFFFFFFF) << 21) | (va & 0x1FFFFF);  break;
            case 0: {
                    uint64_t pa_ppn = (pte >> 10) & ((1 << 44) - 1);
                    pa = (pa_ppn << 12) | pgoff;

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
      //  printf("---=-=-=pte:0x%08x\n",pte);
       // printf("before tlb insert  flag:0x%08x,0x%08x,0x%02x\n",va,pa,*flags);
        tlb_insert(cpu,va,pte,flags,asid);
        return MMU_OK;
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
    uint64_t vpn = (va >> 12) & 0x3FFFFFF;
    uint64_t page_off = va & 0xFFF;

    // ====================== 【关键修复】构造正确的 tag ======================
    uint64_t match_tag = vpn | ((uint64_t)asid << 26);
    // ======================================================================

    for (int i = 0; i < TLB_SIZE; i++) {
        TLBEntry *e = &cpu->tlb.entries[i];
        if (!e->valid) continue;

        // ====================== 正确匹配 ======================
        if (e->global) {
            // 全局页：只对比 VPN
            if ((e->tag & 0x3FFFFFF) != vpn) continue;
        } else {
            // 非全局页：tag 必须完全一致（VPN+ASID）
            if (e->tag != match_tag) continue;
        }
        // ======================================================

        // 命中
        e->last_used = cpu->tlb_cnt++;
        TLBResult f = tlb_check(cpu, acc, e);
        if (f != TLB_OK) return f;

        *pa = (e->ppn << 12) | page_off;
        return TLB_OK;
    }

    return TLB_MISS;
}

void tlb_insert(CPU_State *cpu, uint64_t va, uint64_t pte, uint8_t* flags, uint16_t asid) {
    uint64_t vpn = (va >> 12) & 0x3FFFFFF;
    uint64_t ppn = (pte >> 10) & 0xFFFFFFFFFFF;
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



void map_vaddr_to_paddr(CPU_State* cpu,uint64_t vaddr,uint64_t paddr,uint64_t size,uint8_t flags,uint16_t asid){

    uint64_t page_size = 0x1000;
    for(uint64_t va = vaddr; va < vaddr + size;va += page_size){

        //sv32_translate(cpu,va,ACC_STORE,paddr,flags);
        
        //tlb_insert(cpu,va,paddr,flags,asid);
        paddr += page_size;
    }

}


// handle page fault
void handle_page_fault(CPU_State *cpu, uint64_t va, int acc) {

    /*
    // 设置异常信息
    cpu->sepc = cpu->pc; // 保存异常 PC
    cpu->stval = va;     // 保存错误地址
    switch (acc) {
        case ACC_FETCH:  cpu->scause = 12; break; // 指令页面错误
        case ACC_LOAD:   cpu->scause = 13; break; // 加载页面错误
        case ACC_STORE:  cpu->scause = 15; break; // 存储页面错误
        default:         cpu->scause = 0;  break; // 未知
    }

    // 检查页表
    uint64_t vpn = va >> 12;
    uint64_t page_off = va & 0xFFF;
    uint64_t pte_addr = (cpu->satp & 0xFFFFF) << 12;
    uint64_t pte = memory_read(cpu->mem,pte_addr + vpn * 4,4);

    if (!(pte & PTE_V)) {
        // 无效 PTE，尝试分配新页面
        if (cpu->next_free_ppn * 0x1000 < cpu->mem_size) {
            uint64_t ppn = cpu->next_free_ppn++;
            uint64_t pa = (ppn << 12) | page_off;
            uint64_t flags = 0;
            if (acc == ACC_FETCH) flags |= PTE_X;
            if (acc == ACC_LOAD)  flags |= PTE_R;
            if (acc == ACC_STORE) flags |= PTE_W | PTE_R;
            // 更新页表
            uint64_t pte = (ppn << 10) | flags | PTE_V;
            memory_write(cpu->mem,pte_addr + vpn * 4, pte, 4);
            // 更新 TLB0xA00031
            tlb_insert(cpu,va,pa,flags,cpu->asid);
            // 继续执行（无需跳转）
            return;
        } else {
            fprintf(stderr, "Out of memory for page allocation: va=0x%x\n", va);
            exit(1);
        }
    } else {
        // 权限错误或其他问题
        fprintf(stderr, "Page fault: va=0x%x, acc=%d, scause=%d\n", va, acc, cpu->scause);
        exit(1);
    }

    // 如果支持操作系统，跳转到 stvec 处理程序
    if (cpu->stvec != 0) {
        cpu->pc = cpu->stvec;
        // 模拟器需实现异常处理程序的执行
    } else {
        fprintf(stderr, "No exception handler, terminating\n");
        exit(1);
    } */
}


void init_page_table(CPU_State *cpu) {
    /*uint64_t *root_page_table = malloc(PAGE_SIZE); // 4KB
    if(!root_page_t able){
        printf("failed malloc root page table\n");
    }
    

    memset(root_page_table, 0, PAGE_SIZE);
    
    uint64_t root_pa = 0x1000;
    memcpy(cpu->mem + root_pa, root_page_table, PAGE_SIZE);
    uint64_t val = memory_read(cpu->mem,cpu->mem + root_pa,4);
    */
    cpu->asid = 1;
    cpu->satp = (1 << 31) | (cpu->asid << 22) | (0x90000000 >> 12);
    //ppn = 0x90000 , ppn << 12 得到页表的起始物理地址 就是模拟器的物理地址0x90000000
    printf("init page table: cpu->satp:0x%08x\n",cpu->satp);
    uint64_t l1_tsa = (cpu->satp & ( (1 << 22) - 1)) << 12;
    uint64_t va = 0x7fff0000;
    uint64_t l0_tsa = 0xA0000 << 12;
    uint64_t vpn_i1,vpn_i0;
  
    vpn_i1 = (va >> (PAGE_OFFSET_BITS + VPN_BITS)) & ((1u << VPN_BITS) - 1);
    vpn_i0 = (va >> PAGE_OFFSET_BITS) & ((1u << VPN_BITS) - 1);
    printf("va:0x%08x,vpni0:0x%08x\n",va,vpn_i0);

    uint64_t pte_addr1 = l1_tsa + vpn_i1 * PTE_SIZE;
    printf("pte_addr1:0x%08x\n",pte_addr1);
    uint64_t pte_addr0 = l0_tsa + vpn_i0 * PTE_SIZE;
    printf("pte_addr0:0x%08x ,l0_tsa:0x%08x, vpn_i0:0x%08x\n",pte_addr0,l0_tsa,vpn_i0);
    phys_write_u32(cpu, pte_addr1, 0x28000031);// PTE[0]
    
    phys_write_u32(cpu, pte_addr0, 0x2000001F); //L0 PTE[0] -> 0x80000000

    phys_write_u32(cpu,0xA0000FC4,0x2000101F);// L0   0X80001000;

    phys_write_u32(cpu,0xA0000FC8,0x2000200F);// L0   0X80002000;
    

   
   // free(root_page_table);
    return;

}


uint64_t get_pa(CPU_State *cpu,uint64_t vaddr,int acc_type){
    uint64_t pa = 0;
    uint64_t satp = cpu->csr[CSR_SATP];
    uint8_t flags = 0;

    if (((satp >> 60) & 0xF) == 0){
        return vaddr;
    }
    int result = tlb_lookup(cpu,vaddr,acc_type,&pa,cpu->asid);

    if(result == TLB_OK){
        return pa;
    }

    if(result == TLB_FAULT){
         FaultCtx f = {
            .src = TLB_FAULT,
            .acc_type = acc_type,
            .va = vaddr
        };
        handle_fault(cpu, &f);
        return 0;
    }

    result = sv39_translate(cpu,vaddr,acc_type,&pa,&flags);
    if(result != MMU_OK){
        FaultCtx f = {
            .src = result,
            .acc_type = acc_type,
            .va = vaddr
        };
        handle_fault(cpu, &f);
        return 0;
    }
    return pa;
}