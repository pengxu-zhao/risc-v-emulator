#include <stdint.h>

/* 32-bit ELF header */
typedef struct {
    unsigned char e_ident[16]; /* ELF标识 */
    uint16_t e_type;           /* 文件类型 (可执行/共享库/目标文件) */
    uint16_t e_machine;        /* 目标机器架构 */
    uint32_t e_version;        /* ELF版本 */
    uint32_t e_entry;          /* 程序入口点虚拟地址 */
    uint32_t e_phoff;          /* 程序头表（Program Header Table）偏移 */
    uint32_t e_shoff;          /* 节头表（Section Header Table）偏移 */
    uint32_t e_flags;          /* 与处理器相关的标志 */
    uint16_t e_ehsize;         /* ELF头大小 */
    uint16_t e_phentsize;      /* 程序头表每个表项大小 */
    uint16_t e_phnum;          /* 程序头表项数量 */
    uint16_t e_shentsize;      /* 节头表每个表项大小 */
    uint16_t e_shnum;          /* 节头表项数量 */
    uint16_t e_shstrndx;       /* 节头字符串表索引 */
} Elf32_Ehdr;


/* 32-bit Program header */
typedef struct {
    uint32_t p_type;   /* 段类型 */
    uint32_t p_offset; /* 文件中偏移 */
    uint32_t p_vaddr;  /* 虚拟地址 */
    uint32_t p_paddr;  /* 物理地址(通常忽略) */
    uint32_t p_filesz; /* 文件中大小 */
    uint32_t p_memsz;  /* 内存中大小 */
    uint32_t p_flags;  /* 段标志 */
    uint32_t p_align;  /* 对齐 */
} Elf32_Phdr;

/* 32-bit Section header */
typedef struct {
    uint32_t sh_name;      /* 节名称(字符串表索引) */
    uint32_t sh_type;      /* 节类型 */
    uint32_t sh_flags;     /* 节标志 */
    uint32_t sh_addr;      /* 虚拟地址 */
    uint32_t sh_offset;    /* 文件偏移 */
    uint32_t sh_size;      /* 节大小 */
    uint32_t sh_link;      /* 链接信息 */
    uint32_t sh_info;      /* 额外信息 */
    uint32_t sh_addralign; /* 对齐 */
    uint32_t sh_entsize;   /* 表项大小(如符号表) */
} Elf32_Shdr;

/* 32-bit Symbol table entry */
typedef struct {
    uint32_t st_name;  /* 符号名(字符串表索引) */
    uint32_t st_value; /* 符号值 (地址/偏移) */
    uint32_t st_size;  /* 符号大小 */
    unsigned char st_info; /* 符号类型+绑定 */
    unsigned char st_other;
    uint16_t st_shndx; /* 所属节索引 */
} Elf32_Sym;

/* e_ident[] 数组大小 */
#define EI_NIDENT   16

/* e_ident[] 的下标 */
#define EI_MAG0     0       /* 0x7F */
#define EI_MAG1     1       /* 'E'  */
#define EI_MAG2     2       /* 'L'  */
#define EI_MAG3     3       /* 'F'  */
#define EI_CLASS    4       /* 文件类别：32/64 位 */
#define EI_DATA     5       /* 数据编码：小端/大端 */
#define EI_VERSION  6       /* ELF 版本 */
#define EI_OSABI    7       /* OS ABI */
#define EI_ABIVERSION 8     /* ABI 版本 */
#define EI_PAD      9       /* 填充区开始 */
/* EI_CLASS 的取值 */
#define ELFCLASSNONE  0
#define ELFCLASS32    1
#define ELFCLASS64    2

/* EI_DATA 的取值 */
#define ELFDATANONE   0
#define ELFDATA2LSB   1   /* 小端 */
#define ELFDATA2MSB   2   /* 大端 */
/* 魔数（ELFMAG） */
#define ELFMAG0  0x7f
#define ELFMAG1  'E'
#define ELFMAG2  'L'
#define ELFMAG3  'F'
#define ELFMAG   "\177ELF"

#define SELFMAG  4   /* 魔数长度 */
#define ET_NONE   0  /* 无类型 */
#define ET_REL    1  /* 可重定位文件 (.o) */
#define ET_EXEC   2  /* 可执行文件 */
#define ET_DYN    3  /* 动态库 */
#define ET_CORE   4  /* Core dump */

#define EM_NONE     0   /* 无机器 */
#define EM_RISCV   243  /* RISC-V */

#define PT_NULL    0
#define PT_LOAD    1   /* 可加载段 */
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_SHLIB   5
#define PT_PHDR    6
#define PT_TLS     7
