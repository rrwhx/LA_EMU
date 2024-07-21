#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "cpu-csr.h"

const char * const regnames[32] = {
    "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
    "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
    "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31",
};

const char * const fregnames[32] = {
    "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7",
    "f8", "f9", "f10", "f11", "f12", "f13", "f14", "f15",
    "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",
    "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31",
};

const char *const loongarch_r_alias[32] =
{
    "zer", "ra", "tp", "sp", "a0", "a1", "a2", "a3",
    "a4",   "a5", "a6", "a7", "t0", "t1", "t2", "t3",
    "t4",   "t5", "t6", "t7", "t8", "r21","fp", "s0",
    "s1",   "s2", "s3", "s4", "s5", "s6", "s7", "s8",
};

const char *const loongarch_f_alias[32] =
{
    "fa0", "fa1", "fa2",  "fa3",  "fa4",  "fa5",  "fa6",  "fa7",
    "ft0", "ft1", "ft2",  "ft3",  "ft4",  "ft5",  "ft6",  "ft7",
    "ft8", "ft9", "ft10", "ft11", "ft12", "ft13", "ft14", "ft15",
    "fs0", "fs1", "fs2",  "fs3",  "fs4",  "fs5",  "fs6",  "fs7",
};


#define CSRNAME_DECL(name)                  \
        [LOONGARCH_CSR_ ## name] = #name


const char* const csrnames[LOONGARCH_CSR_MAXADDR + 1] = {
    CSRNAME_DECL(CRMD),
    CSRNAME_DECL(PRMD),
    CSRNAME_DECL(EUEN),
    CSRNAME_DECL(MISC),
    CSRNAME_DECL(ECFG),
    CSRNAME_DECL(ESTAT),
    CSRNAME_DECL(ERA),
    CSRNAME_DECL(BADV),
    CSRNAME_DECL(BADI),
    CSRNAME_DECL(EENTRY),
    CSRNAME_DECL(TLBIDX),
    CSRNAME_DECL(TLBEHI),
    CSRNAME_DECL(TLBELO0),
    CSRNAME_DECL(TLBELO1),
    CSRNAME_DECL(ASID),
    CSRNAME_DECL(PGDL),
    CSRNAME_DECL(PGDH),
    CSRNAME_DECL(PGD),
    CSRNAME_DECL(PWCL),
    CSRNAME_DECL(PWCH),
    CSRNAME_DECL(STLBPS),
    CSRNAME_DECL(RVACFG),
    CSRNAME_DECL(CPUID),
    CSRNAME_DECL(PRCFG1),
    CSRNAME_DECL(PRCFG2),
    CSRNAME_DECL(PRCFG3),
    CSRNAME_DECL(SAVE(0)),
    CSRNAME_DECL(SAVE(1)),
    CSRNAME_DECL(SAVE(2)),
    CSRNAME_DECL(SAVE(3)),
    CSRNAME_DECL(SAVE(4)),
    CSRNAME_DECL(SAVE(5)),
    CSRNAME_DECL(SAVE(6)),
    CSRNAME_DECL(SAVE(7)),
    CSRNAME_DECL(SAVE(8)),
    CSRNAME_DECL(SAVE(9)),
    CSRNAME_DECL(SAVE(10)),
    CSRNAME_DECL(SAVE(11)),
    CSRNAME_DECL(SAVE(12)),
    CSRNAME_DECL(SAVE(13)),
    CSRNAME_DECL(SAVE(14)),
    CSRNAME_DECL(SAVE(15)),
    CSRNAME_DECL(TID),
    CSRNAME_DECL(TCFG),
    CSRNAME_DECL(TVAL),
    CSRNAME_DECL(CNTC),
    CSRNAME_DECL(TICLR),
    CSRNAME_DECL(LLBCTL),
    CSRNAME_DECL(IMPCTL1),
    CSRNAME_DECL(IMPCTL2),
    CSRNAME_DECL(TLBRENTRY),
    CSRNAME_DECL(TLBRBADV),
    CSRNAME_DECL(TLBRERA),
    CSRNAME_DECL(TLBRSAVE),
    CSRNAME_DECL(TLBRELO0),
    CSRNAME_DECL(TLBRELO1),
    CSRNAME_DECL(TLBREHI),
    CSRNAME_DECL(TLBRPRMD),
    CSRNAME_DECL(MERRCTL),
    CSRNAME_DECL(MERRINFO1),
    CSRNAME_DECL(MERRINFO2),
    CSRNAME_DECL(MERRENTRY),
    CSRNAME_DECL(MERRERA),
    CSRNAME_DECL(MERRSAVE),
    CSRNAME_DECL(CTAG),
    CSRNAME_DECL(DMW(0)),
    CSRNAME_DECL(DMW(1)),
    CSRNAME_DECL(DMW(2)),
    CSRNAME_DECL(DMW(3)),
    CSRNAME_DECL(DBG),
    CSRNAME_DECL(DERA),
    CSRNAME_DECL(DSAVE),

};

#if defined(CONFIG_PERF)
void perf_report_plv(CPULoongArchState *env, FILE* f, int plv, char* plv_name) {
    fprintf(f, "%s, COUNTER_INST:                  %20lu\n", plv_name, env->perf_counter[plv][COUNTER_INST]);
    fprintf(f, "%s, COUNTER_INST_BRANCH:           %20lu\n", plv_name, env->perf_counter[plv][COUNTER_INST_BRANCH]);
    fprintf(f, "%s, COUNTER_INST_LOAD:             %20lu\n", plv_name, env->perf_counter[plv][COUNTER_INST_LOAD]);
    fprintf(f, "%s, COUNTER_INST_STORE:            %20lu\n", plv_name, env->perf_counter[plv][COUNTER_INST_STORE]);
    fprintf(f, "%s, COUNTER_INST_FP:               %20lu\n", plv_name, env->perf_counter[plv][COUNTER_INST_FP]);
    fprintf(f, "%s, COUNTER_INST_LSX:              %20lu\n", plv_name, env->perf_counter[plv][COUNTER_INST_LSX]);
    fprintf(f, "%s, COUNTER_INST_LASX:             %20lu\n", plv_name, env->perf_counter[plv][COUNTER_INST_LASX]);
    fprintf(f, "%s, COUNTER_INST_CROSS_PAGE_LOAD:  %20lu\n", plv_name, env->perf_counter[plv][COUNTER_INST_CROSS_PAGE_LOAD]);
    fprintf(f, "%s, COUNTER_INST_CROSS_PAGE_STORE: %20lu\n", plv_name, env->perf_counter[plv][COUNTER_INST_CROSS_PAGE_STORE]);

}
void perf_report(CPULoongArchState *env, FILE* f) {
#if defined(CONFIG_USER_ONLY)
    perf_report_plv(env, f, 0, "user");
#else
    perf_report_plv(env, f, 0, "kern");
    perf_report_plv(env, f, 3, "user");
#endif
}
#endif

void dump_exec_info(CPULoongArchState *env, FILE* f) {
    fprintf(f, "icount:%ld ic_hit_count:%ld syscall_count:%ld ecount:%ld tlbr:%ld irq:%ld\n", env->icount, env->ic_hit_count, env->syscall_count, env->ecount, env->tlbr_count, env->irq_count);
#if !defined(CONFIG_USER_ONLY)
    fprintf(f, "PIL:  %10lu ", env->ecounter[EXCCODE_PIL]);
    fprintf(f, "PIS:  %10lu ", env->ecounter[EXCCODE_PIS]);
    fprintf(f, "PIF:  %10lu\n", env->ecounter[EXCCODE_PIF]);
    fprintf(f, "PME:  %10lu ", env->ecounter[EXCCODE_PME]);
    fprintf(f, "PNR:  %10lu ", env->ecounter[EXCCODE_PNR]);
    fprintf(f, "PNX:  %10lu ", env->ecounter[EXCCODE_PNX]);
    fprintf(f, "PPI:  %10lu\n", env->ecounter[EXCCODE_PPI]);
    fprintf(f, "ADEF: %10lu ", env->ecounter[EXCCODE_ADEF]);
    fprintf(f, "ADEM: %10lu ", env->ecounter[EXCCODE_ADEM]);
    fprintf(f, "ALE:  %10lu ", env->ecounter[EXCCODE_ALE]);
    fprintf(f, "BCE:  %10lu\n", env->ecounter[EXCCODE_BCE]);
    fprintf(f, "SYS:  %10lu ", env->ecounter[EXCCODE_SYS]);
    fprintf(f, "BRK:  %10lu ", env->ecounter[EXCCODE_BRK]);
    fprintf(f, "INE:  %10lu ", env->ecounter[EXCCODE_INE]);
    fprintf(f, "IPE:  %10lu\n", env->ecounter[EXCCODE_IPE]);
    fprintf(f, "FPD:  %10lu ", env->ecounter[EXCCODE_FPD]);
    fprintf(f, "SXD:  %10lu ", env->ecounter[EXCCODE_SXD]);
    fprintf(f, "ASXD: %10lu\n", env->ecounter[EXCCODE_ASXD]);
    fprintf(f, "FPE:  %10lu ", env->ecounter[EXCCODE_FPE]);
    fprintf(f, "VFPE: %10lu ", env->ecounter[EXCCODE_VFPE]);
    fprintf(f, "WPEF: %10lu ", env->ecounter[EXCCODE_WPEF]);
    fprintf(f, "WPEM: %10lu\n", env->ecounter[EXCCODE_WPEM]);
    fprintf(f, "BTD:  %10lu ", env->ecounter[EXCCODE_BTD]);
    fprintf(f, "BTE:  %10lu ", env->ecounter[EXCCODE_BTE]);
    fprintf(f, "DBP:  %10lu\n", env->ecounter[EXCCODE_DBP]);
#endif
}


void loongarch_la464_initfn(CPULoongArchState* env) {
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
    data = FIELD_DP32(data, CPUCFG2, LASX, 1),
    data = FIELD_DP32(data, CPUCFG2, LLFTP, 1);
    data = FIELD_DP32(data, CPUCFG2, LLFTP_VER, 1);
    data = FIELD_DP32(data, CPUCFG2, LSPW, 1);
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

    env->CSR_PRCFG3 = FIELD_DP64(env->CSR_PRCFG3, CSR_PRCFG3, TLB_TYPE, 2);
    env->CSR_PRCFG3 = FIELD_DP64(env->CSR_PRCFG3, CSR_PRCFG3, MTLB_ENTRY, 63);
    env->CSR_PRCFG3 = FIELD_DP64(env->CSR_PRCFG3, CSR_PRCFG3, STLB_WAYS, 7);
    env->CSR_PRCFG3 = FIELD_DP64(env->CSR_PRCFG3, CSR_PRCFG3, STLB_SETS, 8);
}

void loongarch_centaur320_initfn(CPULoongArchState* env) {

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
    data = FIELD_DP32(data, CPUCFG1, MSG_INT, 1);
    env->cpucfg[1] = data;

    data = 0;
    data = FIELD_DP32(data, CPUCFG2, FP, 1);
    data = FIELD_DP32(data, CPUCFG2, FP_SP, 1);
    data = FIELD_DP32(data, CPUCFG2, FP_DP, 1);
    data = FIELD_DP32(data, CPUCFG2, FP_VER, 1);
    data = FIELD_DP32(data, CPUCFG2, LSX, 0);
    data = FIELD_DP32(data, CPUCFG2, LASX, 0);
    data = FIELD_DP32(data, CPUCFG2, LVZ_VER, 1);
    data = FIELD_DP32(data, CPUCFG2, LLFTP, 1);
    data = FIELD_DP32(data, CPUCFG2, LLFTP_VER, 1);
    data = FIELD_DP32(data, CPUCFG2, LSPW, 1);
    data = FIELD_DP32(data, CPUCFG2, LAM, 1);
    data = FIELD_DP32(data, CPUCFG2, HPTW, 1);
    env->cpucfg[2] = data;

    data = 0;
    data = FIELD_DP32(data, CPUCFG3, SFB, 1);
    data = FIELD_DP32(data, CPUCFG3, UCACC, 1);
    data = FIELD_DP32(data, CPUCFG3, LLEXC, 1);
    data = FIELD_DP32(data, CPUCFG3, SCDLY, 1);
    data = FIELD_DP32(data, CPUCFG3, LLDBAR, 1);
    data = FIELD_DP32(data, CPUCFG3, ITLBHMC, 1);
    data = FIELD_DP32(data, CPUCFG3, ICHMC, 1);
    data = FIELD_DP32(data, CPUCFG3, SPW_HP_HF, 1);
    env->cpucfg[3] = data;

    env->cpucfg[4] = 2400000000;

    data = 0;
    data = FIELD_DP32(data, CPUCFG5, CC_MUL, 1);
    data = FIELD_DP32(data, CPUCFG5, CC_DIV, 1);
    env->cpucfg[5] = data;

    data = 0;
    data = FIELD_DP32(data, CPUCFG16, L1_IUPRE, 1);
    data = FIELD_DP32(data, CPUCFG16, L1_DPRE, 1);
    data = FIELD_DP32(data, CPUCFG16, L2_IUUNIFY, 1);
    data = FIELD_DP32(data, CPUCFG16, L2_IUPRIV, 1);
    data = FIELD_DP32(data, CPUCFG16, L2_IUINCL, 1);
    // data = FIELD_DP32(data, CPUCFG16, L3_IUPRE, 1);
    data = FIELD_DP32(data, CPUCFG16, L3_IUUNIFY, 1);
    // data = FIELD_DP32(data, CPUCFG16, L3_IUINCL, 1);
    env->cpucfg[16] = data;

    data = 0;
    data = FIELD_DP32(data, CPUCFG17, L1IU_WAYS, 7);
    data = FIELD_DP32(data, CPUCFG17, L1IU_SETS, 8);
    data = FIELD_DP32(data, CPUCFG17, L1IU_SIZE, 6);
    env->cpucfg[17] = data;

    data = 0;
    data = FIELD_DP32(data, CPUCFG18, L1D_WAYS, 7);
    data = FIELD_DP32(data, CPUCFG18, L1D_SETS, 8);
    data = FIELD_DP32(data, CPUCFG18, L1D_SIZE, 6);
    env->cpucfg[18] = data;

    env->CSR_ASID = FIELD_DP64(0, CSR_ASID, ASIDBITS, 0xa);

    env->CSR_PRCFG1 = FIELD_DP64(env->CSR_PRCFG1, CSR_PRCFG1, SAVE_NUM, 8);
    env->CSR_PRCFG1 = FIELD_DP64(env->CSR_PRCFG1, CSR_PRCFG1, TIMER_BITS, 0x2f);
    env->CSR_PRCFG1 = FIELD_DP64(env->CSR_PRCFG1, CSR_PRCFG1, VSMAX, 7);

    env->CSR_PRCFG2 = 0x1004000; // support 16KB and 32MB page size

    env->CSR_PRCFG3 = FIELD_DP64(env->CSR_PRCFG3, CSR_PRCFG3, TLB_TYPE, 1);
    env->CSR_PRCFG3 = FIELD_DP64(env->CSR_PRCFG3, CSR_PRCFG3, MTLB_ENTRY, 0x3f); // 64 entries

    env->CSR_STLBPS = 0xe; // 16KB page size
}
