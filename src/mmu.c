#include "mmu.h"
extern int j ;
extern int log_enable;


// fault codes returned by translate


int tlb_lookup(CPU_State *cpu, uint64_t va, int acc,uint64_t *pa,uint16_t asid);
void tlb_insert(CPU_State *cpu, uint64_t va, uint64_t pa, uint8_t* flags,uint16_t asid);

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
        return MMU_OK;
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

         if(va == 0x80000f26)
            printf("[va]:0x%16lx,[pte]:0x%16lx\n",va,pte);

        if((pte & PTE_V) == 0) {
            if(log_enable) printf("111pte:0x%16lx\n",pte);
            return MMU_FAULT_PAGE;  
        }

        int is_leaf = ((pte & PTE_R) != 0) || 
                        ((pte & PTE_X) != 0) || ((pte & PTE_W) != 0);

      if(log_enable)  printf("[leaf]:%d\n",is_leaf);
        if(!is_leaf){
            if (--i < 0) return MMU_FAULT_PAGE;
            uint64_t next_ppn = (pte >> 10) & ((1 << 44) - 1);
            table_addr = next_ppn << 12;
            continue;
        }
        if(log_enable)  printf("[privilege]:%d\n",cpu->privilege);
        if (cpu->privilege == 0) { // user mode
            if ((pte & PTE_U) == 0) {
                if(log_enable) printf("111pte:0x%16lx\n",pte);
                return MMU_FAULT_PAGE;}
        } else if (cpu->privilege == 1) { // supervisor
            
            if ((pte & PTE_U) != 0) {
                if(log_enable) printf("111pte:0x%16lx\n",pte);
                if (!cpu->sum) return MMU_FAULT_PAGE;
                if (acc_type == ACC_FETCH) return MMU_FAULT_PAGE;
            }
        }
    
        if (acc_type == ACC_LOAD && !(pte & PTE_R)) {
            if (!(cpu->mxr && (pte & PTE_X))) {
                if(log_enable) printf("111pte:0x%16lx\n",pte);
                return MMU_FAULT_PAGE;}
        }
        if (acc_type == ACC_FETCH && !(pte & PTE_X)) {
            if(log_enable) printf("111pte:0x%16lx\n",pte);
            return MMU_FAULT_PAGE;}
        if (acc_type == ACC_STORE && !(pte & PTE_W)) {
            if(log_enable) printf("111pte:0x%16lx\n",pte);
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
            if(log_enable) printf("111pa:0x%16lx\n",pa);   
            return MMU_FAULT_ACCESS;
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

// -------------------- 页表步进 & 地址转换 (Sv32) --------------------
int sv32_translate(CPU_State *cpu, uint64_t va, int acc_type, uint64_t *out_pa,uint8_t* flags) {
    // bare mode -> identity
    /*  satp register,  mode:0 → Bare 模式（没有分页，VA = PA）
                             1 → Sv32 (RV32 的分页模式) // bit31
                             8 → Sv39 (RV64 的分页模式)
                             9 → Sv48    
                        ppn: page table root的物理页号
                             物理页号 × 页大小 (4KB) = 根页表的起始物理地址

      31          22 21                      0
    +--------------+------------------------+
    |   MODE       |    PPN (页表基地址)   |
    +--------------+------------------------+

        63       60 59                          0
    +-----------+----------------------------+
    |   MODE    |            PPN             |
    +-----------+----------------------------+

    */
    cpu->satp = cpu->csr[CSR_SATP];
    //printf("!!!!satp:0x%08x\n",cpu->satp);
    if ((cpu->satp & SATP_MODE_MASK) == 0) {
        *out_pa = va;
        return MMU_OK;
    }

    // assume satp.MODE == 1 (Sv32). For RV32, PPN is bits [21:0]
    uint64_t satp_ppn = cpu->satp & SATP_PPN_MASK;
    //printf("cpu->satp:0x%08x\n",cpu->satp);
    uint64_t tsa = satp_ppn << PAGE_OFFSET_BITS; // physical base of root page table
   // printf("tsa:0x%08x\n",tsa);
    int i = SV32_LEVELS - 1; // start level = 1

    /* sv32   virtual address
        31          22 21      12 11        0
        +------------+----------+-----------+
        |  VPN[1]    | VPN[0]   | page offset|
        |  10 bits   | 10 bits  | 12 bits   |

        VA[31:22] → VPN[1]  （上层页表索引）  
        VA[21:12] → VPN[0]  （下层页表索引）  
        VA[11:0]  → page offset（页内偏移）

        大页(4MB)偏移 = 22 位 ,VA[21:0]
        PPN[1] << 22 | VA[21:0]
    */

    while (1) {
        uint64_t vpn_i;
        if (i == 1) vpn_i = (va >> (PAGE_OFFSET_BITS + VPN_BITS)) & ((1u << VPN_BITS) - 1);
        else          vpn_i = (va >> PAGE_OFFSET_BITS) & ((1u << VPN_BITS) - 1);

        uint64_t pte_addr = tsa + vpn_i * PTE_SIZE;
        //printf("pte_addr:0x%08x\n",pte_addr); 
        //页表的物理起始地址 + 虚拟地址里的高10位索引号 * 4 = pte的位置(物理地址) 
        //最后计算出的物理地址都必须+ memory才可以  uint64_t* pte =(memory+a+vpn_i*4);

        if (!phys_ok(cpu, pte_addr, 4)) return MMU_FAULT_ACCESS;
        uint64_t pte = phys_read_u32(cpu, pte_addr);
       // printf("pte:0x%08x\n",pte);
        //这里的物理地址pte_addr(pte的位置)就是模拟器定义的内存数组memory的下标
        //这里从内存中读取出来的就是pte页表项的内容

        // step 3: invalid or illegal R=0 && W=1

        /* pte 内容
            0	V	Valid，有效位，指示这个 PTE 是否有效
            1	R	Read，可读
            2	W	Write，可写
            3	X	Execute，可执行
            4	U	User，可用户访问
            5	G	Global，全局
            6	A	Accessed，访问标志
            7	D	Dirty，写标志
            31-10	PPN	物理页号（指向物理页或下一级页表）

            PPN arch
            31         20 19        10
            +------------+-----------+
            |  PPN[1]    | PPN[0]    |
            |  12 bits   | 10 bits   |

            PPN[1] = bits 31:20 → 对应大页高位
            PPN[0] = bits 19:10 → 对应 Level 0 页表或小页
            Level 1 leaf 要求 PPN[0] = 0 → 对齐 4MB

        */
        if ((pte & PTE_V) == 0) return MMU_FAULT_PAGE;
        if (((pte & PTE_R) == 0) && ((pte & PTE_W) != 0)) return MMU_FAULT_PAGE;

        int is_leaf = ((pte & PTE_R) != 0) || ((pte & PTE_X) != 0);
       // printf("is_leaf:0x%08x\n",is_leaf);
        if (!is_leaf) {
       //     printf("i:%d\n",i);
            // non-leaf -> next levelis_leaf:0x00000000

            if (--i < 0) return MMU_FAULT_PAGE;
            // new a = pte.ppn * PAGE_SIZE
            uint64_t ppn1 = (pte >> 20) & ((1u << 12) - 1); // PPN[1] : bits 31:20 (12)
            uint64_t ppn0 = (pte >> 10) & ((1u << 10) - 1); // PPN[0] : bits 19:10 (10)
            uint64_t child_ppn = (pte >> 10) & ((1 << 22) - 1);//(ppn1 << 10) | ppn0;

            tsa = ((uint64_t)child_ppn << PAGE_OFFSET_BITS) & 0xFFFFFFFF;//下一层页表的物理起始地址
          //  printf("-----ppn1:0x%08x,ppn0:0x%08x,child_ppn:0x%08x,tsa:0x%08x\n",ppn1,ppn0,child_ppn,tsa);
            continue;
        }
       // printf("out loop!\n");
        // leaf PTE found -> permissions check (spec step 5)
        // Evaluate user/supervisor permission:
        if (cpu->privilege == 0) { // user mode
            if ((pte & PTE_U) == 0) {
         //       printf("PTE_U\n");
                return MMU_FAULT_PAGE;}
        } else if (cpu->privilege == 1) { // supervisor
            if ((pte & PTE_U) != 0) {
                // Supervisor trying to access U-page: allowed only if SUM=1 (and not execute)
                if (!cpu->sum) return MMU_FAULT_PAGE;
                // Note: s-mode is not allowed to execute pages with U=1 per spec
                if (acc_type == ACC_FETCH) return MMU_FAULT_PAGE;
            }
        } else { 
               // M-mode 不受页表权限限制
            // machine mode: normally bypasses page permissions (we keep simple)
            // M-mode typically ignores mmu or uses MPRV; for now allow (or let bare mode be used)
        }

        // MXR handling: if MXR=1, loads may be allowed on pages with X=1 but R=0 (we assume mxr default 0)
        if (acc_type == ACC_LOAD && !(pte & PTE_R)) {
            if (!(cpu->mxr && (pte & PTE_X))) {
             //   printf("PTE_RX\n");
                return MMU_FAULT_PAGE;}
        }
        if (acc_type == ACC_FETCH && !(pte & PTE_X)) {
           // printf("PTE_X\n");
            return MMU_FAULT_PAGE;}
        if (acc_type == ACC_STORE && !(pte & PTE_W)) {
          //  printf("PTE_W\n");
            return MMU_FAULT_PAGE;}

        // misaligned superpage check (spec step 6)
        if (i > 0) {
            // if any low PPN bits (ppn[i-1:0]) != 0 -> misaligned
            // For Sv32: if leaf at level 1, require PTE.PPN[0] == 0.
            uint64_t ppn0 = (pte >> 10) & ((1u << 10) - 1);
            if (ppn0 != 0) {
            //    printf("i > 0, ppn0 !=0\n");
                return MMU_FAULT_PAGE;}
        }

        // A/D handling (spec step 7). We choose to set A bit on access and D on store.
        // If A==0 (or D==0 for store) we update PTE (atomic in real hw; emulator approximates).
        uint64_t must_set = 0;
        if ((pte & PTE_A) == 0) must_set |= PTE_A;
        if (acc_type == ACC_STORE && (pte & PTE_D) == 0) must_set |= PTE_D;
        //硬件对页表的自动修改行为
        if (must_set) {
            // set the bits in the PTE physically
            phys_atomic_or_u32(cpu, pte_addr, must_set);
            // reload pte value for correctness (optional)
            pte = phys_read_u32(cpu, pte_addr);
           // printf("must_set pte:0x%08x\n",pte);
        }

        
        // pa.pgoff = va.pgoff
        uint64_t pgoff = va & ((1u << PAGE_OFFSET_BITS) - 1);

        // For Sv32: PTE contains PPN[1] bits 31:20 (12), PPN[0] bits 19:10 (10)
        uint64_t pte_ppn1 = (pte >> 20) & ((1u << 12) - 1);
        uint64_t pte_ppn0 = (pte >> 10) & ((1u << 10) - 1);

        uint64_t pa;
        if (i == 0) {
            // normal 4KiB page: pa.ppn[1] = pte.ppn[1], pa.ppn[0] = pte.ppn[0]
            uint64_t phys_ppn =  (pte >> 10) & ((1 << 22) - 1);//(pte_ppn1 << 10) | pte_ppn0; // 22 bits
           // printf("pgys_ppn:0x%08x\n",phys_ppn);
            pa = (phys_ppn << PAGE_OFFSET_BITS) | pgoff;
          //  printf("after cal phys:0x%08x\n",pa);
        } else {
            // superpage (i>0), pa.ppn[1:i] = pte.ppn[1:i], pa.ppn[i-1:0] = va.vpn[i-1:0]
            // For Sv32 and i==1:
            /*
            uint64_t vpn0 = (va >> PAGE_OFFSET_BITS) & ((1u << VPN_BITS) - 1);
            uint64_t phys_ppn1 = pte_ppn1;
            uint64_t phys_ppn0 = vpn0; // lower bits come from VA
            uint64_t phys_ppn = (phys_ppn1 << 10) | phys_ppn0;
            pa = (phys_ppn << PAGE_OFFSET_BITS) | pgoff;
            */
            uint64_t huge_pgoff = va & 0x003FFFFF;
            uint64_t phys_ppn1 = pte_ppn1;
            pa = phys_ppn1 << 22 | huge_pgoff;
        }

        // final PA ok?
        if (!phys_ok(cpu, pa, 1)) return MMU_FAULT_ACCESS;
        *out_pa = pa;
        uint8_t pte_flag = pte & ((1 << 7) - 1);
        *flags = pte_flag;
        uint64_t asid = cpu->asid;
      //  printf("---=-=-=pte:0x%08x\n",pte);
      //  printf("before tlb insert  flag:0x%08x,0x%08x,0x%02x\n",va,pa,*flags);
        tlb_insert(cpu,va,pa,flags,asid);

        return MMU_OK;
    }
}

// -------------------- 对外包装：读写虚拟地址（会处理跨页） --------------------
static int mmu_read(CPU_State *cpu, uint64_t va, int acc,void *buf, uint64_t len,uint16_t asid) {
    uint8_t *dst = (uint8_t*)buf;
    uint64_t off = 0;//
    cpu->mxr = cpu->csr[CSR_MSTATUS] & MSTATUS_MXR;
    

    while (off < len) {
        uint8_t flags = 0;
        uint64_t page_off = va & (PAGE_SIZE - 1);
        //页内偏移:当前访问地址在这个页中的位置
        uint64_t can = PAGE_SIZE - page_off;
        if (can > len - off) can = len - off;
        uint64_t pa;
        int ret = tlb_lookup(cpu, va, acc, &pa,asid);
        if (ret != MMU_OK) {
            // TLB 未命中，走页表
            ret = sv32_translate(cpu, va, ACC_LOAD, &pa, &flags);
            if (ret != MMU_OK) return ret;
            // 插入 TLB
            tlb_insert(cpu, va, pa, &flags,asid); 
        }
        if (!phys_ok(cpu, pa, can)) return MMU_FAULT_ACCESS;
        memcpy(dst + off, cpu->mem + pa, can);
        off += can;
        va += can;
    }
    return MMU_OK;
}

static int mmu_write(CPU_State *cpu, uint64_t va, const void *buf, uint64_t len,uint8_t* flags) {
    const uint8_t *src = (const uint8_t*)buf;
    uint64_t off = 0;
    while (off < len) {
        uint64_t page_off = va & (PAGE_SIZE - 1);
        uint64_t can = PAGE_SIZE - page_off;
        if (can > len - off) can = len - off;
        uint64_t pa;
        int ret = sv32_translate(cpu, va, ACC_STORE, &pa, flags);
        if (ret != MMU_OK) return ret;
        if (!phys_ok(cpu, pa, can)) return MMU_FAULT_ACCESS;
        memcpy(cpu->mem + pa, src + off, can);
        off += can;
        va += can;
    }
    return MMU_OK;
}


//--------------TLB---------------

static inline int tlb_check(CPU_State* cpu,int acc ,TLBEntry *e,uint64_t* pa,uint64_t page_off){


    
    if (acc == ACC_LOAD && !(e->flags & PTE_R)) {
        if (!(cpu->mxr && (e->flags & PTE_X))) 
            {   
                
                return MMU_FAULT_PAGE;
            }
    }
    if (acc == ACC_FETCH && !(e->flags & PTE_X)){
         
        return MMU_FAULT_PAGE;
    }
    if (acc == ACC_STORE && !(e->flags & PTE_W)){
        
        return MMU_FAULT_PAGE;

    }
    
    return MMU_OK;
}

int tlb_lookup(CPU_State *cpu, uint64_t va, int acc,uint64_t *pa,uint16_t asid) {
    uint64_t vpn = (va >> 12) & 0x7FFFFFF;  // 虚拟页号
    uint64_t page_off = va & 0xFFF;

    uint64_t tag_global = (vpn << 8);          // 全局页tag（无ASID）
    uint64_t tag_asid   = (vpn << 8) | asid;   // 带ASID的tag
    

   // printf("vpn: 0x%08lx,page_off:0x%08lx,tag_global:0x%08lx,tag_asid:0x%08lx\n",
   //         vpn,page_off,tag_global,tag_asid);

    for (int i = 0; i < TLB_SIZE; i++) {
        TLBEntry *e = &cpu->tlb.entries[i];


        if(!e->valid) continue;

        if(e->tag != vpn) continue;

        if(!e->global && e->asid != asid) continue;

        /*
        每个不同的 vpn 有完全不同的 tag
        全局页（G=1）：忽略 asid，直接匹配 vpn
        非全局页：vpn + asid 都匹配才命中
        */

        //命中：更新LRU，检查权限
        e->last_used = cpu->tlb_cnt++;
        
        // 组合物理地址
        *pa = (e->ppn << 12) | page_off;
        
       // printf("e->flag:%d,e->ppn:0x%08lx,*pa:0x%08lx\n",e->flags,e->ppn,*pa);
        
        return tlb_check(cpu,acc,e,pa,page_off);
        
    }
    return MMU_FAULT_ACCESS; // 未命中
}

void tlb_insert(CPU_State *cpu, uint64_t va, uint64_t pte, uint8_t* flags,uint16_t asid) {
    uint64_t vpn = (va >> 12) & 0x3FFFFFF;
    uint64_t ppn = (pte >> 10) & 0xFFFFFFFFFFF;
    bool global = (pte >> 5) & 0x1;  // G位

    int replace_idx = 0;
    uint64_t min_used = cpu->tlb.entries[0].last_used;

    
    for (int i = 1; i < TLB_SIZE; i++){
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
    entry->tag = vpn;
    entry->ppn = ppn;
    entry->asid = cpu->asid;
    entry->last_used = cpu->tlb_cnt++;

   // printf("[tlb insert] tag:0x%08lx,vpn:0x%08lx,ppn:0x%08lx\n",entry->tag,vpn,ppn);

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

    if(log_enable)
    {
        printf("vaddr:0x%16lx,satp:0x%16lx\n",vaddr,satp);
    }
    if (((satp >> 60) & 0xF) != 0){
        int result = tlb_lookup(cpu,vaddr,acc_type,&pa,cpu->asid);
        if(log_enable) printf("tlb result:%d\n",result);
        if(result != MMU_OK){
            result = sv39_translate(cpu,vaddr,acc_type,&pa,&flags);
            if(log_enable) printf("sv39 result:%d\n",result);
            if(result == MMU_FAULT_ACCESS){
                //handle_page_fault(cpu,va,ACC_FETCH);
                return 0;
            }
        }
           
        return pa;
    }
    
    pa = vaddr;

    return pa;
}