#include "qemu/osdep.h"
#include "util.h"

#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/mman.h>
#include <errno.h>

#include <elf.h>

#include "sizes.h"
#include "cpu.h"
#include "internals.h"

#if !defined(CONFIG_USER_ONLY)

#define __4KB 0x1000

#define LOWMEM_BEGIN 0ul
#define LOWMEM_SIZE 0x10000000ul
#define HIGHMEM_BEGIN 0x90000000ul
#define HIGHMEM_SIZE 0xf0000000ul

static uint8_t zero4k[__4KB] __attribute__ ((aligned (__4KB)));
extern char* ram;

static void loongarch_cpu_dump_state(CPULoongArchState *env, FILE *f);
static void loongarch_cpu_restore_state(CPULoongArchState *env, FILE *f);
extern uint64_t helper_read_csr(CPULoongArchState *env, int csr_index);
extern uint64_t helper_csrrd_pgd(CPULoongArchState*);

static inline FILE* fopen_nofail(const char *__restrict __filename, const char *__restrict __modes) {
    FILE* f = fopen(__filename, __modes);
    if (!f) {
        perror(__filename);
        abort();
    }
    return f;
}

static void* fread_all_nofail(const char *__restrict __filename, int64_t* size) {
    FILE *f = fopen_nofail(__filename, "rb");
    fseek(f, 0, SEEK_END);
    int64_t fsize = ftell(f);
    fseek(f, 0, SEEK_SET);  /* same as rewind(f); */

    void *string = malloc(fsize + 1);
    lsassert(fread(string, fsize, 1, f) == 1);
    fclose(f);
    *size = fsize;
    return string;
}

static uint64_t xor_sum(const void* buf, uint64_t size) {
    lsassert(size % __4KB == 0);
    uint64_t r = 0;
    for(uint64_t addr = 0; addr < size; addr += 8) {
        r ^= *(uint64_t*)((char*)buf + addr);
    }
    return r;
}

static uint64_t simple_compress(const char* filename, const void* buf, uint64_t size) {
    FILE* f = fopen_nofail(filename, "wb");
    lsassert(size % __4KB == 0);
    uint64_t present_cnt = 0;
    int64_t present = 0;
    for(uint64_t addr = 0; addr < size; addr += __4KB) {
        if (memcmp((char*)buf + addr, zero4k, __4KB)) {
            present = 1;
            ++ present_cnt;
            lsassert(fwrite(&present, sizeof(present), 1, f) == 1);
            lsassert(fwrite((char*)buf + addr, __4KB, 1, f) == 1);
        } else {
            present = 0;
            lsassert(fwrite(&present, sizeof(present), 1, f) == 1);
        }
    }
    uint64_t xs = xor_sum(buf, size);
    lsassert(fwrite(&xs, sizeof(xs), 1, f) == 1);
    fclose(f);
    return xs;
}

static uint64_t simple_decompress(const char* filename, uint64_t base_addr, uint64_t size) {
    lsassert(size % __4KB == 0);
    int64_t input_size;
    void* input_buf =  fread_all_nofail(filename, &input_size);
    uint64_t index = 0;
    uint64_t xs = 0;
    for(uint64_t addr = 0; addr < size; addr += __4KB) {
        int64_t present = *(int64_t *)((uint8_t*)input_buf + index);
        index += 8;
        if (present) {
            memcpy(ram + base_addr + addr, (uint8_t*)input_buf + index, __4KB);
            xs ^= xor_sum((uint8_t*)input_buf + index, __4KB);
            index += __4KB;
        }
        // zero page need not copy
    }
    printf("my:%lx input:%lx\n", xs, *(int64_t *)((uint8_t*)input_buf + index));
    printf("index:%lx, input_size:%lx\n", index, input_size);
    lsassert(xs == *(int64_t *)((uint8_t*)input_buf + index));
    lsassert(index + 8 == input_size);
    free(input_buf);
    return xs;
}

void save_checkpoint(CPULoongArchState *env, char* name)
{
    FILE *f;
    char filename[1024];

    fprintf(stderr, "checkpoint at icount:%ld\n", env->icount);

    sprintf(filename, "%s_icount_%ld", name, env->icount);

    if (mkdir(filename, 0755) < 0 && errno != EEXIST) {
        fprintf(stderr, "ERROR: cannot create dir:%s\n", filename);
        exit(1);
    }

    sprintf(filename, "%s_icount_%ld/lowmem.c.bin", name, env->icount);
    simple_compress(filename, (void*)(ram + LOWMEM_BEGIN), LOWMEM_SIZE);
    sprintf(filename, "%s_icount_%ld/highmem.c.bin", name, env->icount);
    simple_compress(filename, (void*)(ram + HIGHMEM_BEGIN), HIGHMEM_SIZE);

    sprintf(filename, "%s_icount_%ld/regs.txt", name, env->icount);
    f = fopen_nofail(filename, "w");
    fprintf(f, "icount 0x%016lx\n", env->icount);
    loongarch_cpu_dump_state(env, f);
    fclose(f);

}

void restore_checkpoint(CPULoongArchState *env, char* image_dir)
{
    char filename[1024];
    char buffer[1024];

    fprintf(stderr, "load ckpt, from directory \"%s\"\n", image_dir);

    sprintf(filename, "%s/lowmem.c.bin", image_dir);
    simple_decompress(filename, LOWMEM_BEGIN, LOWMEM_SIZE);
    sprintf(filename, "%s/highmem.c.bin", image_dir);
    simple_decompress(filename, HIGHMEM_BEGIN, HIGHMEM_SIZE);

    sprintf(filename, "%s/regs.txt", image_dir);
    FILE *reg_file = fopen_nofail(filename, "r");
    lsassert(fgets(buffer, sizeof(buffer), reg_file));
    lsassert(sscanf(buffer, "icount 0x%lx", &env->icount) == 1);
    printf("restore from icount=%ld\n", env->icount);
    loongarch_cpu_restore_state(env, reg_file);

    if (env->CSR_CNTC < 0) {
        env->CSR_CNTC = 0;
    }
    env->timer_counter = env->CSR_TVAL;
    env->CSR_TICLR = 0;
}

static uint64_t* get_csr_ptr(CPULoongArchState *env, uint64_t idx) {
    switch (idx)
    {
    case LOONGARCH_CSR_CRMD:    return &(env->CSR_CRMD);
    case LOONGARCH_CSR_PRMD:    return &(env->CSR_PRMD);
    case LOONGARCH_CSR_EUEN:    return &(env->CSR_EUEN);
    case LOONGARCH_CSR_MISC:    return &(env->CSR_MISC);
    case LOONGARCH_CSR_ECFG:    return &(env->CSR_ECFG);
    case LOONGARCH_CSR_ESTAT:   return &(env->CSR_ESTAT);
    case LOONGARCH_CSR_ERA:     return &(env->CSR_ERA);
    case LOONGARCH_CSR_BADV:    return &(env->CSR_BADV);
    case LOONGARCH_CSR_BADI:    return &(env->CSR_BADI);
    case LOONGARCH_CSR_EENTRY:  return &(env->CSR_EENTRY);
    case LOONGARCH_CSR_TLBIDX:  return &(env->CSR_TLBIDX);
    case LOONGARCH_CSR_TLBEHI:  return &(env->CSR_TLBEHI);
    case LOONGARCH_CSR_TLBELO0: return &(env->CSR_TLBELO0);
    case LOONGARCH_CSR_TLBELO1: return &(env->CSR_TLBELO1);
    case LOONGARCH_CSR_ASID:    return &(env->CSR_ASID);
    case LOONGARCH_CSR_PGDL:    return &(env->CSR_PGDL);
    case LOONGARCH_CSR_PGDH:    return &(env->CSR_PGDH);
    case LOONGARCH_CSR_PGD:     return &(env->CSR_PGD);
    case LOONGARCH_CSR_PWCL:    return &(env->CSR_PWCL);
    case LOONGARCH_CSR_PWCH:    return &(env->CSR_PWCH);
    case LOONGARCH_CSR_STLBPS:  return &(env->CSR_STLBPS);
    case LOONGARCH_CSR_RVACFG:  return &(env->CSR_RVACFG);
    case LOONGARCH_CSR_CPUID:   return &(env->CSR_CPUID);
    case LOONGARCH_CSR_PRCFG1:  return &(env->CSR_PRCFG1);
    case LOONGARCH_CSR_PRCFG2:  return &(env->CSR_PRCFG2);
    case LOONGARCH_CSR_PRCFG3:  return &(env->CSR_PRCFG3);
    case LOONGARCH_CSR_SAVE(0) ... LOONGARCH_CSR_SAVE(15): return &(env->CSR_SAVE[idx - LOONGARCH_CSR_SAVE(0)]);
    case LOONGARCH_CSR_TID:     return &(env->CSR_TID);
    case LOONGARCH_CSR_TCFG:    return &(env->CSR_TCFG);
    case LOONGARCH_CSR_TVAL:    return &(env->CSR_TVAL);
    case LOONGARCH_CSR_CNTC:    return &(env->CSR_CNTC);
    case LOONGARCH_CSR_TICLR:   return &(env->CSR_TICLR);
    case LOONGARCH_CSR_LLBCTL:  return &(env->CSR_LLBCTL);
    case LOONGARCH_CSR_IMPCTL1: return &(env->CSR_IMPCTL1);
    case LOONGARCH_CSR_IMPCTL2: return &(env->CSR_IMPCTL2);
    case LOONGARCH_CSR_TLBRENTRY: return &(env->CSR_TLBRENTRY);
    case LOONGARCH_CSR_TLBRBADV: return &(env->CSR_TLBRBADV);
    case LOONGARCH_CSR_TLBRERA: return &(env->CSR_TLBRERA);
    case LOONGARCH_CSR_TLBRSAVE: return &(env->CSR_TLBRSAVE);
    case LOONGARCH_CSR_TLBRELO0: return &(env->CSR_TLBRELO0);
    case LOONGARCH_CSR_TLBRELO1: return &(env->CSR_TLBRELO0);
    case LOONGARCH_CSR_TLBREHI: return &(env->CSR_TLBREHI);
    case LOONGARCH_CSR_TLBRPRMD: return &(env->CSR_TLBRPRMD);
    case LOONGARCH_CSR_MERRCTL: return &(env->CSR_MERRCTL);
    case LOONGARCH_CSR_MERRINFO1: return &(env->CSR_MERRINFO1);
    case LOONGARCH_CSR_MERRINFO2: return &(env->CSR_MERRINFO2);
    case LOONGARCH_CSR_MERRENTRY: return &(env->CSR_MERRENTRY);
    case LOONGARCH_CSR_MERRERA: return &(env->CSR_MERRERA);
    case LOONGARCH_CSR_MERRSAVE: return &(env->CSR_MERRSAVE);
    case LOONGARCH_CSR_CTAG: return &(env->CSR_CTAG);
    case LOONGARCH_CSR_DMW(0) ... LOONGARCH_CSR_DMW(3): return &(env->CSR_DMW[idx - LOONGARCH_CSR_DMW(0)]);
    case LOONGARCH_CSR_DBG: return &(env->CSR_DBG);
    case LOONGARCH_CSR_DERA: return &(env->CSR_DERA);
    case LOONGARCH_CSR_DSAVE: return &(env->CSR_DSAVE);
    default: return NULL;
    }

}

static void loongarch_cpu_dump_state(CPULoongArchState *env, FILE *f)
{
    int i, j;

    fprintf(f, "pc 0x%x 0x%016lx\n", 0, env->pc);

    for(i = 0; i < 32; i++) {
        fprintf(f, "gpr 0x%x 0x%016lx\n", i, env->gpr[i]);
    }

    for (i = 0; i < 8; i++) {
        fprintf(f, "cf 0x%x 0x%x\n", i, env->cf[i]);
    }

    fprintf(f, "fcsr0 0x%x 0x%08x\n", 0, env->fcsr0);

    for(i = 0; i < 32; i++) {
        fprintf(f, "fpr256 0x%x", i);
        for(j = 0; j < (LASX_LEN / 64); j++) {
            fprintf(f, " 0x%016lx", env->fpr[i].vreg.D[j]);
        }
        fprintf(f, "\n");
    }

    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_CRMD, env->CSR_CRMD);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_PRMD, env->CSR_PRMD);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_EUEN, env->CSR_EUEN);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_MISC, env->CSR_MISC);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_ECFG, env->CSR_ECFG);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_ESTAT, env->CSR_ESTAT);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_ERA, env->CSR_ERA);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_BADV, env->CSR_BADV);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_BADI, env->CSR_BADI);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_EENTRY, env->CSR_EENTRY);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_TLBIDX, env->CSR_TLBIDX);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_TLBEHI, env->CSR_TLBEHI);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_TLBELO0, env->CSR_TLBELO0);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_TLBELO1, env->CSR_TLBELO1);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_ASID, env->CSR_ASID);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_PGDL, env->CSR_PGDL);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_PGDH, env->CSR_PGDH);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_PGD, helper_csrrd_pgd(env));
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_PWCL, env->CSR_PWCL);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_PWCH, env->CSR_PWCH);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_STLBPS, env->CSR_STLBPS);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_RVACFG, env->CSR_RVACFG);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_CPUID, env->CSR_CPUID);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_PRCFG1, env->CSR_PRCFG1);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_PRCFG2, env->CSR_PRCFG2);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_PRCFG3, env->CSR_PRCFG3);
    for (i = 0; i < 9; i++) {
        fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_SAVE(0) + i, env->CSR_SAVE[i]);
    }
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_TID, env->CSR_TID);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_TCFG, env->CSR_TCFG);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_TVAL, env->timer_counter);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_CNTC, env->CSR_CNTC);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_TICLR, 0ul);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_LLBCTL, env->CSR_LLBCTL);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_IMPCTL1, env->CSR_IMPCTL1);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_IMPCTL2, env->CSR_IMPCTL2);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_TLBRENTRY, env->CSR_TLBRENTRY);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_TLBRBADV, env->CSR_TLBRBADV);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_TLBRERA, env->CSR_TLBRERA);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_TLBRSAVE, env->CSR_TLBRSAVE);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_TLBRELO0, env->CSR_TLBRELO0);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_TLBRELO1, env->CSR_TLBRELO1);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_TLBREHI, env->CSR_TLBREHI);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_TLBRPRMD, env->CSR_TLBRPRMD);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_MERRCTL, env->CSR_MERRCTL);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_MERRINFO1, env->CSR_MERRINFO1);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_MERRINFO2, env->CSR_MERRINFO2);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_MERRENTRY, env->CSR_MERRENTRY);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_MERRERA, env->CSR_MERRERA);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_MERRSAVE, env->CSR_MERRSAVE);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_CTAG, env->CSR_CTAG);
    for (i = 0; i < 4; i++) {
        fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_DMW(0) + i, env->CSR_DMW[i]);
    }
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_DBG, env->CSR_DBG);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_DERA, env->CSR_DERA);
    fprintf(f, "csr 0x%x 0x%016lx\n", LOONGARCH_CSR_DSAVE, env->CSR_DSAVE);
}

static void loongarch_cpu_restore_state(CPULoongArchState *env, FILE* f)
{
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), f)) {
        char type_str[16];
        unsigned int index;
        uint64_t value;

        if (sscanf(buffer, "%15s %x", type_str, &index) != 2) {
            lsassertm(false, "Error parsing line: %s\n", buffer);
        }

        if (strcmp(type_str, "pc") == 0) {
            if (sscanf(buffer, "%*s %*x %lx", &value) == 1) {
                env->pc = value;
            }
        } else if (strcmp(type_str, "gpr") == 0) {
            if (sscanf(buffer, "%*s %*x %lx", &value) == 1) {
                env->gpr[index] = value;
            }
        } else if (strcmp(type_str, "cf") == 0) {
            if (sscanf(buffer, "%*s %*x %lx", &value) == 1) {
                env->cf[index] = (uint8_t)value;
            }
        } else if (strcmp(type_str, "fcsr0") == 0) {
            if (sscanf(buffer, "%*s %*x %lx", &value) == 1) {
                env->fcsr0 = (uint32_t)value;
            }
        } else if (strcmp(type_str, "fpr256") == 0) {
            uint64_t fpr[4];
            sscanf(buffer, "%*s %*x %lx %lx %lx %lx", &fpr[0], &fpr[1], &fpr[2], &fpr[3]);
            for (int i = 0; i < 4; i++) {
                env->fpr[index].vreg.D[i] = fpr[i];
            }
        } else if (strcmp(type_str, "csr") == 0) {
            if (sscanf(buffer, "%*s %*x %lx", &value) == 1) {
                uint64_t* ptr = get_csr_ptr(env, index);
                if (ptr != NULL) {
                    *ptr = value;
                } else {
                    fprintf(stderr, "LA_EMU does not have csr idx=0x%x\n", index);
                }
            }
        } else {
            lsassertm(false, "Unknown register type, check txt format\n");
        }
    }
}

#else
void save_checkpoint(CPULoongArchState *env, char* name) {
    fprintf(stderr, "error :can not save checkpoint in user mode\n");
}

void restore_checkpoint(CPULoongArchState *env, char* name) {
    lsassertm(false, "can not restore checkpoint in user mode\n");
}
#endif

// export save_checkpoint to dynamic library
void la_emu_save_checkpoint(void *env, char* name) {
    save_checkpoint((CPULoongArchState*)env, name);
}