// elf_loader.c  -- RV32 ELF loader (minimal, synchronous)
#include "elf_load.h"

extern uint8_t* memory;
/*
 * load_elf32:
 *   path     : ELF 文件路径
 *   mem      : 指向模拟器内存的指针（字节数组）
 *   mem_size : 模拟器内存大小（字节）
 *   mem_base : 该内存对应的起始客地址（例如 0x80000000 或 0）
 *   cpu      : 指向 CPU 状态结构，用于设置 pc / sp
 *
 * 返回 0 表示成功，非 0 表示失败（并打印错误到 stderr）。
 */

int load_elf32_bare(const char *path, uint8_t *mem, size_t mem_size, uint32_t mem_base, CPU_State *cpu) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror("fopen");
        return -1;
    }
    if (path) {
        printf("path: %s\n", path);
    } else {
        printf("path: NULL\n");
    }

    Elf32_Ehdr eh;
    if (fread(&eh, 1, sizeof(eh), f) != sizeof(eh)) {
        fprintf(stderr, "failed to read ELF header\n");
        fclose(f);
        return -1;
    }

    /* basic checks */
    if (memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0) {
        fprintf(stderr, "not an ELF file\n");
        fclose(f);
        return -1;
    }
    if (eh.e_ident[EI_CLASS] != ELFCLASS32) {
        fprintf(stderr, "only ELF32 supported by this loader\n");
        fclose(f);
        return -1;
    }
    if (eh.e_ident[EI_DATA] != ELFDATA2LSB) {
        fprintf(stderr, "big-endian ELF not supported\n");
        fclose(f);
        return -1;
    }
    if (eh.e_machine != EM_RISCV) {
        fprintf(stderr, "ELF is not for RISC-V (e_machine=%u)\n", eh.e_machine);
        fclose(f);
        return -1;
    }

    if (eh.e_phoff == 0 || eh.e_phnum == 0) {
        fprintf(stderr, "no program headers\n");
        fclose(f);
        return -1;
    }

    /* find min/max vaddr among PT_LOAD */
    uint32_t min_vaddr = UINT32_MAX;
    uint32_t max_vaddr = 0;
    for (int i = 0; i < eh.e_phnum; i++) {
        if (fseek(f, eh.e_phoff + i * eh.e_phentsize, SEEK_SET) != 0) {
            perror("fseek");
            fclose(f);
            return -1;
        }
        Elf32_Phdr ph;
        if (fread(&ph, 1, sizeof(ph), f) != sizeof(ph)) {
            fprintf(stderr, "failed to read program header\n");
            fclose(f);
            return -1;
        }
        if (ph.p_type != PT_LOAD) continue;
        if (ph.p_vaddr < min_vaddr) min_vaddr = ph.p_vaddr;
        uint32_t seg_end = ph.p_vaddr + ph.p_memsz;
        if (seg_end > max_vaddr) max_vaddr = seg_end;
        printf("p_vaddr:0x%08x,min_vaddr:0x%08x,seg end:0x%08x,max_vaddr:0x%08x\n",
        ph.p_vaddr,min_vaddr,seg_end,max_vaddr);
    }

    if (min_vaddr == UINT32_MAX) {
        fprintf(stderr, "no loadable segments (PT_LOAD)\n");
        fclose(f);
        return -1;
    }

    uint64_t needed = (uint64_t)max_vaddr - (uint64_t)min_vaddr;
    if (needed > mem_size) {
        fprintf(stderr, "not enough memory: need 0x%08x bytes, have 0x%08x\n",
                (unsigned long long)needed, mem_size);
        fclose(f);
        return -1;
    }

    /* map min_vaddr -> mem_base */
    int64_t load_bias = (int64_t)mem_base - (int64_t)min_vaddr;

    /* actually load each PT_LOAD */
    for (int i = 0; i < eh.e_phnum; i++) {
        if (fseek(f, eh.e_phoff + i * eh.e_phentsize, SEEK_SET) != 0) {
            perror("fseek");
            fclose(f);
            return -1;
        }
        Elf32_Phdr ph;
        if (fread(&ph, 1, sizeof(ph), f) != sizeof(ph)) {
            fprintf(stderr, "failed to read program header\n");
            fclose(f);
            return -1;
        }
        if (ph.p_type != PT_LOAD) continue;

        uint64_t dest = (uint64_t)ph.p_vaddr + load_bias; /* guest addr -> mem index = dest - mem_base */
        if (dest < mem_base || dest + ph.p_memsz > mem_base + mem_size) {
            fprintf(stderr, "segment doesn't fit in emulator memory (dest=0x%llx, mem_base=0x%llx, mem_size=0x%zx)\n",
                    (unsigned long long)dest, (unsigned long long)mem_base, mem_size);
            fclose(f);
            return -1;
        }

        size_t mem_off = (size_t)(dest - mem_base);

        /* read file-backed bytes */
        if (ph.p_filesz > 0) {
            if (fseek(f, ph.p_offset, SEEK_SET) != 0) { perror("fseek"); fclose(f); return -1; }
            size_t got = fread(mem + mem_off, 1, ph.p_filesz, f);
            if (got != ph.p_filesz) {
                fprintf(stderr, "failed reading segment data: got %zu expected %u\n", got, (unsigned)ph.p_filesz);
                fclose(f);
                return -1;
            }
        }

        /* zero the rest (bss) */
        if (ph.p_memsz > ph.p_filesz) {
            memset(mem + mem_off + ph.p_filesz, 0, ph.p_memsz - ph.p_filesz);
        }
    }

    /* set PC and SP (stack) */
    uint64_t entry = (uint64_t)eh.e_entry + load_bias;
    printf("eh.e_entry:0x%08x,load_bias:0x%08x\n",eh.e_entry,load_bias);
    cpu->pc = (uint32_t)entry;
    /* set sp (x2) to top of memory, 16-byte aligned */
    uint64_t sp = (mem_base + mem_size) & ~((uint64_t)0xF);
    cpu->gpr[2] = (uint32_t)sp;
    //printf("gpr[2]:0x%08x\n",cpu->gpr[2]);

    uint8_t pc_index = entry - min_vaddr;
    //printf("pc index:0x%08x",pc_index);

    fclose(f);
    return 0;

    // index
    // pt_load index = p_vaddr -min_vaddr;
    // pc index = entry - min_vaddr;
}
/*
void load_elf32_virt(CPU_State* cpu,const char *filename, uint32_t *entry_point) {

     FILE *file = fopen(filename, "rb");
    Elf32_Ehdr ehdr;
    if(!fread(&ehdr, sizeof(Elf32_Ehdr), 1, file)){
        perror("fread err\n");
    }

    if (ehdr.e_ident[EI_MAG0] != ELFMAG0 || ehdr.e_machine != EM_RISCV) {
        fprintf(stderr, "Invalid ELF file\n");
        fclose(file);
        return;
    }
    *entry_point = (uint32_t)ehdr.e_entry;
    Elf32_Phdr phdr;
    fseek(file, ehdr.e_phoff, SEEK_SET);
    for (int i = 0; i < ehdr.e_phnum; i++) {
       
        if(!fread(&phdr, sizeof(Elf32_Phdr), 1, file)){
            perror("phdr fread error\n");
        }
        if (phdr.p_type == PT_LOAD) {
            uint32_t phys_addr = (uint32_t)phdr.p_paddr;
            fseek(file, phdr.p_offset, SEEK_SET);
            printf("paddr:0x%08x,filesize:0x%08x\n",phdr.p_paddr,phdr.p_filesz);
            if(!fread(memory + (phys_addr - MEMORY_BASE), phdr.p_filesz, 1, file)){
                perror("memory error\n");
            }
            uint32_t flags = 0;
            if (phdr.p_flags & PF_R) flags |= PTE_R;
            if (phdr.p_flags & PF_W) flags |= PTE_W;
            if (phdr.p_flags & PF_X) flags |= PTE_X;
            printf("phdr.p_vaddr:0x%08x,phys_addr:0x%08x\n",phdr.p_vaddr,phdr.p_paddr);
            map_vaddr_to_paddr(cpu,phdr.p_vaddr, phdr.p_paddr, phdr.p_memsz, flags,cpu->asid);
        }
    }
   // printf("p_vaddr:0x%08x , phys_addr:0x%08x\n",phdr.p_vaddr,phdr.p_paddr);

    fclose(file);
}*/
void load_elf32_virt(CPU_State* cpu, const char *filename, uint32_t *entry_point) {
    FILE *file = fopen(filename, "rb");
    Elf32_Ehdr ehdr;
    
    if(!fread(&ehdr, sizeof(Elf32_Ehdr), 1, file)){
        perror("fread err\n");
    }

    if (ehdr.e_ident[EI_MAG0] != ELFMAG0 || ehdr.e_machine != EM_RISCV) {
        fprintf(stderr, "Invalid ELF file\n");
        fclose(file);
        return;
    }
    
    *entry_point = (uint32_t)ehdr.e_entry;
    
    printf("=== ELF文件信息 ===\n");
    printf("程序头数量: %d\n", ehdr.e_phnum);
    printf("程序头偏移: 0x%x\n", ehdr.e_phoff);
    printf("程序头大小: %d\n", ehdr.e_phentsize);
    printf("入口点: 0x%08x\n", *entry_point);
    
    int segments_loaded = 0;
    
    for (int i = 0; i < ehdr.e_phnum; i++) {
        Elf32_Phdr phdr;
        
        // 正确计算每个程序头的位置
        fseek(file, ehdr.e_phoff + i * ehdr.e_phentsize, SEEK_SET);
        
        if(!fread(&phdr, sizeof(Elf32_Phdr), 1, file)){
            perror("phdr fread error\n");
            continue;
        }
        
        printf("\n--- 程序头 %d ---\n", i);
        printf("类型: 0x%x", phdr.p_type);
        if (phdr.p_type == PT_LOAD) printf(" (LOAD)");
        else if (phdr.p_type == PT_INTERP) printf(" (INTERP)");
        else if (phdr.p_type == PT_DYNAMIC) printf(" (DYNAMIC)");
        else printf(" (其他)");
        printf("\n");
        
        if (phdr.p_type == PT_LOAD) {
            printf("虚拟地址: 0x%08x\n", phdr.p_vaddr);
            printf("物理地址: 0x%08x\n", phdr.p_paddr);
            printf("文件大小: 0x%x\n", phdr.p_filesz);
            printf("内存大小: 0x%x\n", phdr.p_memsz);
            printf("文件偏移: 0x%x\n", phdr.p_offset);
            printf("标志: 0x%x ", phdr.p_flags);
            if (phdr.p_flags & PF_R) printf("R");
            if (phdr.p_flags & PF_W) printf("W");
            if (phdr.p_flags & PF_X) printf("X");
            printf("\n");
            
            uint32_t phys_addr = (uint32_t)phdr.p_paddr;
            
            // 检查物理地址是否有效
            if (phys_addr < MEMORY_BASE || phys_addr + phdr.p_filesz >= MEMORY_BASE + MEMORY_SIZE) {
                printf("❌ 错误: 物理地址超出范围! 0x%08x - 0x%08x\n", 
                       phys_addr, phys_addr + phdr.p_filesz);
                continue;
            }
            
            fseek(file, phdr.p_offset, SEEK_SET);
            
            printf("加载到物理内存: 0x%08x\n", phys_addr);
            size_t bytes_read = fread(memory + (phys_addr - MEMORY_BASE), 1, phdr.p_filesz, file);
            printf("实际读取: %zu 字节\n", bytes_read);
            
            // 验证加载
            uint32_t first_word = *(uint32_t*)(memory + (phys_addr - MEMORY_BASE));
            printf("加载后首字: 0x%08x\n", first_word);
            
            uint32_t flags = 0;
            if (phdr.p_flags & PF_R) flags |= PTE_R;
            if (phdr.p_flags & PF_W) flags |= PTE_W;
            if (phdr.p_flags & PF_X) flags |= PTE_X;
            
            printf("建立映射: 虚拟 0x%08x → 物理 0x%08x\n", phdr.p_vaddr, phdr.p_paddr);
            map_vaddr_to_paddr(cpu, phdr.p_vaddr, phdr.p_paddr, phdr.p_memsz, flags, cpu->asid);
            
            segments_loaded++;
        }
    }
    
    printf("\n=== 加载完成 ===\n");
    printf("成功加载 %d 个段\n", segments_loaded);
    printf("期望加载: %d 个段 (根据程序头数量)\n", ehdr.e_phnum);

    fclose(file);
}


int load_elf64_SBI(const char *filename, uint64_t *entry_point) {
  //  if (!filename || !entry_point || !cpu) return -1;

    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "fopen(%s) failed: %s\n", filename, strerror(errno));
        return -1;
    }

    // 读取 ELF64 header
    Elf64_Ehdr eh;
    if (fread(&eh, 1, sizeof(eh), f) != sizeof(eh)) {
        fprintf(stderr, "Failed to read ELF header: %s\n", strerror(errno));
        fclose(f);
        return -1;
    }

    // 验证 ELF 魔数和类型 & 机器
    if (memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0) {
        fprintf(stderr, "Not an ELF file: %s\n", filename);
        fclose(f);
        return -1;
    }
    if (eh.e_ident[EI_CLASS] != ELFCLASS64) {
        fprintf(stderr, "ELF is not 64-bit\n");
        fclose(f);
        return -1;
    }
    if (eh.e_machine != EM_RISCV) {
        fprintf(stderr, "ELF is not RISC-V (e_machine=0x%x)\n", eh.e_machine);
        fclose(f);
        return -1;
    }

    printf("=== openSBI ELF文件信息 ===\n");
    printf("程序头数量: %" PRIu16 "\n", eh.e_phnum);
    printf("程序头偏移: 0x%lx\n", (unsigned long)eh.e_phoff);
    printf("程序头大小: %u\n", eh.e_phentsize);
    printf("入口点: 0x%lx\n", (unsigned long)eh.e_entry);

    // 选择加载基址（对于 fw_dynamic.elf 推荐 0x80000000）
   // const uint64_t load_base = 0x80000000ULL;

    uint64_t load_base = 0;// 根据 ELF 类型动态处理
    // 计算并设置 entry point：
    // - 对于 ET_DYN（PIE），entry 通常为相对地址或0，需加上 load_base
    // - 对于 ET_EXEC，entry 是绝对虚拟地址
    if (eh.e_type == ET_DYN) {
        load_base = 0x80000000ULL;
        if (eh.e_entry == 0)
            *entry_point = load_base;
        else
            *entry_point = eh.e_entry + load_base;
    } else {
        // ET_EXEC 或其他
        *entry_point = eh.e_entry;
    }

    int segments_loaded = 0;

    // 逐个 program header
    printf("en.e_phnum:%d",eh.e_phnum);
    for (uint16_t i = 0; i < eh.e_phnum; ++i) {
        // 定位到第 i 个 program header
        off_t ph_off = (off_t)eh.e_phoff + (off_t)i * (off_t)eh.e_phentsize;
        if (fseeko(f, ph_off, SEEK_SET) != 0) {
            fprintf(stderr, "fseeko phdr failed: %s\n", strerror(errno));
            continue;
        }

        Elf64_Phdr ph;
        if (fread(&ph, 1, sizeof(ph), f) != sizeof(ph)) {
            fprintf(stderr, "Failed to read phdr %u\n", i);
            continue;
        }

        // 只处理 PT_LOAD 段
        printf("ph.p_type:%d",ph.p_type);
        if (ph.p_type != PT_LOAD) continue;


        // 计算最终加载的虚拟地址（并把它当作物理地址使用）
        
        uint64_t virt_addr = 0;
        if(eh.e_type == ET_DYN){
            virt_addr = ph.p_vaddr + load_base; //openSBI
        }else{
            virt_addr = ph.p_vaddr; //xv6
        }

        uint64_t phys_addr = virt_addr; // 在你的模拟器中，M 模式下虚拟==物理 (或你将其视为直接物理地址)

        printf("\n--- LOAD 段 %u ---\n", i);
        printf("orig p_vaddr: 0x%lx  offset: 0x%lx  filesz: 0x%lx  memsz: 0x%lx  flags: 0x%x\n",
               (unsigned long)ph.p_vaddr, (unsigned long)ph.p_offset,
               (unsigned long)ph.p_filesz, (unsigned long)ph.p_memsz, ph.p_flags);
        printf("mapped virt -> phys: 0x%lx -> 0x%lx\n", (unsigned long)virt_addr, (unsigned long)phys_addr);

        // 检查内存边界
        if (phys_addr < MEMORY_BASE || (phys_addr + ph.p_memsz) > (MEMORY_BASE + MEMORY_SIZE)) {
            fprintf(stderr, "Segment %u out of memory range: 0x%lx - 0x%lx (membase 0x%lx size 0x%lx)\n",
                    i, (unsigned long)phys_addr, (unsigned long)(phys_addr + ph.p_memsz),
                    (unsigned long)MEMORY_BASE, (unsigned long)MEMORY_SIZE);
            continue;
        }

        // 将文件数据读取到内存
        if (ph.p_filesz > 0) {
            if (fseeko(f, (off_t)ph.p_offset, SEEK_SET) != 0) {
                fprintf(stderr, "fseeko to file offset failed: %s\n", strerror(errno));
                continue;
            }

            size_t to_read = (size_t)ph.p_filesz;
            uint8_t *dst = memory + (size_t)(phys_addr - MEMORY_BASE);
            size_t read = fread(dst, 1, to_read, f);
            if (read != to_read) {
                fprintf(stderr, "Warning: read %zu of %zu bytes for segment %u\n", read, to_read, i);
            }
        }

        // 若 memsz > filesz，需要将剩余部分清零（bss）
        if (ph.p_memsz > ph.p_filesz) {
            uint8_t *dst = memory + (size_t)(phys_addr - MEMORY_BASE + ph.p_filesz);
            size_t zero_sz = (size_t)(ph.p_memsz - ph.p_filesz);
            memset(dst, 0, zero_sz);
        }

        // 建立 vaddr->paddr 映射（如果你的模拟器有页表或映射逻辑）
        uint32_t flags = 0;
        if (ph.p_flags & PF_R) flags |= (1<<0); // 你原来的 PTE_R 等常量替换或保持
        if (ph.p_flags & PF_W) flags |= (1<<1);
        if (ph.p_flags & PF_X) flags |= (1<<2);
        // 注意：这里 flags 的具体位要与你 map_vaddr_to_paddr 的实现对应

        // 调用映射函数（如果需要）；如果不需要，可以注释掉
        //map_vaddr_to_paddr(cpu, virt_addr, phys_addr, ph.p_memsz, flags, cpu->asid);

        printf("✅ 加载段 %u 到 0x%lx (memsz 0x%lx)\n", i, (unsigned long)phys_addr, (unsigned long)ph.p_memsz);
        segments_loaded++;
    }

    printf("\n=== 加载完成 ===\n");
    printf("成功加载 %d 个段\n", segments_loaded);
    printf("程序入口: 0x%lx\n", (unsigned long)*entry_point);

    fclose(f);
    return segments_loaded;
}

/* Example main: 测试用 */
#ifdef ELF_LOADER_TEST
int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <elf32-file>\n", argv[0]); return 1; }
    const char *path = argv[1];
    const size_t MEM_SIZE = 16 * 1024 * 1024; // 16MB
    uint8_t *memory = calloc(1, MEM_SIZE);
    CPU_State cpu;
    memset(&cpu, 0, sizeof(cpu));

    uint64_t mem_base = 0x80000000ULL; // 假设内存从 0x80000000 开始
    int r = load_elf32(path, memory, MEM_SIZE, mem_base, &cpu);
    if (r != 0) { fprintf(stderr, "load failed\n"); free(memory); return 1; }

    printf("loaded %s -> pc=0x%08x sp=0x%08x\n", path, cpu.pc, cpu.regs[2]);
    /* 这里你可以把 memory, cpu 传给你的模拟器循环开始执行 */
    free(memory);
    return 0;
}
#endif
