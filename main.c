#define _GNU_SOURCE

#include "osdep.h"


#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <elf.h>

#include "sizes.h"
#include "cpu.h"

// # define ELF_CLASS  ELFCLASS64


#if 1
#define lsassert(cond)                                                  \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr,                                             \
                    "\033[31m assertion failed in <%s> %s:%d \033[m\n", \
                    __FUNCTION__, __FILE__, __LINE__);                  \
            abort();                                                    \
        }                                                               \
    } while (0)

#define lsassertm(cond, ...)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "\033[31m assertion failed in <%s> %s:%d \033[m", \
                    __FUNCTION__, __FILE__, __LINE__);                        \
            fprintf(stderr, __VA_ARGS__);                                     \
            abort();                                                          \
        }                                                                     \
    } while (0)

#else
#define lsassert(cond)          ((void)0)
#define lsassertm(cond, ...)    ((void)0)
#endif


char* ram;
uint64_t ram_size = SZ_4G;
const char* kernel_filename;

CPULoongArchState env;
void usage(void) {
    printf("la_emu -m n[G] -k kernel\n");
    exit(EXIT_SUCCESS);
}

static char* alloc_ram(uint64_t ram_size) {
    void* start = mmap(NULL, ram_size + SZ_2G, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    lsassert(start != MAP_FAILED);
    void* part1 = mmap(start, SZ_256M, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    lsassert(part1 != MAP_FAILED);
    void* part2 = mmap(start + SZ_2G, ram_size - SZ_256M, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    lsassert(part2 != MAP_FAILED);
    return part1;
}

#define elfhdr Elf64_Ehdr
#define elf_shdr Elf64_Shdr
#define elf_phdr Elf64_Phdr

bool load_elf(char* ram, const char* filename, uint64_t* entry_addr) {
    int size, i, total_size;
    uint64_t mem_size, file_size;
    uint8_t e_ident[EI_NIDENT];
    uint8_t *data = NULL;
    int ret = 1;
    elfhdr ehdr;
    elf_shdr *symtab, *strtab, *shdr_table = NULL;
    elf_phdr *phdr = NULL, *ph;
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror(filename);
        goto fail;
    }

    if (read(fd, e_ident, sizeof(e_ident)) != sizeof(e_ident))
        goto fail;
    if (e_ident[0] != ELFMAG0 ||
        e_ident[1] != ELFMAG1 ||
        e_ident[2] != ELFMAG2 ||
        e_ident[3] != ELFMAG3) {
            lsassertm(0, "%s is not an elf\n", filename);
    }
    lsassert(e_ident[EI_CLASS] == ELFCLASS64);
    lseek(fd, 0, SEEK_SET);


    if (read(fd, &ehdr, sizeof(ehdr)) != sizeof(ehdr))
        goto fail;

    *entry_addr = ehdr.e_entry;

    size = ehdr.e_phnum * sizeof(phdr[0]);
    if (lseek(fd, ehdr.e_phoff, SEEK_SET) != ehdr.e_phoff) {
        goto fail;
    }
    phdr = (elf_phdr *)malloc(size);
    if (!phdr)
        goto fail;
    if (read(fd, phdr, size) != size)
        goto fail;

    total_size = 0;
    for(i = 0; i < ehdr.e_phnum; i++) {
        ph = &phdr[i];
        if (ph->p_type == PT_LOAD) {
            mem_size = ph->p_memsz; /* Size of the ROM */
            file_size = ph->p_filesz; /* Size of the allocated data */
            data = (uint8_t*)malloc(file_size);
            if (ph->p_filesz > 0) {
                if (lseek(fd, ph->p_offset, SEEK_SET) < 0) {
                    goto fail;
                }
                if (read(fd, data, file_size) != file_size) {
                    goto fail;
                }
                // ram_writen(ph->p_paddr & 0xfffffff, data, file_size);
                memcpy(ram + (ph->p_paddr & 0xfffffff), data, file_size);
                printf("%lx, %lx, \n", ph->p_paddr, file_size);
            }
        }
    }

fail:
    close(fd);
    return ret;
}

static void cpu_reset(CPULoongArchState* env) {
    int n;
    /* Set csr registers value after reset */
    env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, PLV, 0);
    env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, IE, 0);
    env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, DA, 1);
    env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, PG, 0);
    env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, DATF, 1);
    env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, DATM, 1);

    env->CSR_EUEN = FIELD_DP64(env->CSR_EUEN, CSR_EUEN, FPE, 0);
    env->CSR_EUEN = FIELD_DP64(env->CSR_EUEN, CSR_EUEN, SXE, 0);
    env->CSR_EUEN = FIELD_DP64(env->CSR_EUEN, CSR_EUEN, ASXE, 0);
    env->CSR_EUEN = FIELD_DP64(env->CSR_EUEN, CSR_EUEN, BTE, 0);

    env->CSR_MISC = 0;

    env->CSR_ECFG = FIELD_DP64(env->CSR_ECFG, CSR_ECFG, VS, 0);
    env->CSR_ECFG = FIELD_DP64(env->CSR_ECFG, CSR_ECFG, LIE, 0);

    env->CSR_ESTAT = env->CSR_ESTAT & (~MAKE_64BIT_MASK(0, 2));
    env->CSR_RVACFG = FIELD_DP64(env->CSR_RVACFG, CSR_RVACFG, RBITS, 0);
    env->CSR_TCFG = FIELD_DP64(env->CSR_TCFG, CSR_TCFG, EN, 0);
    env->CSR_LLBCTL = FIELD_DP64(env->CSR_LLBCTL, CSR_LLBCTL, KLO, 0);
    env->CSR_TLBRERA = FIELD_DP64(env->CSR_TLBRERA, CSR_TLBRERA, ISTLBR, 0);
    env->CSR_MERRCTL = FIELD_DP64(env->CSR_MERRCTL, CSR_MERRCTL, ISMERR, 0);

    env->CSR_PRCFG3 = FIELD_DP64(env->CSR_PRCFG3, CSR_PRCFG3, TLB_TYPE, 2);
    env->CSR_PRCFG3 = FIELD_DP64(env->CSR_PRCFG3, CSR_PRCFG3, MTLB_ENTRY, 63);
    env->CSR_PRCFG3 = FIELD_DP64(env->CSR_PRCFG3, CSR_PRCFG3, STLB_WAYS, 7);
    env->CSR_PRCFG3 = FIELD_DP64(env->CSR_PRCFG3, CSR_PRCFG3, STLB_SETS, 8);

    for (n = 0; n < 4; n++) {
        env->CSR_DMW[n] = FIELD_DP64(env->CSR_DMW[n], CSR_DMW, PLV0, 0);
        env->CSR_DMW[n] = FIELD_DP64(env->CSR_DMW[n], CSR_DMW, PLV1, 0);
        env->CSR_DMW[n] = FIELD_DP64(env->CSR_DMW[n], CSR_DMW, PLV2, 0);
        env->CSR_DMW[n] = FIELD_DP64(env->CSR_DMW[n], CSR_DMW, PLV3, 0);
    }

    env->pc = 0x1c000000;
    // memset(env->tlb, 0, sizeof(env->tlb));

    // restore_fp_status(env);
    // cs->exception_index = -1;
    env->CSR_ECFG = 0x7 << 16;
}

static void loongarch_la464_initfn(CPULoongArchState* env) {
    int i;

    for (i = 0; i < 21; i++) {
        env->cpucfg[i] = 0x0;
    }

    env->cpucfg[0] = 0x14c010;  /* PRID */

    uint32_t data = 0;
    data = FIELD_DP32(data, CPUCFG1, ARCH, 2);
    data = FIELD_DP32(data, CPUCFG1, PGMMU, 1);
    data = FIELD_DP32(data, CPUCFG1, IOCSR, 1);
    data = FIELD_DP32(data, CPUCFG1, PALEN, 0x2f);
    data = FIELD_DP32(data, CPUCFG1, VALEN, 0x2f);
    data = FIELD_DP32(data, CPUCFG1, UAL, 1);
    data = FIELD_DP32(data, CPUCFG1, RI, 1);
    data = FIELD_DP32(data, CPUCFG1, EP, 1);
    data = FIELD_DP32(data, CPUCFG1, RPLV, 1);
    data = FIELD_DP32(data, CPUCFG1, HP, 1);
    data = FIELD_DP32(data, CPUCFG1, IOCSR_BRD, 1);
    env->cpucfg[1] = data;

    data = 0;
    data = FIELD_DP32(data, CPUCFG2, FP, 1);
    data = FIELD_DP32(data, CPUCFG2, FP_SP, 1);
    data = FIELD_DP32(data, CPUCFG2, FP_DP, 1);
    data = FIELD_DP32(data, CPUCFG2, FP_VER, 1);
    data = FIELD_DP32(data, CPUCFG2, LSX, 1),
    data = FIELD_DP32(data, CPUCFG2, LLFTP, 1);
    data = FIELD_DP32(data, CPUCFG2, LLFTP_VER, 1);
    data = FIELD_DP32(data, CPUCFG2, LAM, 1);
    env->cpucfg[2] = data;

    env->cpucfg[4] = 100 * 1000 * 1000; /* Crystal frequency */

    data = 0;
    data = FIELD_DP32(data, CPUCFG5, CC_MUL, 1);
    data = FIELD_DP32(data, CPUCFG5, CC_DIV, 1);
    env->cpucfg[5] = data;

    data = 0;
    data = FIELD_DP32(data, CPUCFG16, L1_IUPRE, 1);
    data = FIELD_DP32(data, CPUCFG16, L1_DPRE, 1);
    data = FIELD_DP32(data, CPUCFG16, L2_IUPRE, 1);
    data = FIELD_DP32(data, CPUCFG16, L2_IUUNIFY, 1);
    data = FIELD_DP32(data, CPUCFG16, L2_IUPRIV, 1);
    data = FIELD_DP32(data, CPUCFG16, L3_IUPRE, 1);
    data = FIELD_DP32(data, CPUCFG16, L3_IUUNIFY, 1);
    data = FIELD_DP32(data, CPUCFG16, L3_IUINCL, 1);
    env->cpucfg[16] = data;

    data = 0;
    data = FIELD_DP32(data, CPUCFG17, L1IU_WAYS, 3);
    data = FIELD_DP32(data, CPUCFG17, L1IU_SETS, 8);
    data = FIELD_DP32(data, CPUCFG17, L1IU_SIZE, 6);
    env->cpucfg[17] = data;

    data = 0;
    data = FIELD_DP32(data, CPUCFG18, L1D_WAYS, 3);
    data = FIELD_DP32(data, CPUCFG18, L1D_SETS, 8);
    data = FIELD_DP32(data, CPUCFG18, L1D_SIZE, 6);
    env->cpucfg[18] = data;

    data = 0;
    data = FIELD_DP32(data, CPUCFG19, L2IU_WAYS, 15);
    data = FIELD_DP32(data, CPUCFG19, L2IU_SETS, 8);
    data = FIELD_DP32(data, CPUCFG19, L2IU_SIZE, 6);
    env->cpucfg[19] = data;

    data = 0;
    data = FIELD_DP32(data, CPUCFG20, L3IU_WAYS, 15);
    data = FIELD_DP32(data, CPUCFG20, L3IU_SETS, 14);
    data = FIELD_DP32(data, CPUCFG20, L3IU_SIZE, 6);
    env->cpucfg[20] = data;

    env->CSR_ASID = FIELD_DP64(0, CSR_ASID, ASIDBITS, 0xa);
}

static uint64_t addr_trans(uint64_t addr, int prot) {
    if (env.CSR_CRMD) {

    }
}

static uint32_t fetch(uint64_t addr) {
    hwaddr ha;
    int prot;
    int mmu_idx = FIELD_EX64(env.CSR_CRMD, CSR_CRMD, PLV) == 0 ? MMU_IDX_KERNEL : MMU_IDX_USER;
    int r = get_physical_address(&env, &ha, &prot, addr, MMU_INST_FETCH, mmu_idx);
    // printf("va:%lx,pa:%lx\n", addr, ha);
    return *(uint32_t*)(ram + ha);
}

static void exec_env() {
    uint32_t insn;
    while(1) {
        insn = fetch(env.pc);
        // printf("pa:%lx,insn:%x\n", env.pc, insn);
        interpreter(&env, insn);
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
    }
    char* end;
    int c;
    while ((c = getopt(argc, argv, "m:k:")) != -1) {
        switch (c) {
            case 'm':
                ram_size = atol(optarg) << 30;
                break;
            case 'k':
                kernel_filename = optarg;
                break;
            case '?':
                usage();
                return 1;
            default:
                abort();
        }
    }
    printf("%d, ram_size:%lx kernel_filename:%s\n", getpid(), ram_size,kernel_filename);

    ram = alloc_ram(ram_size);
    uint64_t entry_addr;
    load_elf(ram, kernel_filename, &entry_addr);
    cpu_reset(&env);
    loongarch_la464_initfn(&env);
    env.pc = entry_addr;
    exec_env();

    getchar();

    return 0;
}