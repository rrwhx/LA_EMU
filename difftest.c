#define _GNU_SOURCE

#include "qemu/osdep.h"
#include "util.h"

#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/mman.h>

#include <elf.h>

#include "sizes.h"
#include "cpu.h"
#include "internals.h"

#define DUT_TO_REF 0
#define REF_TO_DUT 1

extern char* ram;

extern int64_t singlestep;

extern int exec_env(CPULoongArchState *env);
extern void cpu_reset(CPUState* cs);
extern uint64_t helper_read_csr(CPULoongArchState *env, int csr_index);

extern const char* const csrnames[];


static inline uint8_t* guest_to_host(uint64_t guest_paddr)
{
    return (uint8_t*)(ram + guest_paddr);
}

static void difftest_init_ram(size_t size)
{
    lsassert(ram == NULL);

    ram = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    lsassert(ram != NULL);

}

void difftest_init(size_t ram_size_bytes)
{

    // TODO : Don't repeat yourself(DRY)
    LoongArchCPU* cpu = aligned_alloc(64, sizeof(LoongArchCPU));
    memset(cpu, 0, sizeof(LoongArchCPU));
    CPUState *cs = CPU(cpu);
    CPULoongArchState* env = &cpu->env;
    cs->env = env;
    cpu_reset(cs);
    loongarch_core_initfn(env);
    cpu_clear_tc(env);
    env->timer_counter = INT64_MAX;

    current_env = env;

    difftest_init_ram(ram_size_bytes);

}

void difftest_exec(uint64_t n)
{
    singlestep = n;
    exec_env(current_env);
}

static inline void difftest_cpy_helper(void* ref_buf, void* dut_buf, size_t n, bool direction)
{
    if (direction == DUT_TO_REF) {
        memcpy(ref_buf, dut_buf, n);
    } else {
        memcpy(dut_buf, ref_buf, n);
    }
}

void difftest_memcpy(uint64_t guest_paddr, void* dut_buf, size_t n, bool direction)
{
    void* ref_buf = (void*)guest_to_host(guest_paddr);
    difftest_cpy_helper(ref_buf, dut_buf, n, direction);
}


void difftest_gprcpy(void* dut_buf, bool direction)
{
    void* gpr_base_addr = (void*)(&current_env->gpr);
    size_t gpr_size = sizeof(current_env->gpr[0]) * 32;

    difftest_cpy_helper(gpr_base_addr, dut_buf, gpr_size, direction);
}

void difftest_gprcpy_idx(int gpr_idx, uint64_t* dut_buf, bool direction)
{
    assert(gpr_idx >= 0 && gpr_idx <= 31);
    if (direction == DUT_TO_REF) {
        current_env->gpr[gpr_idx] = *dut_buf;
    } else {
        *dut_buf = current_env->gpr[gpr_idx];
    }
}

void difftest_pccpy(uint64_t* dut_buf, bool direction)
{
    if (direction == DUT_TO_REF) {
        current_env->pc = *dut_buf;
    } else {
        *dut_buf = current_env->prev_pc;
    }
}

void difftest_insncpy(uint32_t* dut_buf)
{
    *dut_buf = current_env->insn;
}

void difftest_fprcpy(void* dut_buf, bool direction)
{
    void* fpr_base_addr = (void*)(&current_env->fpr);
    size_t fpr_size = sizeof(current_env->fpr[0]) * 32;

    difftest_cpy_helper(fpr_base_addr, dut_buf, fpr_size, direction);
}

void difftest_fprcpy_idx(int fpr_idx, void* dut_buf, bool direction)
{
    assert(fpr_idx >= 0 && fpr_idx <= 31);
    void* fpr_base_addr = (void*)(&current_env->fpr[fpr_idx]);
    size_t fpr_size = sizeof(current_env->fpr[0]);

    difftest_cpy_helper(fpr_base_addr, dut_buf, fpr_size, direction);
}

void difftest_cfcpy(void* dut_buf, bool direction)
{
    void* cf_base_addr = &current_env->cf;
    size_t cf_size = sizeof(current_env->cf[0]) * 8;

    difftest_cpy_helper(cf_base_addr, dut_buf, cf_size, direction);
}

void difftest_cfcpy_idx(int cf_idx, uint8_t* dut_buf, bool direction)
{
    assert(cf_idx >= 0 && cf_idx <= 7);

    if (direction == DUT_TO_REF) {
        current_env->cf[cf_idx] = *dut_buf;
    } else {
        *dut_buf = current_env->cf[cf_idx];
    }
}

void difftest_fcsr0cpy(uint32_t* dut_buf, bool direction)
{
    if (direction == DUT_TO_REF) {
        current_env->fcsr0 = *dut_buf;
    } else {
        *dut_buf = current_env->fcsr0;
    }
}

#define CSR_CPY_HELPER(CSR)             \
    case LOONGARCH_CSR_ ## CSR : csr_base_addr = &(current_env->CSR_ ## CSR); break;

void difftest_csrcpy_idx(int csr_idx, uint64_t* dut_buf, uint64_t mask, bool direction)
{
    uint64_t csr_value;

    if (direction == REF_TO_DUT) {
        csr_value = helper_read_csr(current_env, csr_idx);
        *dut_buf = (*dut_buf & ~mask) | (csr_value & mask);
        return;
    }

    uint64_t* csr_base_addr = &csr_value;

    switch (csr_idx)
    {
    CSR_CPY_HELPER(CRMD)
    CSR_CPY_HELPER(PRMD)
    CSR_CPY_HELPER(EUEN)
    CSR_CPY_HELPER(MISC)
    CSR_CPY_HELPER(ECFG)
    CSR_CPY_HELPER(ESTAT)
    CSR_CPY_HELPER(ERA)
    CSR_CPY_HELPER(BADV)
    CSR_CPY_HELPER(BADI)
    CSR_CPY_HELPER(EENTRY)
    CSR_CPY_HELPER(TLBIDX)
    CSR_CPY_HELPER(TLBEHI)
    CSR_CPY_HELPER(TLBELO0)
    CSR_CPY_HELPER(TLBELO1)
    CSR_CPY_HELPER(ASID)
    CSR_CPY_HELPER(PGDL)
    CSR_CPY_HELPER(PGDH)
    CSR_CPY_HELPER(PGD)
    CSR_CPY_HELPER(PWCL)
    CSR_CPY_HELPER(PWCH)
    CSR_CPY_HELPER(STLBPS)
    CSR_CPY_HELPER(RVACFG)
    CSR_CPY_HELPER(CPUID)
    CSR_CPY_HELPER(PRCFG1)
    CSR_CPY_HELPER(PRCFG2)
    CSR_CPY_HELPER(PRCFG3)
    case LOONGARCH_CSR_SAVE(0) ... LOONGARCH_CSR_SAVE(15): csr_base_addr = &(current_env->CSR_SAVE[csr_idx - LOONGARCH_CSR_SAVE(0)]); break;
    CSR_CPY_HELPER(TID)
    CSR_CPY_HELPER(TCFG)
    CSR_CPY_HELPER(TVAL)
    CSR_CPY_HELPER(CNTC)
    CSR_CPY_HELPER(TICLR)
    CSR_CPY_HELPER(LLBCTL)
    CSR_CPY_HELPER(IMPCTL1)
    CSR_CPY_HELPER(IMPCTL2)
    CSR_CPY_HELPER(TLBRENTRY)
    CSR_CPY_HELPER(TLBRBADV)
    CSR_CPY_HELPER(TLBRERA)
    CSR_CPY_HELPER(TLBRSAVE)
    CSR_CPY_HELPER(TLBRELO0)
    CSR_CPY_HELPER(TLBRELO1)
    CSR_CPY_HELPER(TLBREHI)
    CSR_CPY_HELPER(TLBRPRMD)
    CSR_CPY_HELPER(MERRCTL)
    CSR_CPY_HELPER(MERRINFO1)
    CSR_CPY_HELPER(MERRINFO2)
    CSR_CPY_HELPER(MERRENTRY)
    CSR_CPY_HELPER(MERRERA)
    CSR_CPY_HELPER(MERRSAVE)
    CSR_CPY_HELPER(CTAG)
    case LOONGARCH_CSR_DMW(0) ... LOONGARCH_CSR_DMW(3): csr_base_addr = &(current_env->CSR_DMW[csr_idx - LOONGARCH_CSR_DMW(0)]); break;
    CSR_CPY_HELPER(DBG)
    CSR_CPY_HELPER(DERA)
    CSR_CPY_HELPER(DSAVE)
    default:
        fprintf(stderr, "NOT IMPLEMENTED %s %x\n", __func__, csr_idx);
        break;
    }

    *csr_base_addr = (*csr_base_addr & ~mask) | (*dut_buf & mask);

}

void difftest_tlbcpy()
{
    // TODO
}