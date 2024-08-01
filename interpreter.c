#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "tcg/tcg-gvec-desc.h"
#include "fpu/softfloat.h"

#if defined(CONFIG_USER_ONLY)
#include "user.h"
#endif

#include "util.h"

#include <stdalign.h>

#ifndef CONFIG_DIFF
static inline long long la_get_tval(CPULoongArchState *env){
    if (determined) {
        return current_env->icount / TIME_SCALE;
    } else {
        return nano_second() / TIMER_PERIOD;
    }
}
#endif

#ifndef CONFIG_USER_ONLY

#define CHECK_FPE(bytes)                                                                                              \
    do {                                                                                                              \
        if (bytes == 8) {                                                                                             \
            if (!FIELD_EX32(env->cpucfg[2], CPUCFG2, FP)) {return false;};                                            \
            if (!FIELD_EX64(env->CSR_EUEN, CSR_EUEN, FPE)) {do_raise_exception(env, EXCCODE_FPD, 0); return true;}    \
            PERF_INC(COUNTER_INST_FP);                                                                                \
        } else if (bytes == 16) {                                                                                     \
            if (!FIELD_EX32(env->cpucfg[2], CPUCFG2, LSX)) {return false;};                                           \
            if (!FIELD_EX64(env->CSR_EUEN, CSR_EUEN, SXE)) {do_raise_exception(env, EXCCODE_SXD, 0); return true;}    \
            PERF_INC(COUNTER_INST_LSX);                                                                               \
        } else if (bytes == 32) {                                                                                     \
            if (!FIELD_EX32(env->cpucfg[2], CPUCFG2, LASX)) {return false;};                                          \
            if (!FIELD_EX64(env->CSR_EUEN, CSR_EUEN, ASXE)) {do_raise_exception(env, EXCCODE_ASXD, 0); return true;}  \
            PERF_INC(COUNTER_INST_LASX);                                                                              \
        } else {lsassert(0);};                                                                                        \
    } while (0)

#else
#define CHECK_FPE(bytes)                                                                                              \
    do {                                                                                                              \
        if (bytes == 8) {                                                                                             \
            PERF_INC(COUNTER_INST_FP);                                                                                \
        } else if (bytes == 16) {                                                                                     \
            PERF_INC(COUNTER_INST_LSX);                                                                               \
        } else if (bytes == 32) {                                                                                     \
            PERF_INC(COUNTER_INST_LASX);                                                                              \
        } else {lsassert(0);};                                                                                        \
    } while (0)
#endif
#ifdef CONFIG_DIFF
#define __NOT_IMPLEMENTED__ __NOT_IMPLEMENTED_EXIT__
#else
#define __NOT_IMPLEMENTED__ do {fprintf(stderr, "LA_EMU NOT IMPLEMENTED %s, pc:%lx\n", __func__, env->pc); env->pc += 4; return false;} while(0);
#endif
#define __NOT_CORRECTED_IMPLEMENTED__ do {fprintf(stderr, "LA_EMU NOT CORRECTED IMPLEMENTED %s, pc:%lx\n", __func__, env->pc);} while(0);
#define __NOT_IMPLEMENTED_EXIT__ do {fprintf(stderr, "LA_EMU NOT IMPLEMENTED %s, pc:%lx\n", __func__, env->pc); laemu_exit(1); return false;} while(0);

#define DisasContext CPULoongArchState
#define ctx env
#define TCGv int64_t
static inline int plus_1(DisasContext *ctx, int x)
{
    return x + 1;
}

static inline int shl_1(DisasContext *ctx, int x)
{
    return x << 1;
}

static inline int shl_2(DisasContext *ctx, int x)
{
    return x << 2;
}

static inline int shl_3(DisasContext *ctx, int x)
{
    return x << 3;
}
#include "trans_la.c.inc"

/*
 * If an operation is being performed on less than TARGET_LONG_BITS,
 * it may require the inputs to be sign- or zero-extended; which will
 * depend on the exact operation being performed.
 */
typedef enum {
    EXT_NONE,
    EXT_SIGN,
    EXT_ZERO,
} DisasExtend;

__attribute__((unused)) static inline int64_t gpr_src(CPULoongArchState *env, int reg_num, DisasExtend src_ext)
{
    if (reg_num == 0) {
        return 0;
    }

    switch (src_ext) {
    case EXT_NONE:
        return env->gpr[reg_num];
    case EXT_SIGN:
        return (int64_t)(int32_t)env->gpr[reg_num];
    case EXT_ZERO:
        return (uint64_t)(uint32_t)env->gpr[reg_num];
    }
    g_assert_not_reached();
}

static inline void gen_set_gpr(CPULoongArchState *env, int reg_num, int64_t t, DisasExtend dst_ext)
{
    if (reg_num != 0) {
        switch (dst_ext) {
        case EXT_NONE:
            env->gpr[reg_num] = t;
            break;
        case EXT_SIGN:
            env->gpr[reg_num] = (int64_t)(int32_t)t;
            break;
        case EXT_ZERO:
            env->gpr[reg_num] = (uint64_t)(uint32_t)t;
            break;
        default:
            g_assert_not_reached();
        }
    }
}
static inline int64_t get_fpr(CPULoongArchState *env, int reg_num) {
    return env->fpr[reg_num].vreg.D[0];
}
static inline void set_fpr(CPULoongArchState *env, int reg_num, int64_t val) {
    env->fpr[reg_num].vreg.D[0] = val;
}

/* bit0(signaling/quiet) bit1(lt) bit2(eq) bit3(un) bit4(neq) */
static uint32_t get_fcmp_flags(int cond)
{
    uint32_t flags = 0;

    if (cond & 0x1) {
        flags |= FCMP_LT;
    }
    if (cond & 0x2) {
        flags |= FCMP_EQ;
    }
    if (cond & 0x4) {
        flags |= FCMP_UN;
    }
    if (cond & 0x8) {
        flags |= FCMP_GT | FCMP_LT;
    }
    return flags;
}

static bool trans_add_w(CPULoongArchState *env, arg_add_w *restrict a) {
    env->gpr[a->rd] = (int64_t)(int32_t)(env->gpr[a->rj] + env->gpr[a->rk]);
    env->pc += 4;
    return true;
}
static bool trans_add_d(CPULoongArchState *env, arg_add_d *restrict a) {
    env->gpr[a->rd] = env->gpr[a->rj] + env->gpr[a->rk];
    env->pc += 4;
    return true;
}
static bool trans_sub_w(CPULoongArchState *env, arg_sub_w *restrict a) {
    env->gpr[a->rd] = (int64_t)(int32_t)(env->gpr[a->rj] - env->gpr[a->rk]);
    env->pc += 4;
    return true;
}
static bool trans_sub_d(CPULoongArchState *env, arg_sub_d *restrict a) {
    env->gpr[a->rd] = env->gpr[a->rj] - env->gpr[a->rk];
    env->pc += 4;
    return true;
}
static bool trans_slt(CPULoongArchState *env, arg_slt *restrict a) {
    env->gpr[a->rd] = (int64_t)env->gpr[a->rj] < (int64_t)env->gpr[a->rk];
    env->pc += 4;
    return true;
}
static bool trans_sltu(CPULoongArchState *env, arg_sltu *restrict a) {
    env->gpr[a->rd] = (uint64_t)env->gpr[a->rj] < (uint64_t)env->gpr[a->rk];
    env->pc += 4;
    return true;
}
static bool trans_slti(CPULoongArchState *env, arg_slti *restrict a) {
    env->gpr[a->rd] = (int64_t)env->gpr[a->rj] < (int64_t)a->imm;
    env->pc += 4;
    return true;
}
static bool trans_sltui(CPULoongArchState *env, arg_sltui *restrict a) {
    env->gpr[a->rd] = (uint64_t)env->gpr[a->rj] < (uint64_t)(int64_t)a->imm;
    env->pc += 4;
    return true;
}
static bool trans_nor(CPULoongArchState *env, arg_nor *restrict a) {
    env->gpr[a->rd] = ~(env->gpr[a->rj] | env->gpr[a->rk]);
    env->pc += 4;
    return true;
}
static bool trans_and(CPULoongArchState *env, arg_and *restrict a) {
    env->gpr[a->rd] = env->gpr[a->rj] & env->gpr[a->rk];
    env->pc += 4;
    return true;
}
static bool trans_or(CPULoongArchState *env, arg_or *restrict a) {
    env->gpr[a->rd] = env->gpr[a->rj] | env->gpr[a->rk];
    env->pc += 4;
    return true;
}
static bool trans_xor(CPULoongArchState *env, arg_xor *restrict a) {
    env->gpr[a->rd] = env->gpr[a->rj] ^ env->gpr[a->rk];
    env->pc += 4;
    return true;
}

static bool trans_orn(CPULoongArchState *env, arg_orn *restrict a) {
    env->gpr[a->rd] = env->gpr[a->rj] | (~ env->gpr[a->rk]);
    env->pc += 4;
    return true;
}

static bool trans_andn(CPULoongArchState *env, arg_andn *restrict a) {
    env->gpr[a->rd] = env->gpr[a->rj] & (~ env->gpr[a->rk]);
    env->pc += 4;
    return true;
}

static bool trans_mul_w(CPULoongArchState *env, arg_mul_w *restrict a) {
    env->gpr[a->rd] = (int64_t)((int32_t)env->gpr[a->rj] * (int32_t)env->gpr[a->rk]);
    env->pc += 4;
    return true;
}
static bool trans_mulh_w(CPULoongArchState *env, arg_mulh_w *restrict a) {
    int64_t data = ((int64_t)(int32_t)env->gpr[a->rj] * (int64_t)(int32_t)env->gpr[a->rk]) >> 32;
    env->gpr[a->rd] = data;
    env->pc += 4;
    return true;
}
static bool trans_mulh_wu(CPULoongArchState *env, arg_mulh_wu *restrict a) {
    int64_t data = ((int64_t)((uint64_t)(uint32_t)env->gpr[a->rj] * (uint64_t)(uint32_t)env->gpr[a->rk])) >> 32;
    env->gpr[a->rd] = data;
    env->pc += 4;
    return true;
}
static bool trans_mul_d(CPULoongArchState *env, arg_mul_d *restrict a) {
    env->gpr[a->rd] = (int64_t)env->gpr[a->rj] * (int64_t)env->gpr[a->rk];
    env->pc += 4;
    return true;
}
static bool trans_mulh_d(CPULoongArchState *env, arg_mulh_d *restrict a) {
    uint64_t high,low;
    muls64(&low, &high, env->gpr[a->rj], env->gpr[a->rk]);
    env->gpr[a->rd] = high;
    env->pc += 4;
    return true;
}
static bool trans_mulh_du(CPULoongArchState *env, arg_mulh_du *restrict a) {
    uint64_t high,low;
    mulu64(&low, &high, env->gpr[a->rj], env->gpr[a->rk]);
    env->gpr[a->rd] = high;
    env->pc += 4;
    return true;
}
static bool trans_mulw_d_w(CPULoongArchState *env, arg_mulw_d_w *restrict a) {
    env->gpr[a->rd] = (int64_t)(int32_t)env->gpr[a->rj] * (int64_t)(int32_t)env->gpr[a->rk];
    env->pc += 4;
    return true;
}
static bool trans_mulw_d_wu(CPULoongArchState *env, arg_mulw_d_wu *restrict a) {
    env->gpr[a->rd] = (uint64_t)(uint32_t)env->gpr[a->rj] * (uint64_t)(uint32_t)env->gpr[a->rk];
    env->pc += 4;
    return true;
}
static bool trans_div_w(CPULoongArchState *env, arg_div_w *restrict a) {
    env->gpr[a->rd] = (int32_t)env->gpr[a->rj] / (int32_t)env->gpr[a->rk];
    env->pc += 4;
    return true;
}
static bool trans_mod_w(CPULoongArchState *env, arg_mod_w *restrict a) {
    env->gpr[a->rd] = (int32_t)env->gpr[a->rj] % (int32_t)env->gpr[a->rk];
    env->pc += 4;
    return true;
}
static bool trans_div_wu(CPULoongArchState *env, arg_div_wu *restrict a) {
    env->gpr[a->rd] = (int32_t)((uint32_t)env->gpr[a->rj] / (uint32_t)env->gpr[a->rk]);
    env->pc += 4;
    return true;
}
static bool trans_mod_wu(CPULoongArchState *env, arg_mod_wu *restrict a) {
    env->gpr[a->rd] = (uint32_t)env->gpr[a->rj] % (uint32_t)env->gpr[a->rk];
    env->pc += 4;
    return true;
}
static bool trans_div_d(CPULoongArchState *env, arg_div_d *restrict a) {
    env->gpr[a->rd] = (int64_t)env->gpr[a->rj] / (int64_t)env->gpr[a->rk];
    env->pc += 4;
    return true;
}
static bool trans_mod_d(CPULoongArchState *env, arg_mod_d *restrict a) {
    env->gpr[a->rd] = (int64_t)env->gpr[a->rj] % (int64_t)env->gpr[a->rk];
    env->pc += 4;
    return true;
}
static bool trans_div_du(CPULoongArchState *env, arg_div_du *restrict a) {
    env->gpr[a->rd] = (uint64_t)env->gpr[a->rj] / (uint64_t)env->gpr[a->rk];
    env->pc += 4;
    return true;
}
static bool trans_mod_du(CPULoongArchState *env, arg_mod_du *restrict a) {
    env->gpr[a->rd] = (uint64_t)env->gpr[a->rj] % (uint64_t)env->gpr[a->rk];
    env->pc += 4;
    return true;
}
static bool trans_alsl_w(CPULoongArchState *env, arg_alsl_w *restrict a) {
    env->gpr[a->rd] = (int64_t)(int32_t)((env->gpr[a->rj] << a->sa) + env->gpr[a->rk]);
    env->pc += 4;
    return true;
}
static bool trans_alsl_wu(CPULoongArchState *env, arg_alsl_wu *restrict a) {
    env->gpr[a->rd] = (uint32_t)((env->gpr[a->rj] << a->sa) + env->gpr[a->rk]);
    env->pc += 4;
    return true;
}
static bool trans_alsl_d(CPULoongArchState *env, arg_alsl_d *restrict a) {
    env->gpr[a->rd] = (env->gpr[a->rj] << a->sa) + env->gpr[a->rk];
    env->pc += 4;
    return true;
}
static bool trans_lu12i_w(CPULoongArchState *env, arg_lu12i_w *restrict a) {
    env->gpr[a->rd] = (int64_t)(a->imm << 12);
    env->pc += 4;
    return true;
}
static bool trans_lu32i_d(CPULoongArchState *env, arg_lu32i_d *restrict a) {
    env->gpr[a->rd] = (uint64_t)(uint32_t)env->gpr[a->rd] | ((int64_t)a->imm << 32);
    env->pc += 4;
    return true;
}
static bool trans_lu52i_d(CPULoongArchState *env, arg_lu52i_d *restrict a) {
    env->gpr[a->rd] = deposit64(env->gpr[a->rj], 52, 12, a->imm);
    env->pc += 4;
    return true;
}
static bool trans_pcaddi(CPULoongArchState *env, arg_pcaddi *restrict a) {
    env->gpr[a->rd] = env->pc + (a->imm << 2);
    env->pc += 4;
    return true;
}
static bool trans_pcalau12i(CPULoongArchState *env, arg_pcalau12i *restrict a) {
    env->gpr[a->rd] = env->pc + (a->imm << 12);
    env->gpr[a->rd] &= ~0xfffull;
    env->pc += 4;
    return true;
}
static bool trans_pcaddu12i(CPULoongArchState *env, arg_pcaddu12i *restrict a) {
    env->gpr[a->rd] = env->pc + (a->imm << 12);
    env->pc += 4;
    return true;
}
static bool trans_pcaddu18i(CPULoongArchState *env, arg_pcaddu18i *restrict a) {
    env->gpr[a->rd] = env->pc + (a->imm << 18);
    env->pc += 4;
    return true;
}
static bool trans_addi_w(CPULoongArchState *env, arg_addi_w *restrict a) {
    env->gpr[a->rd] = (int64_t)(int32_t)(env->gpr[a->rj] + a->imm);
    env->pc += 4;
    return true;
}
static bool trans_addi_d(CPULoongArchState *env, arg_addi_d *restrict a) {
    env->gpr[a->rd] = env->gpr[a->rj] + a->imm;
    env->pc += 4;
    return true;
}
static bool trans_addu16i_d(CPULoongArchState *env, arg_addu16i_d *restrict a) {
    env->gpr[a->rd] = env->gpr[a->rj] + (a->imm << 16);
    env->pc += 4;
    return true;
}
static bool trans_andi(CPULoongArchState *env, arg_andi *restrict a) {
    env->gpr[a->rd] = env->gpr[a->rj] & a->imm;
    env->pc += 4;
    return true;
}
static bool trans_ori(CPULoongArchState *env, arg_ori *restrict a) {
    env->gpr[a->rd] = env->gpr[a->rj] | a->imm;
    env->pc += 4;
    return true;
}
static bool trans_xori(CPULoongArchState *env, arg_xori *restrict a) {
    env->gpr[a->rd] = env->gpr[a->rj] ^ a->imm;
    env->pc += 4;
    return true;
}
static bool trans_sll_w(CPULoongArchState *env, arg_sll_w *restrict a) {
    env->gpr[a->rd] = (int64_t)((int32_t)env->gpr[a->rj] << (env->gpr[a->rk] & 0x1f));
    env->pc += 4;
    return true;
}
static bool trans_srl_w(CPULoongArchState *env, arg_srl_w *restrict a) {
    env->gpr[a->rd] = (int64_t)(int32_t)((uint32_t)env->gpr[a->rj] >> (env->gpr[a->rk] & 0x1f));
    env->pc += 4;
    return true;
}
static bool trans_sra_w(CPULoongArchState *env, arg_sra_w *restrict a) {
    env->gpr[a->rd] = (int64_t)((int32_t)env->gpr[a->rj] >> (env->gpr[a->rk] & 0x1f));
    env->pc += 4;
    return true;
}
static bool trans_sll_d(CPULoongArchState *env, arg_sll_d *restrict a) {
    env->gpr[a->rd] = env->gpr[a->rj] << (env->gpr[a->rk] & 0x3f);
    env->pc += 4;
    return true;
}
static bool trans_srl_d(CPULoongArchState *env, arg_srl_d *restrict a) {
    env->gpr[a->rd] = (uint64_t)env->gpr[a->rj] >> (env->gpr[a->rk] & 0x3f);
    env->pc += 4;
    return true;
}
static bool trans_sra_d(CPULoongArchState *env, arg_sra_d *restrict a) {
    env->gpr[a->rd] = (int64_t)env->gpr[a->rj] >> (env->gpr[a->rk] & 0x3f);
    env->pc += 4;
    return true;
}
static bool trans_rotr_w(CPULoongArchState *env, arg_rotr_w *restrict a) {
    uint32_t rj = env->gpr[a->rj];
    int imm = env->gpr[a->rk] & 0x1f;
    env->gpr[a->rd] = (int64_t)(int32_t)((rj >> imm) | (rj << (32 - imm)));
    env->pc += 4;
    return true;
}
static bool trans_rotr_d(CPULoongArchState *env, arg_rotr_d *restrict a) {
    uint64_t rj = env->gpr[a->rj];
    int imm = env->gpr[a->rk] & 0x3f;
    env->gpr[a->rd] = (rj >> imm) | (rj << (64 - imm));
    env->pc += 4;
    return true;
}
static bool trans_slli_w(CPULoongArchState *env, arg_slli_w *restrict a) {
    env->gpr[a->rd] = (int64_t)((int32_t)env->gpr[a->rj] << a->imm);
    env->pc += 4;
    return true;
}
static bool trans_slli_d(CPULoongArchState *env, arg_slli_d *restrict a) {
    env->gpr[a->rd] = env->gpr[a->rj] << a->imm;
    env->pc += 4;
    return true;
}
static bool trans_srli_w(CPULoongArchState *env, arg_srli_w *restrict a) {
    env->gpr[a->rd] = (int64_t)((uint32_t)env->gpr[a->rj] >> a->imm);
    env->pc += 4;
    return true;
}
static bool trans_srli_d(CPULoongArchState *env, arg_srli_d *restrict a) {
    env->gpr[a->rd] = env->gpr[a->rj] >> a->imm;
    env->pc += 4;
    return true;
}
static bool trans_srai_w(CPULoongArchState *env, arg_srai_w *restrict a) {
    env->gpr[a->rd] = (int64_t)((int32_t)env->gpr[a->rj] >> a->imm);
    env->pc += 4;
    return true;
}
static bool trans_srai_d(CPULoongArchState *env, arg_srai_d *restrict a) {
    env->gpr[a->rd] = (int64_t)env->gpr[a->rj] >> a->imm;
    env->pc += 4;
    return true;
}
static bool trans_rotri_w(CPULoongArchState *env, arg_rotri_w *restrict a) {
    uint32_t rj = env->gpr[a->rj];
    int imm = a->imm & 0x1f;
    env->gpr[a->rd] = (int64_t)(int32_t)((rj >> imm) | (rj << (32 - imm)));
    env->pc += 4;
    return true;
}
static bool trans_rotri_d(CPULoongArchState *env, arg_rotri_d *restrict a) {
    uint64_t rj = env->gpr[a->rj];
    int imm = a->imm & 0x3f;
    env->gpr[a->rd] = (rj >> imm) | (rj << (64 - imm));
    env->pc += 4;
    return true;
}
static bool trans_ext_w_h(CPULoongArchState *env, arg_ext_w_h *restrict a) {
    env->gpr[a->rd] = (int64_t)(int16_t)env->gpr[a->rj];
    env->pc += 4;
    return true;
}
static bool trans_ext_w_b(CPULoongArchState *env, arg_ext_w_b *restrict a) {
    env->gpr[a->rd] = (int64_t)(int8_t)env->gpr[a->rj];
    env->pc += 4;
    return true;
}
static bool trans_clo_w(CPULoongArchState *env, arg_clo_w *restrict a) {
    env->gpr[a->rd] = env->gpr[a->rj] ? clo32(env->gpr[a->rj]) : 0;
    env->pc += 4;
    return true;
}
static bool trans_clz_w(CPULoongArchState *env, arg_clz_w *restrict a) {
    env->gpr[a->rd] = env->gpr[a->rj] ? clz32(env->gpr[a->rj]) : 32;
    env->pc += 4;
    return true;
}
static bool trans_cto_w(CPULoongArchState *env, arg_cto_w *restrict a) {
    env->gpr[a->rd] = env->gpr[a->rj] ? cto32(env->gpr[a->rj]) : 0;
    env->pc += 4;
    return true;
}
static bool trans_ctz_w(CPULoongArchState *env, arg_ctz_w *restrict a) {
    env->gpr[a->rd] = env->gpr[a->rj] ? ctz32(env->gpr[a->rj]) : 32;
    env->pc += 4;
    return true;
}
static bool trans_clo_d(CPULoongArchState *env, arg_clo_d *restrict a) {
    env->gpr[a->rd] = env->gpr[a->rj] ? clo64(env->gpr[a->rj]) : 0;
    env->pc += 4;
    return true;
}
static bool trans_clz_d(CPULoongArchState *env, arg_clz_d *restrict a) {
    env->gpr[a->rd] = env->gpr[a->rj] ? clz64(env->gpr[a->rj]) : 64;
    env->pc += 4;
    return true;
}
static bool trans_cto_d(CPULoongArchState *env, arg_cto_d *restrict a) {
    env->gpr[a->rd] = env->gpr[a->rj] ? cto64(env->gpr[a->rj]) : 0;
    env->pc += 4;
    return true;
}
static bool trans_ctz_d(CPULoongArchState *env, arg_ctz_d *restrict a) {
    env->gpr[a->rd] = env->gpr[a->rj] ? ctz64(env->gpr[a->rj]) : 64;
    env->pc += 4;
    return true;
}
static bool trans_revb_2h(CPULoongArchState *env, arg_revb_2h *restrict a) {
    uint32_t mask = 0x00FF00FF;
    uint32_t rj = env->gpr[a->rj];
    env->gpr[a->rd] = (int64_t)(int32_t)(((rj >> 8) & mask) | ((rj & mask ) << 8));
    env->pc += 4;
    return true;
}
static bool trans_revb_4h(CPULoongArchState *env, arg_revb_4h *restrict a) {
    uint64_t mask = 0x00FF00FF00FF00FFULL;
    uint64_t rj = env->gpr[a->rj];
    env->gpr[a->rd] = ((rj >> 8) & mask) | ((rj & mask ) << 8);
    env->pc += 4;
    return true;
}
static bool trans_revb_2w(CPULoongArchState *env, arg_revb_2w *restrict a) {
    uint64_t rj = env->gpr[a->rj];
    env->gpr[a->rd] =
        ((rj & 0xFF00000000000000u) >> 24u) |
        ((rj & 0x00FF000000000000u) >>  8u) |
        ((rj & 0x0000FF0000000000u) <<  8u) |
        ((rj & 0x000000FF00000000u) << 24u) |
        ((rj & 0x00000000FF000000u) >> 24u) |
        ((rj & 0x0000000000FF0000u) >>  8u) |
        ((rj & 0x000000000000FF00u) <<  8u) |
        ((rj & 0x00000000000000FFu) << 24u);
    env->pc += 4;
    return true;
}
static bool trans_revb_d(CPULoongArchState *env, arg_revb_d *restrict a) {
    uint64_t rj = env->gpr[a->rj];
    env->gpr[a->rd] =
        ((rj & 0xFF00000000000000u) >> 56u) |
        ((rj & 0x00FF000000000000u) >> 40u) |
        ((rj & 0x0000FF0000000000u) >> 24u) |
        ((rj & 0x000000FF00000000u) >>  8u) |
        ((rj & 0x00000000FF000000u) <<  8u) |
        ((rj & 0x0000000000FF0000u) << 24u) |
        ((rj & 0x000000000000FF00u) << 40u) |
        ((rj & 0x00000000000000FFu) << 56u);
    env->pc += 4;
    return true;
}
static bool trans_revh_2w(CPULoongArchState *env, arg_revh_2w *restrict a) {
    uint64_t rj = env->gpr[a->rj];
    env->gpr[a->rd] =
        ((rj & 0xFFFF000000000000u) >> 16u) |
        ((rj & 0x0000FFFF00000000u) << 16u) |
        ((rj & 0x00000000FFFF0000u) >> 16u) |
        ((rj & 0x000000000000FFFFu) << 16u);
    env->pc += 4;
    return true;
}
static bool trans_revh_d(CPULoongArchState *env, arg_revh_d *restrict a) {
    uint64_t mask = 0x0000FFFF0000FFFFULL;
    uint64_t rj = env->gpr[a->rj];
    uint64_t t = ((rj >> 16) & mask) | ((rj & mask ) << 16);
    env->gpr[a->rd] = (t >> 32) | (t << 32);
    env->pc += 4;
    return true;
}
target_ulong helper_bitswap(target_ulong v)
{
    v = ((v >> 1) & (target_ulong)0x5555555555555555ULL) |
        ((v & (target_ulong)0x5555555555555555ULL) << 1);
    v = ((v >> 2) & (target_ulong)0x3333333333333333ULL) |
        ((v & (target_ulong)0x3333333333333333ULL) << 2);
    v = ((v >> 4) & (target_ulong)0x0F0F0F0F0F0F0F0FULL) |
        ((v & (target_ulong)0x0F0F0F0F0F0F0F0FULL) << 4);
    return v;
}
static bool trans_bitrev_4b(CPULoongArchState *env, arg_bitrev_4b *restrict a) {
    gen_set_gpr(env, a->rd, helper_bitswap(env->gpr[a->rj]), EXT_SIGN);
    env->pc += 4;
    return true;
}
static bool trans_bitrev_8b(CPULoongArchState *env, arg_bitrev_8b *restrict a) {
    gen_set_gpr(env, a->rd, helper_bitswap(env->gpr[a->rj]), EXT_NONE);
    env->pc += 4;
    return true;
}
static bool trans_bitrev_w(CPULoongArchState *env, arg_bitrev_w *restrict a) {
    gen_set_gpr(env, a->rd, revbit32(env->gpr[a->rj]), EXT_SIGN);
    env->pc += 4;
    return true;
}
static bool trans_bitrev_d(CPULoongArchState *env, arg_bitrev_d *restrict a) {
    gen_set_gpr(env, a->rd, revbit64(env->gpr[a->rj]), EXT_NONE);
    env->pc += 4;
    return true;
}
static bool trans_bytepick_w(CPULoongArchState *env, arg_bytepick_w *restrict a) {
    uint64_t t = (env->gpr[a->rk] << 32) | (uint32_t)env->gpr[a->rj];
    env->gpr[a->rd] = (int64_t)(int32_t)(t >> (32 - a->sa * 8));
    env->pc += 4;
    return true;
}
static bool trans_bytepick_d(CPULoongArchState *env, arg_bytepick_d *restrict a) {
    uint64_t high = env->gpr[a->rk] << (a->sa * 8);
    uint64_t low  = env->gpr[a->rj] >> (64 - a->sa * 8);
    env->gpr[a->rd] = high | low;
    env->pc += 4;
    return true;
}
static bool trans_maskeqz(CPULoongArchState *env, arg_maskeqz *restrict a) {
    env->gpr[a->rd] = env->gpr[a->rk] == 0 ? 0 : env->gpr[a->rj];
    env->pc += 4;
    return true;
}
static bool trans_masknez(CPULoongArchState *env, arg_masknez *restrict a) {
    env->gpr[a->rd] = env->gpr[a->rk] != 0 ? 0 : env->gpr[a->rj];
    env->pc += 4;
    return true;
}
static bool trans_bstrins_w(CPULoongArchState *env, arg_bstrins_w *restrict a)  {
    env->gpr[a->rd] = (int64_t)(int32_t)deposit32(env->gpr[a->rd], a->ls, a->ms - a->ls + 1, env->gpr[a->rj]);
    env->pc += 4;
    return true;
}
static bool trans_bstrpick_w(CPULoongArchState *env, arg_bstrpick_w *restrict a) {
    if (a->ls > a->ms) {
        return false;
    }
    env->gpr[a->rd] = (int64_t)extract32(env->gpr[a->rj], a->ls, a->ms - a->ls + 1);
    env->pc += 4;
    return true;
}
static bool trans_bstrins_d(CPULoongArchState *env, arg_bstrins_d *restrict a) {
    env->gpr[a->rd] = deposit64(env->gpr[a->rd], a->ls, a->ms - a->ls + 1, env->gpr[a->rj]);
    env->pc += 4;
    return true;
}
static bool trans_bstrpick_d(CPULoongArchState *env, arg_bstrpick_d *restrict a) {
    if (a->ls > a->ms) {
        return false;
    }
    env->gpr[a->rd] = extract64(env->gpr[a->rj], a->ls, a->ms - a->ls + 1);
    env->pc += 4;
    return true;
}

bool is_one_page(uint64_t addr, int bytes) {
    target_ulong pgsz = 0x4000;
    target_ulong pgmsk = pgsz - 1;
    return (addr & ~pgmsk) == ((addr + bytes - 1) & ~pgmsk);
}

bool is_two_page(uint64_t addr, int bytes) {
    return !is_one_page(addr, bytes);
}

bool is_aligned(uint64_t addr, int bytes) {
#ifdef CONFIG_USER_ONLY
        return true;
#endif
    return is_one_page(addr, bytes);
    // return !(addr & (bytes - 1));
}

bool is_unaligned(uint64_t addr, int bytes) {
    return !is_aligned(addr, bytes);
}

static hwaddr load_pa(CPULoongArchState *env, uint64_t addr) {
    PERF_INC(COUNTER_INST_LOAD);
#ifdef CONFIG_USER_ONLY
        return addr;
#endif
    hwaddr ha;
    int prot;
    int tc_index = TC_INDEX(addr);
    TLBCache* tc = env->tc_load + tc_index;
    uint64_t page_addr = addr & TARGET_PAGE_MASK;
    if (likely(page_addr == tc->va)) {
        uint64_t ha = (addr & (TARGET_PAGE_SIZE - 1)) | tc->pa;
        // fprintf(stderr, "%lx %lx\n", addr, ha);
        return ha;
    }
    int mmu_idx = FIELD_EX64(env->CSR_CRMD, CSR_CRMD, PLV) == 0 ? MMU_KERNEL_IDX : MMU_USER_IDX;
    check_get_physical_address(env, &ha, &prot, addr, MMU_DATA_LOAD, mmu_idx);
    tc->va = page_addr;
    tc->pa = ha & TARGET_PAGE_MASK;
    return ha;
}
static hwaddr store_pa(CPULoongArchState *env, uint64_t addr) {
    PERF_INC(COUNTER_INST_STORE);
#ifdef CONFIG_USER_ONLY
        return addr;
#endif
    hwaddr ha;
    int prot;
    int tc_index = TC_INDEX(addr);
    TLBCache* tc = env->tc_store + tc_index;
    uint64_t page_addr = addr & TARGET_PAGE_MASK;
    if (likely(page_addr == tc->va)) {
        ha = (addr & (TARGET_PAGE_SIZE - 1)) | tc->pa;
        // fprintf(stderr, "%lx %lx\n", addr, ha);
    } else {
        int mmu_idx = FIELD_EX64(env->CSR_CRMD, CSR_CRMD, PLV) == 0 ? MMU_KERNEL_IDX : MMU_USER_IDX;
        check_get_physical_address(env, &ha, &prot, addr, MMU_DATA_STORE, mmu_idx);
        tc->va = page_addr;
        tc->pa = ha & TARGET_PAGE_MASK;
    }
    return ha;
}
#if defined(CONFIG_USER_ONLY) || defined(CONFIG_DIFF)
#define is_io(...) false
#else
// exclude 32MB bios
static bool is_io(hwaddr ha) {
    return (ha >= 0x10000000 && ha < 0x1c000000)
            || (ha > 0x1e000000 && ha < 0x90000000);
}
#endif

static uint64_t add_addr(int64_t base, int64_t disp) {
    return (uint64_t)(base + disp);
}

static int8_t ld_b(CPULoongArchState *env, uint64_t va) {
    hwaddr ha = load_pa(env, va);
#if defined(CONFIG_USER_ONLY)
    return ram_ldb(ha);
#else
    return is_io(ha) ? do_io_ld(ha, 1) : ram_ldb(ha);
#endif
}

static int16_t ld_h(CPULoongArchState *env, uint64_t va) {
    uint64_t data;
    const int data_size = 2;
    hwaddr ha = load_pa(env, va);
    if (is_io(ha)) {
#if !defined(CONFIG_USER_ONLY)
        data = do_io_ld(ha, data_size);
#endif
    } else {
        if (is_aligned(va, data_size)) {
            data = ram_ldh(ha);
        } else {
            PERF_INC(COUNTER_INST_CROSS_PAGE_LOAD);
            data = 0;
            for (int i = (data_size - 1); i >= 0; i--){
                data |= (((uint16_t)ld_b(env, va + i) & 0xff) << (i * 8)) ;
            }
        }
    }
    return data;
}

static int32_t ld_w(CPULoongArchState *env, uint64_t va) {
    uint64_t data;
    const int data_size = 4;
    hwaddr ha = load_pa(env, va);
    if (is_io(ha)) {
#if !defined(CONFIG_USER_ONLY)
        data = do_io_ld(ha, data_size);
#endif
    } else {
        if (is_aligned(va, data_size)) {
            data = ram_ldw(ha);
        } else {
            PERF_INC(COUNTER_INST_CROSS_PAGE_LOAD);
            data = 0;
            for (int i = (data_size - 1); i >= 0; i--){
                data |= (((uint32_t)ld_b(env, va + i) & 0xff) << (i * 8)) ;
            }
        }
    }
    return data;
}

static int64_t ld_d(CPULoongArchState *env, uint64_t va) {
    uint64_t data;
    const int data_size = 8;
    hwaddr ha = load_pa(env, va);
    if (is_io(ha)) {
#if !defined(CONFIG_USER_ONLY)
        data = do_io_ld(ha, data_size);
#endif
    } else {
        if (is_aligned(va, data_size)) {
            data = ram_ldd(ha);
        } else {
            PERF_INC(COUNTER_INST_CROSS_PAGE_LOAD);
            data = 0;
            for (int i = (data_size - 1); i >= 0; i--){
                data |= (((uint64_t)ld_b(env, va + i) & 0xff) << (i * 8)) ;
            }
        }
    }
    return data;
}

// static Int128 ld_128(CPULoongArchState *env, uint64_t va) {
//     Int128 data;
//     const int data_size = 16;
//     hwaddr ha = load_pa(env, va);
//     if (is_io(ha)) {
//         lsassert(0);
//     } else {
//         if (is_aligned(va, data_size)) {
//             data = ram_ld128(ha);
//         } else {
//             data = 0;
//             for (int i = (data_size - 1); i >= 0; i--){
//                 data |= (((Int128)ld_b(env, va + i) & 0xff) << (i * 8));
//             }
//         }
//     }
//     return data;
// }

// static VReg ld_256(CPULoongArchState *env, uint64_t va) {
//     VReg data;
//     const int data_size = 32;
//     hwaddr ha = load_pa(env, va);
//     if (is_io(ha)) {
//         lsassert(0);
//     } else {
//         if (is_aligned(va, data_size)) {
//             data = ram_ld256(ha);
//         } else {
//             for (int i = (data_size - 1); i >= 0; i--){
//                 data.B[i] = (ld_b(env, va + i) & 0xff) << (i * 8);
//             }
//         }
//     }
//     return data;
// }

static void st_b(CPULoongArchState *env, uint64_t va, uint8_t data) {
    hwaddr ha = store_pa(env, va);
#if defined(CONFIG_USER_ONLY)
    ram_stb(ha, data);
#else
    is_io(ha) ? do_io_st(ha, data, 1) : ram_stb(ha, data);
#endif
}

static void st_h(CPULoongArchState *env, uint64_t va, uint16_t data) {
    const int data_size = 2;
    hwaddr ha = store_pa(env, va);
    if (is_io(ha)) {
#if !defined(CONFIG_USER_ONLY)
        do_io_st(ha, data, data_size);
#endif
    } else {
        if (is_aligned(va, data_size)) {
            ram_sth(ha, data);
        } else {
            PERF_INC(COUNTER_INST_CROSS_PAGE_LOAD);
            for (int i = (data_size - 1); i >= 0; i--){
                st_b(env, va + i, (data >> (i * 8)) & 0xff);
            }
        }
    }
}

static void st_w(CPULoongArchState *env, uint64_t va, uint32_t data) {
    const int data_size = 4;
    hwaddr ha = store_pa(env, va);
    if (is_io(ha)) {
#if !defined(CONFIG_USER_ONLY)
        do_io_st(ha, data, data_size);
#endif
    } else {
        if (is_aligned(va, data_size)) {
            ram_stw(ha, data);
        } else {
            PERF_INC(COUNTER_INST_CROSS_PAGE_LOAD);
            for (int i = (data_size - 1); i >= 0; i--){
                st_b(env, va + i, (data >> (i * 8)) & 0xff);
            }
        }
    }
}

static void st_d(CPULoongArchState *env, uint64_t va, uint64_t data) {
    const int data_size = 8;
    hwaddr ha = store_pa(env, va);
    if (is_io(ha)) {
#if !defined(CONFIG_USER_ONLY)
        do_io_st(ha, data, data_size);
#endif
    } else {
        if (is_aligned(va, data_size)) {
            ram_std(ha, data);
        } else {
            PERF_INC(COUNTER_INST_CROSS_PAGE_LOAD);
            for (int i = (data_size - 1); i >= 0; i--){
                st_b(env, va + i, (data >> (i * 8)) & 0xff);
            }
        }
    }
}

// static void st_128(CPULoongArchState *env, uint64_t va, Int128 data) {
//     const int data_size = 16;
//     hwaddr ha = store_pa(env, va);
//     if (is_io(ha)) {
//         lsassert(0);
//     } else {
//         if (is_aligned(va, data_size)) {
//             ram_st128(ha, data);
//         } else {
//             for (int i = (data_size - 1); i >= 0; i--){
//                 st_b(env, va + i, (data >> (i * 8)) & 0xff);
//             }
//         }
//     }
// }

// static void st_256(CPULoongArchState *env, uint64_t va, VReg data) {
//     const int data_size = 32;
//     hwaddr ha = store_pa(env, va);
//     if (is_io(ha)) {
//         lsassert(0);
//     } else {
//         if (is_aligned(va, data_size)) {
//             ram_st256(ha, data);
//         } else {
//             for (int i = (data_size - 1); i >= 0; i--){
//                 st_b(env, va + i, data.B[i]);
//             }
//         }
//     }
// }

static bool trans_ld_b(CPULoongArchState *env, arg_ld_b *restrict a) {
    env->gpr[a->rd] = (int64_t)ld_b(env, add_addr(env->gpr[a->rj], a->imm));
    env->pc += 4;
    return true;
}
static bool trans_ld_h(CPULoongArchState *env, arg_ld_h *restrict a) {
    env->gpr[a->rd] = (int64_t)ld_h(env, add_addr(env->gpr[a->rj], a->imm));
    env->pc += 4;
    return true;
}
static bool trans_ld_w(CPULoongArchState *env, arg_ld_w *restrict a) {
    env->gpr[a->rd] = (int64_t)ld_w(env, add_addr(env->gpr[a->rj], a->imm));
    env->pc += 4;
    return true;
}
static bool trans_ld_d(CPULoongArchState *env, arg_ld_d *restrict a) {
    env->gpr[a->rd] = (int64_t)ld_d(env, add_addr(env->gpr[a->rj], a->imm));
    env->pc += 4;
    return true;
}

static bool trans_st_b(CPULoongArchState *env, arg_st_b *restrict a) {
    st_b(env, add_addr(env->gpr[a->rj], a->imm), env->gpr[a->rd]);
    env->pc += 4;
    return true;
}
static bool trans_st_h(CPULoongArchState *env, arg_st_h *restrict a) {
    st_h(env, add_addr(env->gpr[a->rj], a->imm), env->gpr[a->rd]);
    env->pc += 4;
    return true;
}

static bool trans_st_w(CPULoongArchState *env, arg_st_w *restrict a) {
    st_w(env, add_addr(env->gpr[a->rj], a->imm), env->gpr[a->rd]);
    env->pc += 4;
    return true;
}
static bool trans_st_d(CPULoongArchState *env, arg_st_d *restrict a) {
    st_d(env, add_addr(env->gpr[a->rj], a->imm), env->gpr[a->rd]);
    env->pc += 4;
    return true;
}
static bool trans_ld_bu(CPULoongArchState *env, arg_ld_bu *restrict a) {
    env->gpr[a->rd] = (uint8_t)ld_b(env, add_addr(env->gpr[a->rj], a->imm));
    env->pc += 4;
    return true;
}
static bool trans_ld_hu(CPULoongArchState *env, arg_ld_hu *restrict a) {
    env->gpr[a->rd] = (uint16_t)ld_h(env, add_addr(env->gpr[a->rj], a->imm));
    env->pc += 4;
    return true;
}
static bool trans_ld_wu(CPULoongArchState *env, arg_ld_wu *restrict a) {
    env->gpr[a->rd] = (uint32_t)ld_w(env, add_addr(env->gpr[a->rj], a->imm));
    env->pc += 4;
    return true;
}
static bool trans_ldx_b(CPULoongArchState *env, arg_ldx_b *restrict a) {
    env->gpr[a->rd] = (int64_t)ld_b(env, add_addr(env->gpr[a->rj], env->gpr[a->rk]));
    env->pc += 4;
    return true;
}
static bool trans_ldx_h(CPULoongArchState *env, arg_ldx_h *restrict a) {
    env->gpr[a->rd] = (int64_t)ld_h(env, add_addr(env->gpr[a->rj], env->gpr[a->rk]));
    env->pc += 4;
    return true;
}
static bool trans_ldx_w(CPULoongArchState *env, arg_ldx_w *restrict a) {
    env->gpr[a->rd] = (int64_t)ld_w(env, add_addr(env->gpr[a->rj], env->gpr[a->rk]));
    env->pc += 4;
    return true;
}
static bool trans_ldx_d(CPULoongArchState *env, arg_ldx_d *restrict a) {
    env->gpr[a->rd] = (int64_t)ld_d(env, add_addr(env->gpr[a->rj], env->gpr[a->rk]));
    env->pc += 4;
    return true;
}
static bool trans_stx_b(CPULoongArchState *env, arg_stx_b *restrict a) {
    st_b(env, add_addr(env->gpr[a->rj], env->gpr[a->rk]), env->gpr[a->rd]);
    env->pc += 4;
    return true;
}
static bool trans_stx_h(CPULoongArchState *env, arg_stx_h *restrict a) {
    st_h(env, add_addr(env->gpr[a->rj], env->gpr[a->rk]), env->gpr[a->rd]);
    env->pc += 4;
    return true;
}
static bool trans_stx_w(CPULoongArchState *env, arg_stx_w *restrict a) {
    st_w(env, add_addr(env->gpr[a->rj], env->gpr[a->rk]), env->gpr[a->rd]);
    env->pc += 4;
    return true;
}
static bool trans_stx_d(CPULoongArchState *env, arg_stx_d *restrict a) {
    st_d(env, add_addr(env->gpr[a->rj], env->gpr[a->rk]), env->gpr[a->rd]);
    env->pc += 4;
    return true;
}
static bool trans_ldx_bu(CPULoongArchState *env, arg_ldx_bu *restrict a) {
    env->gpr[a->rd] = (uint8_t)ld_b(env, add_addr(env->gpr[a->rj], env->gpr[a->rk]));
    env->pc += 4;
    return true;
}
static bool trans_ldx_hu(CPULoongArchState *env, arg_ldx_hu *restrict a) {
    env->gpr[a->rd] = (uint16_t)ld_h(env, add_addr(env->gpr[a->rj], env->gpr[a->rk]));
    env->pc += 4;
    return true;
}
static bool trans_ldx_wu(CPULoongArchState *env, arg_ldx_wu *restrict a) {
    env->gpr[a->rd] = (uint32_t)ld_w(env, add_addr(env->gpr[a->rj], env->gpr[a->rk]));
    env->pc += 4;
    return true;
}
static bool trans_preld(CPULoongArchState *env, arg_preld *restrict a) {
    env->pc += 4;
    return true;
}
static bool trans_preldx(CPULoongArchState *env, arg_preldx *restrict a) {
    env->pc += 4;
    return true;
}
static bool trans_dbar(CPULoongArchState *env, arg_dbar *restrict a) {
    env->pc += 4;
    return true;
}
static bool trans_ibar(CPULoongArchState *env, arg_ibar *restrict a) {
#ifndef CONFIG_DIFF
    static double begin_timestamp;
    if (a->imm == 64) {
        begin_timestamp = second();
        fprintf(stderr, "[INST HACK] ibar 64 begin %f\n", begin_timestamp);
    } else if (a->imm == 65) {
        fprintf(stderr, "[INST HACK] ibar 65 end %f\n", second() - begin_timestamp);
        dump_exec_info(env, stderr);
        laemu_exit(0);
    }
#endif
    env->pc += 4;
    return true;
}
static bool trans_ldptr_w(CPULoongArchState *env, arg_ldptr_w *restrict a) {
    env->gpr[a->rd] = (int64_t)ld_w(env, add_addr(env->gpr[a->rj], a->imm));
    env->pc += 4;
    return true;
}
static bool trans_stptr_w(CPULoongArchState *env, arg_stptr_w *restrict a) {
    st_w(env, add_addr(env->gpr[a->rj], a->imm), env->gpr[a->rd]);
    env->pc += 4;
    return true;
}
static bool trans_ldptr_d(CPULoongArchState *env, arg_ldptr_d *restrict a) {
    env->gpr[a->rd] = (int64_t)ld_d(env, add_addr(env->gpr[a->rj], a->imm));
    env->pc += 4;
    return true;
}
static bool trans_stptr_d(CPULoongArchState *env, arg_stptr_d *restrict a) {
    st_d(env, add_addr(env->gpr[a->rj], a->imm), env->gpr[a->rd]);
    env->pc += 4;
    return true;
}
static bool trans_ldgt_b(CPULoongArchState *env, arg_ldgt_b *restrict a) {__NOT_IMPLEMENTED__}
static bool trans_ldgt_h(CPULoongArchState *env, arg_ldgt_h *restrict a) {__NOT_IMPLEMENTED__}
static bool trans_ldgt_w(CPULoongArchState *env, arg_ldgt_w *restrict a) {__NOT_IMPLEMENTED__}
static bool trans_ldgt_d(CPULoongArchState *env, arg_ldgt_d *restrict a) {__NOT_IMPLEMENTED__}
static bool trans_ldle_b(CPULoongArchState *env, arg_ldle_b *restrict a) {__NOT_IMPLEMENTED__}
static bool trans_ldle_h(CPULoongArchState *env, arg_ldle_h *restrict a) {__NOT_IMPLEMENTED__}
static bool trans_ldle_w(CPULoongArchState *env, arg_ldle_w *restrict a) {__NOT_IMPLEMENTED__}
static bool trans_ldle_d(CPULoongArchState *env, arg_ldle_d *restrict a) {__NOT_IMPLEMENTED__}
static bool trans_stgt_b(CPULoongArchState *env, arg_stgt_b *restrict a) {__NOT_IMPLEMENTED__}
static bool trans_stgt_h(CPULoongArchState *env, arg_stgt_h *restrict a) {__NOT_IMPLEMENTED__}
static bool trans_stgt_w(CPULoongArchState *env, arg_stgt_w *restrict a) {__NOT_IMPLEMENTED__}
static bool trans_stgt_d(CPULoongArchState *env, arg_stgt_d *restrict a) {__NOT_IMPLEMENTED__}
static bool trans_stle_b(CPULoongArchState *env, arg_stle_b *restrict a) {__NOT_IMPLEMENTED__}
static bool trans_stle_h(CPULoongArchState *env, arg_stle_h *restrict a) {__NOT_IMPLEMENTED__}
static bool trans_stle_w(CPULoongArchState *env, arg_stle_w *restrict a) {__NOT_IMPLEMENTED__}
static bool trans_stle_d(CPULoongArchState *env, arg_stle_d *restrict a) {__NOT_IMPLEMENTED__}
static bool trans_ll_w(CPULoongArchState *env, arg_ll_w *restrict a) {
    hwaddr ha = load_pa(env, env->gpr[a->rj] + a->imm);
    env->gpr[a->rd] = ram_ldw(ha);
    env->lladdr = ha;
    env->llval = env->gpr[a->rd];
    env->CSR_LLBCTL = FIELD_DP64(env->CSR_LLBCTL, CSR_LLBCTL, ROLLB, 1);
    env->pc += 4;
    return true;
}
static bool trans_sc_w(CPULoongArchState *env, arg_sc_w *restrict a) {
    hwaddr ha = store_pa(env, env->gpr[a->rj] + a->imm);
    if (FIELD_EX64(env->CSR_LLBCTL, CSR_LLBCTL, ROLLB) &&
        env->lladdr == ha && env->llval == ram_ldw(ha)) {
        ram_stw(ha, env->gpr[a->rd]);
        env->gpr[a->rd] = 1;
    } else {
        env->gpr[a->rd] = 0;
    }
    env->pc += 4;
    return true;
}
static bool trans_ll_d(CPULoongArchState *env, arg_ll_d *restrict a) {
    hwaddr ha = load_pa(env, env->gpr[a->rj] + a->imm);
    env->gpr[a->rd] = ram_ldd(ha);
    env->lladdr = ha;
    env->llval = env->gpr[a->rd];
    env->CSR_LLBCTL = FIELD_DP64(env->CSR_LLBCTL, CSR_LLBCTL, ROLLB, 1);
    env->pc += 4;
    return true;
}
static bool trans_sc_d(CPULoongArchState *env, arg_sc_d *restrict a) {
    hwaddr ha = store_pa(env, env->gpr[a->rj] + a->imm);
    if (FIELD_EX64(env->CSR_LLBCTL, CSR_LLBCTL, ROLLB) &&
        env->lladdr == ha && env->llval == ram_ldd(ha)) {
        ram_std(ha, env->gpr[a->rd]);
        env->gpr[a->rd] = 1;
    } else {
        env->gpr[a->rd] = 0;
    }
    env->pc += 4;
    return true;
}
static bool trans_amswap_w(CPULoongArchState *env, arg_amswap_w *restrict a) {return trans_amswap_db_w(env, a);}
static bool trans_amswap_d(CPULoongArchState *env, arg_amswap_d *restrict a) {return trans_amswap_db_d(env, a);}
static bool trans_amadd_w(CPULoongArchState *env, arg_amadd_w *restrict a) {return trans_amadd_db_w(env, a);}
static bool trans_amadd_d(CPULoongArchState *env, arg_amadd_d *restrict a) {return trans_amadd_db_d(env, a);}
static bool trans_amand_w(CPULoongArchState *env, arg_amand_w *restrict a) {return trans_amand_db_w(env, a);}
static bool trans_amand_d(CPULoongArchState *env, arg_amand_d *restrict a) {return trans_amand_db_d(env, a);}
static bool trans_amor_w(CPULoongArchState *env, arg_amor_w *restrict a) {return trans_amor_db_w(env, a);}
static bool trans_amor_d(CPULoongArchState *env, arg_amor_d *restrict a) {return trans_amor_db_d(env, a);}
static bool trans_amxor_w(CPULoongArchState *env, arg_amxor_w *restrict a) {return trans_amxor_db_w(env, a);}
static bool trans_amxor_d(CPULoongArchState *env, arg_amxor_d *restrict a) {return trans_amxor_db_d(env, a);}
static bool trans_ammax_w(CPULoongArchState *env, arg_ammax_w *restrict a) {return trans_ammax_db_w(env, a);}
static bool trans_ammax_d(CPULoongArchState *env, arg_ammax_d *restrict a) {return trans_ammax_db_d(env, a);}
static bool trans_ammin_w(CPULoongArchState *env, arg_ammin_w *restrict a) {return trans_ammin_db_w(env, a);}
static bool trans_ammin_d(CPULoongArchState *env, arg_ammin_d *restrict a) {return trans_ammin_db_d(env, a);}
static bool trans_ammax_wu(CPULoongArchState *env, arg_ammax_wu *restrict a) {return trans_ammax_db_wu(env, a);}
static bool trans_ammax_du(CPULoongArchState *env, arg_ammax_du *restrict a) {return trans_ammax_db_du(env, a);}
static bool trans_ammin_wu(CPULoongArchState *env, arg_ammin_wu *restrict a) {return trans_ammin_db_wu(env, a);}
static bool trans_ammin_du(CPULoongArchState *env, arg_ammin_du *restrict a) {return trans_ammin_db_du(env, a);}
static bool trans_amswap_db_w(CPULoongArchState *env, arg_amswap_db_w *restrict a) {
    hwaddr ha = store_pa(env, env->gpr[a->rj]);
    int32_t old_v = ram_ldw(ha);
    ram_stw(ha, env->gpr[a->rk]);
    env->gpr[a->rd] = (int64_t)old_v;
    env->pc += 4;
    return true;
}
static bool trans_amswap_db_d(CPULoongArchState *env, arg_amswap_db_d *restrict a) {
    hwaddr ha = store_pa(env, env->gpr[a->rj]);
    int64_t old_v = ram_ldd(ha);
    ram_std(ha, env->gpr[a->rk]);
    env->gpr[a->rd] = old_v;
    env->pc += 4;
    return true;
}
static bool trans_amadd_db_w(CPULoongArchState *env, arg_amadd_db_w *restrict a) {
    hwaddr ha = store_pa(env, env->gpr[a->rj]);
    int32_t old_v = ram_ldw(ha);
    int32_t new_v = env->gpr[a->rk] + old_v;
    ram_stw(ha, new_v);
    env->gpr[a->rd] = (int64_t)old_v;
    env->pc += 4;
    return true;
}
static bool trans_amadd_db_d(CPULoongArchState *env, arg_amadd_db_d *restrict a) {
    hwaddr ha = store_pa(env, env->gpr[a->rj]);
    int64_t old_v = ram_ldd(ha);
    int64_t new_v = env->gpr[a->rk] + old_v;
    ram_std(ha, new_v);
    env->gpr[a->rd] = (int64_t)old_v;
    env->pc += 4;
    return true;
}
static bool trans_amand_db_w(CPULoongArchState *env, arg_amand_db_w *restrict a) {
    hwaddr ha = store_pa(env, env->gpr[a->rj]);
    int32_t old_v = ram_ldw(ha);
    int32_t new_v = env->gpr[a->rk] & old_v;
    ram_stw(ha, new_v);
    env->gpr[a->rd] = (int64_t)old_v;
    env->pc += 4;
    return true;
}
static bool trans_amand_db_d(CPULoongArchState *env, arg_amand_db_d *restrict a) {
    hwaddr ha = store_pa(env, env->gpr[a->rj]);
    int64_t old_v = ram_ldd(ha);
    int64_t new_v = env->gpr[a->rk] & old_v;
    ram_std(ha, new_v);
    env->gpr[a->rd] = (int64_t)old_v;
    env->pc += 4;
    return true;
}
static bool trans_amor_db_w(CPULoongArchState *env, arg_amor_db_w *restrict a) {
    hwaddr ha = store_pa(env, env->gpr[a->rj]);
    int32_t old_v = ram_ldw(ha);
    int32_t new_v = env->gpr[a->rk] | old_v;
    ram_stw(ha, new_v);
    env->gpr[a->rd] = (int64_t)old_v;
    env->pc += 4;
    return true;
}
static bool trans_amor_db_d(CPULoongArchState *env, arg_amor_db_d *restrict a) {
    hwaddr ha = store_pa(env, env->gpr[a->rj]);
    int64_t old_v = ram_ldd(ha);
    int64_t new_v = env->gpr[a->rk] | old_v;
    ram_std(ha, new_v);
    env->gpr[a->rd] = (int64_t)old_v;
    env->pc += 4;
    return true;
}
static bool trans_amxor_db_w(CPULoongArchState *env, arg_amxor_db_w *restrict a) {
    hwaddr ha = store_pa(env, env->gpr[a->rj]);
    int32_t old_v = ram_ldw(ha);
    int32_t new_v = env->gpr[a->rk] ^ old_v;
    ram_stw(ha, new_v);
    env->gpr[a->rd] = (int64_t)old_v;
    env->pc += 4;
    return true;
}
static bool trans_amxor_db_d(CPULoongArchState *env, arg_amxor_db_d *restrict a) {
    hwaddr ha = store_pa(env, env->gpr[a->rj]);
    int64_t old_v = ram_ldd(ha);
    int64_t new_v = env->gpr[a->rk] ^ old_v;
    ram_std(ha, new_v);
    env->gpr[a->rd] = (int64_t)old_v;
    env->pc += 4;
    return true;
}
static bool trans_ammax_db_w(CPULoongArchState *env, arg_ammax_db_w *restrict a) {
    hwaddr ha = store_pa(env, env->gpr[a->rj]);
    int32_t old_v = ram_ldw(ha);
    int32_t new_v = MAX((int32_t)env->gpr[a->rk], old_v);
    ram_stw(ha, new_v);
    env->gpr[a->rd] = (int64_t)old_v;
    env->pc += 4;
    return true;
}
static bool trans_ammax_db_d(CPULoongArchState *env, arg_ammax_db_d *restrict a) {
    hwaddr ha = store_pa(env, env->gpr[a->rj]);
    int64_t old_v = ram_ldd(ha);
    int64_t new_v = MAX((int64_t)env->gpr[a->rk], old_v);
    ram_stw(ha, new_v);
    env->gpr[a->rd] = (int64_t)old_v;
    env->pc += 4;
    return true;
}
static bool trans_ammin_db_w(CPULoongArchState *env, arg_ammin_db_w *restrict a) {
    hwaddr ha = store_pa(env, env->gpr[a->rj]);
    int32_t old_v = ram_ldw(ha);
    int32_t new_v = MIN((int32_t)env->gpr[a->rk], old_v);
    ram_stw(ha, new_v);
    env->gpr[a->rd] = (int64_t)old_v;
    env->pc += 4;
    return true;
}
static bool trans_ammin_db_d(CPULoongArchState *env, arg_ammin_db_d *restrict a) {
    hwaddr ha = store_pa(env, env->gpr[a->rj]);
    int64_t old_v = ram_ldd(ha);
    int64_t new_v = MIN((int64_t)env->gpr[a->rk], old_v);
    ram_stw(ha, new_v);
    env->gpr[a->rd] = (int64_t)old_v;
    env->pc += 4;
    return true;
}
static bool trans_ammax_db_wu(CPULoongArchState *env, arg_ammax_db_wu *restrict a) {
    hwaddr ha = store_pa(env, env->gpr[a->rj]);
    int32_t old_v = ram_ldw(ha);
    int32_t new_v = MAX((uint32_t)env->gpr[a->rk], (uint32_t)old_v);
    ram_stw(ha, new_v);
    env->gpr[a->rd] = (int64_t)old_v;
    env->pc += 4;
    return true;
}
static bool trans_ammax_db_du(CPULoongArchState *env, arg_ammax_db_du *restrict a) {
    hwaddr ha = store_pa(env, env->gpr[a->rj]);
    int64_t old_v = ram_ldd(ha);
    int64_t new_v = MAX((uint64_t)env->gpr[a->rk], (uint64_t)old_v);
    ram_stw(ha, new_v);
    env->gpr[a->rd] = (int64_t)old_v;
    env->pc += 4;
    return true;
}
static bool trans_ammin_db_wu(CPULoongArchState *env, arg_ammin_db_wu *restrict a) {
    hwaddr ha = store_pa(env, env->gpr[a->rj]);
    int32_t old_v = ram_ldw(ha);
    int32_t new_v = MIN((uint32_t)env->gpr[a->rk], (uint32_t)old_v);
    ram_stw(ha, new_v);
    env->gpr[a->rd] = (int64_t)old_v;
    env->pc += 4;
    return true;
}
static bool trans_ammin_db_du(CPULoongArchState *env, arg_ammin_db_du *restrict a) {
    hwaddr ha = store_pa(env, env->gpr[a->rj]);
    int64_t old_v = ram_ldd(ha);
    int64_t new_v = MIN((uint64_t)env->gpr[a->rk], (uint64_t)old_v);
    ram_stw(ha, new_v);
    env->gpr[a->rd] = (int64_t)old_v;
    env->pc += 4;
    return true;
}
static const unsigned long crc_table[256] =
{
    0x00000000UL, 0x77073096UL, 0xee0e612cUL, 0x990951baUL, 0x076dc419UL,
    0x706af48fUL, 0xe963a535UL, 0x9e6495a3UL, 0x0edb8832UL, 0x79dcb8a4UL,
    0xe0d5e91eUL, 0x97d2d988UL, 0x09b64c2bUL, 0x7eb17cbdUL, 0xe7b82d07UL,
    0x90bf1d91UL, 0x1db71064UL, 0x6ab020f2UL, 0xf3b97148UL, 0x84be41deUL,
    0x1adad47dUL, 0x6ddde4ebUL, 0xf4d4b551UL, 0x83d385c7UL, 0x136c9856UL,
    0x646ba8c0UL, 0xfd62f97aUL, 0x8a65c9ecUL, 0x14015c4fUL, 0x63066cd9UL,
    0xfa0f3d63UL, 0x8d080df5UL, 0x3b6e20c8UL, 0x4c69105eUL, 0xd56041e4UL,
    0xa2677172UL, 0x3c03e4d1UL, 0x4b04d447UL, 0xd20d85fdUL, 0xa50ab56bUL,
    0x35b5a8faUL, 0x42b2986cUL, 0xdbbbc9d6UL, 0xacbcf940UL, 0x32d86ce3UL,
    0x45df5c75UL, 0xdcd60dcfUL, 0xabd13d59UL, 0x26d930acUL, 0x51de003aUL,
    0xc8d75180UL, 0xbfd06116UL, 0x21b4f4b5UL, 0x56b3c423UL, 0xcfba9599UL,
    0xb8bda50fUL, 0x2802b89eUL, 0x5f058808UL, 0xc60cd9b2UL, 0xb10be924UL,
    0x2f6f7c87UL, 0x58684c11UL, 0xc1611dabUL, 0xb6662d3dUL, 0x76dc4190UL,
    0x01db7106UL, 0x98d220bcUL, 0xefd5102aUL, 0x71b18589UL, 0x06b6b51fUL,
    0x9fbfe4a5UL, 0xe8b8d433UL, 0x7807c9a2UL, 0x0f00f934UL, 0x9609a88eUL,
    0xe10e9818UL, 0x7f6a0dbbUL, 0x086d3d2dUL, 0x91646c97UL, 0xe6635c01UL,
    0x6b6b51f4UL, 0x1c6c6162UL, 0x856530d8UL, 0xf262004eUL, 0x6c0695edUL,
    0x1b01a57bUL, 0x8208f4c1UL, 0xf50fc457UL, 0x65b0d9c6UL, 0x12b7e950UL,
    0x8bbeb8eaUL, 0xfcb9887cUL, 0x62dd1ddfUL, 0x15da2d49UL, 0x8cd37cf3UL,
    0xfbd44c65UL, 0x4db26158UL, 0x3ab551ceUL, 0xa3bc0074UL, 0xd4bb30e2UL,
    0x4adfa541UL, 0x3dd895d7UL, 0xa4d1c46dUL, 0xd3d6f4fbUL, 0x4369e96aUL,
    0x346ed9fcUL, 0xad678846UL, 0xda60b8d0UL, 0x44042d73UL, 0x33031de5UL,
    0xaa0a4c5fUL, 0xdd0d7cc9UL, 0x5005713cUL, 0x270241aaUL, 0xbe0b1010UL,
    0xc90c2086UL, 0x5768b525UL, 0x206f85b3UL, 0xb966d409UL, 0xce61e49fUL,
    0x5edef90eUL, 0x29d9c998UL, 0xb0d09822UL, 0xc7d7a8b4UL, 0x59b33d17UL,
    0x2eb40d81UL, 0xb7bd5c3bUL, 0xc0ba6cadUL, 0xedb88320UL, 0x9abfb3b6UL,
    0x03b6e20cUL, 0x74b1d29aUL, 0xead54739UL, 0x9dd277afUL, 0x04db2615UL,
    0x73dc1683UL, 0xe3630b12UL, 0x94643b84UL, 0x0d6d6a3eUL, 0x7a6a5aa8UL,
    0xe40ecf0bUL, 0x9309ff9dUL, 0x0a00ae27UL, 0x7d079eb1UL, 0xf00f9344UL,
    0x8708a3d2UL, 0x1e01f268UL, 0x6906c2feUL, 0xf762575dUL, 0x806567cbUL,
    0x196c3671UL, 0x6e6b06e7UL, 0xfed41b76UL, 0x89d32be0UL, 0x10da7a5aUL,
    0x67dd4accUL, 0xf9b9df6fUL, 0x8ebeeff9UL, 0x17b7be43UL, 0x60b08ed5UL,
    0xd6d6a3e8UL, 0xa1d1937eUL, 0x38d8c2c4UL, 0x4fdff252UL, 0xd1bb67f1UL,
    0xa6bc5767UL, 0x3fb506ddUL, 0x48b2364bUL, 0xd80d2bdaUL, 0xaf0a1b4cUL,
    0x36034af6UL, 0x41047a60UL, 0xdf60efc3UL, 0xa867df55UL, 0x316e8eefUL,
    0x4669be79UL, 0xcb61b38cUL, 0xbc66831aUL, 0x256fd2a0UL, 0x5268e236UL,
    0xcc0c7795UL, 0xbb0b4703UL, 0x220216b9UL, 0x5505262fUL, 0xc5ba3bbeUL,
    0xb2bd0b28UL, 0x2bb45a92UL, 0x5cb36a04UL, 0xc2d7ffa7UL, 0xb5d0cf31UL,
    0x2cd99e8bUL, 0x5bdeae1dUL, 0x9b64c2b0UL, 0xec63f226UL, 0x756aa39cUL,
    0x026d930aUL, 0x9c0906a9UL, 0xeb0e363fUL, 0x72076785UL, 0x05005713UL,
    0x95bf4a82UL, 0xe2b87a14UL, 0x7bb12baeUL, 0x0cb61b38UL, 0x92d28e9bUL,
    0xe5d5be0dUL, 0x7cdcefb7UL, 0x0bdbdf21UL, 0x86d3d2d4UL, 0xf1d4e242UL,
    0x68ddb3f8UL, 0x1fda836eUL, 0x81be16cdUL, 0xf6b9265bUL, 0x6fb077e1UL,
    0x18b74777UL, 0x88085ae6UL, 0xff0f6a70UL, 0x66063bcaUL, 0x11010b5cUL,
    0x8f659effUL, 0xf862ae69UL, 0x616bffd3UL, 0x166ccf45UL, 0xa00ae278UL,
    0xd70dd2eeUL, 0x4e048354UL, 0x3903b3c2UL, 0xa7672661UL, 0xd06016f7UL,
    0x4969474dUL, 0x3e6e77dbUL, 0xaed16a4aUL, 0xd9d65adcUL, 0x40df0b66UL,
    0x37d83bf0UL, 0xa9bcae53UL, 0xdebb9ec5UL, 0x47b2cf7fUL, 0x30b5ffe9UL,
    0xbdbdf21cUL, 0xcabac28aUL, 0x53b39330UL, 0x24b4a3a6UL, 0xbad03605UL,
    0xcdd70693UL, 0x54de5729UL, 0x23d967bfUL, 0xb3667a2eUL, 0xc4614ab8UL,
    0x5d681b02UL, 0x2a6f2b94UL, 0xb40bbe37UL, 0xc30c8ea1UL, 0x5a05df1bUL,
    0x2d02ef8dUL
};

static unsigned long crc32(uint32_t crc, const unsigned char *buf, size_t len)
{
    if (buf == NULL) return 0UL;

    crc = crc ^ 0xffffffffUL;
    if (len) do {
        crc = crc_table[((int)crc ^ (*buf++)) & 0xff] ^ (crc >> 8);
    } while (--len);
    return crc ^ 0xffffffffUL;
}

target_ulong helper_crc32(target_ulong val, target_ulong m, uint64_t sz)
{
    uint8_t buf[8];
    target_ulong mask = ((sz * 8) == 64) ? -1ULL : ((1ULL << (sz * 8)) - 1);

    m &= mask;
    stq_le_p(buf, m);
    return (int32_t) (crc32(val ^ 0xffffffff, buf, sz) ^ 0xffffffff);
}
static const uint32_t crc32c_table[256] = {
    0x00000000L, 0xF26B8303L, 0xE13B70F7L, 0x1350F3F4L,
    0xC79A971FL, 0x35F1141CL, 0x26A1E7E8L, 0xD4CA64EBL,
    0x8AD958CFL, 0x78B2DBCCL, 0x6BE22838L, 0x9989AB3BL,
    0x4D43CFD0L, 0xBF284CD3L, 0xAC78BF27L, 0x5E133C24L,
    0x105EC76FL, 0xE235446CL, 0xF165B798L, 0x030E349BL,
    0xD7C45070L, 0x25AFD373L, 0x36FF2087L, 0xC494A384L,
    0x9A879FA0L, 0x68EC1CA3L, 0x7BBCEF57L, 0x89D76C54L,
    0x5D1D08BFL, 0xAF768BBCL, 0xBC267848L, 0x4E4DFB4BL,
    0x20BD8EDEL, 0xD2D60DDDL, 0xC186FE29L, 0x33ED7D2AL,
    0xE72719C1L, 0x154C9AC2L, 0x061C6936L, 0xF477EA35L,
    0xAA64D611L, 0x580F5512L, 0x4B5FA6E6L, 0xB93425E5L,
    0x6DFE410EL, 0x9F95C20DL, 0x8CC531F9L, 0x7EAEB2FAL,
    0x30E349B1L, 0xC288CAB2L, 0xD1D83946L, 0x23B3BA45L,
    0xF779DEAEL, 0x05125DADL, 0x1642AE59L, 0xE4292D5AL,
    0xBA3A117EL, 0x4851927DL, 0x5B016189L, 0xA96AE28AL,
    0x7DA08661L, 0x8FCB0562L, 0x9C9BF696L, 0x6EF07595L,
    0x417B1DBCL, 0xB3109EBFL, 0xA0406D4BL, 0x522BEE48L,
    0x86E18AA3L, 0x748A09A0L, 0x67DAFA54L, 0x95B17957L,
    0xCBA24573L, 0x39C9C670L, 0x2A993584L, 0xD8F2B687L,
    0x0C38D26CL, 0xFE53516FL, 0xED03A29BL, 0x1F682198L,
    0x5125DAD3L, 0xA34E59D0L, 0xB01EAA24L, 0x42752927L,
    0x96BF4DCCL, 0x64D4CECFL, 0x77843D3BL, 0x85EFBE38L,
    0xDBFC821CL, 0x2997011FL, 0x3AC7F2EBL, 0xC8AC71E8L,
    0x1C661503L, 0xEE0D9600L, 0xFD5D65F4L, 0x0F36E6F7L,
    0x61C69362L, 0x93AD1061L, 0x80FDE395L, 0x72966096L,
    0xA65C047DL, 0x5437877EL, 0x4767748AL, 0xB50CF789L,
    0xEB1FCBADL, 0x197448AEL, 0x0A24BB5AL, 0xF84F3859L,
    0x2C855CB2L, 0xDEEEDFB1L, 0xCDBE2C45L, 0x3FD5AF46L,
    0x7198540DL, 0x83F3D70EL, 0x90A324FAL, 0x62C8A7F9L,
    0xB602C312L, 0x44694011L, 0x5739B3E5L, 0xA55230E6L,
    0xFB410CC2L, 0x092A8FC1L, 0x1A7A7C35L, 0xE811FF36L,
    0x3CDB9BDDL, 0xCEB018DEL, 0xDDE0EB2AL, 0x2F8B6829L,
    0x82F63B78L, 0x709DB87BL, 0x63CD4B8FL, 0x91A6C88CL,
    0x456CAC67L, 0xB7072F64L, 0xA457DC90L, 0x563C5F93L,
    0x082F63B7L, 0xFA44E0B4L, 0xE9141340L, 0x1B7F9043L,
    0xCFB5F4A8L, 0x3DDE77ABL, 0x2E8E845FL, 0xDCE5075CL,
    0x92A8FC17L, 0x60C37F14L, 0x73938CE0L, 0x81F80FE3L,
    0x55326B08L, 0xA759E80BL, 0xB4091BFFL, 0x466298FCL,
    0x1871A4D8L, 0xEA1A27DBL, 0xF94AD42FL, 0x0B21572CL,
    0xDFEB33C7L, 0x2D80B0C4L, 0x3ED04330L, 0xCCBBC033L,
    0xA24BB5A6L, 0x502036A5L, 0x4370C551L, 0xB11B4652L,
    0x65D122B9L, 0x97BAA1BAL, 0x84EA524EL, 0x7681D14DL,
    0x2892ED69L, 0xDAF96E6AL, 0xC9A99D9EL, 0x3BC21E9DL,
    0xEF087A76L, 0x1D63F975L, 0x0E330A81L, 0xFC588982L,
    0xB21572C9L, 0x407EF1CAL, 0x532E023EL, 0xA145813DL,
    0x758FE5D6L, 0x87E466D5L, 0x94B49521L, 0x66DF1622L,
    0x38CC2A06L, 0xCAA7A905L, 0xD9F75AF1L, 0x2B9CD9F2L,
    0xFF56BD19L, 0x0D3D3E1AL, 0x1E6DCDEEL, 0xEC064EEDL,
    0xC38D26C4L, 0x31E6A5C7L, 0x22B65633L, 0xD0DDD530L,
    0x0417B1DBL, 0xF67C32D8L, 0xE52CC12CL, 0x1747422FL,
    0x49547E0BL, 0xBB3FFD08L, 0xA86F0EFCL, 0x5A048DFFL,
    0x8ECEE914L, 0x7CA56A17L, 0x6FF599E3L, 0x9D9E1AE0L,
    0xD3D3E1ABL, 0x21B862A8L, 0x32E8915CL, 0xC083125FL,
    0x144976B4L, 0xE622F5B7L, 0xF5720643L, 0x07198540L,
    0x590AB964L, 0xAB613A67L, 0xB831C993L, 0x4A5A4A90L,
    0x9E902E7BL, 0x6CFBAD78L, 0x7FAB5E8CL, 0x8DC0DD8FL,
    0xE330A81AL, 0x115B2B19L, 0x020BD8EDL, 0xF0605BEEL,
    0x24AA3F05L, 0xD6C1BC06L, 0xC5914FF2L, 0x37FACCF1L,
    0x69E9F0D5L, 0x9B8273D6L, 0x88D28022L, 0x7AB90321L,
    0xAE7367CAL, 0x5C18E4C9L, 0x4F48173DL, 0xBD23943EL,
    0xF36E6F75L, 0x0105EC76L, 0x12551F82L, 0xE03E9C81L,
    0x34F4F86AL, 0xC69F7B69L, 0xD5CF889DL, 0x27A40B9EL,
    0x79B737BAL, 0x8BDCB4B9L, 0x988C474DL, 0x6AE7C44EL,
    0xBE2DA0A5L, 0x4C4623A6L, 0x5F16D052L, 0xAD7D5351L
};
static uint32_t crc32c(uint32_t crc, const uint8_t *data, unsigned int length)
{
    while (length--) {
        crc = crc32c_table[(crc ^ *data++) & 0xFFL] ^ (crc >> 8);
    }
    return crc^0xffffffff;
}

target_ulong helper_crc32c(target_ulong val, target_ulong m, uint64_t sz)
{
    uint8_t buf[8];
    target_ulong mask = ((sz * 8) == 64) ? -1ULL : ((1ULL << (sz * 8)) - 1);
    m &= mask;
    stq_le_p(buf, m);
    return (int32_t) (crc32c(val, buf, sz) ^ 0xffffffff);
}
static bool trans_crc_w_b_w(CPULoongArchState *env, arg_crc_w_b_w *restrict a) {env->gpr[a->rd] = helper_crc32(env->gpr[a->rk], env->gpr[a->rj], 1);env->pc += 4;return true;}
static bool trans_crc_w_h_w(CPULoongArchState *env, arg_crc_w_h_w *restrict a) {env->gpr[a->rd] = helper_crc32(env->gpr[a->rk], env->gpr[a->rj], 2);env->pc += 4;return true;}
static bool trans_crc_w_w_w(CPULoongArchState *env, arg_crc_w_w_w *restrict a) {env->gpr[a->rd] = helper_crc32(env->gpr[a->rk], env->gpr[a->rj], 4);env->pc += 4;return true;}
static bool trans_crc_w_d_w(CPULoongArchState *env, arg_crc_w_d_w *restrict a) {env->gpr[a->rd] = helper_crc32(env->gpr[a->rk], env->gpr[a->rj], 8);env->pc += 4;return true;}
static bool trans_crcc_w_b_w(CPULoongArchState *env, arg_crcc_w_b_w *restrict a) {env->gpr[a->rd] = helper_crc32c(env->gpr[a->rk], env->gpr[a->rj], 1);env->pc += 4;return true;}
static bool trans_crcc_w_h_w(CPULoongArchState *env, arg_crcc_w_h_w *restrict a) {env->gpr[a->rd] = helper_crc32c(env->gpr[a->rk], env->gpr[a->rj], 2);env->pc += 4;return true;}
static bool trans_crcc_w_w_w(CPULoongArchState *env, arg_crcc_w_w_w *restrict a) {env->gpr[a->rd] = helper_crc32c(env->gpr[a->rk], env->gpr[a->rj], 4);env->pc += 4;return true;}
static bool trans_crcc_w_d_w(CPULoongArchState *env, arg_crcc_w_d_w *restrict a) {env->gpr[a->rd] = helper_crc32c(env->gpr[a->rk], env->gpr[a->rj], 8);env->pc += 4;return true;}
static bool trans_break(CPULoongArchState *env, arg_break *restrict a) {

#if defined(CONFIG_USER_ONLY)
    fprintf(stderr, "trans_break\n");
    laemu_exit(0);
#else
    do_raise_exception(env, EXCCODE_BRK, 0);
#endif
    return true;
}


static bool trans_syscall(CPULoongArchState *env, arg_syscall *restrict a) {
    env->syscall_count ++;
#if defined(CONFIG_USER_ONLY)
    target_long ret = do_syscall(env, env->gpr[11],
                        env->gpr[4], env->gpr[5],
                        env->gpr[6], env->gpr[7],
                        env->gpr[8], env->gpr[9],
                        -1, -1);
    env->gpr[4] = ret;
    env->pc += 4;
#else
    do_raise_exception(env, EXCCODE_SYS, 0);
#endif
    return true;
}
static bool trans_asrtle_d(CPULoongArchState *env, arg_asrtle_d *restrict a) {
    if (env->gpr[a->rj] > env->gpr[a->rk]) {
        env->CSR_BADV = env->gpr[a->rj];
        do_raise_exception(env, EXCCODE_BCE, 0);
    }
    env->pc += 4;
    return true;
}
static bool trans_asrtgt_d(CPULoongArchState *env, arg_asrtgt_d *restrict a) {
    if (env->gpr[a->rj] <= env->gpr[a->rk]) {
        env->CSR_BADV = env->gpr[a->rj];
        do_raise_exception(env, EXCCODE_BCE, 0);
    }
    env->pc += 4;
    return true;
}
static bool trans_rdtimel_w(CPULoongArchState *env, arg_rdtimel_w *restrict a) {
#ifndef CONFIG_DIFF
    long long tval = la_get_tval(env);
    gen_set_gpr(env, a->rd, tval, EXT_SIGN);
    env->gpr[a->rj] = 0;
#endif
    env->pc += 4;
    return true;
}
static bool trans_rdtimeh_w(CPULoongArchState *env, arg_rdtimeh_w *restrict a) {
#ifndef CONFIG_DIFF
    long long tval = la_get_tval(env);
    gen_set_gpr(env, a->rd, tval >> 32, EXT_SIGN);
    env->gpr[a->rj] = 0;
#endif
    env->pc += 4;
    return true;
}
static bool trans_rdtime_d(CPULoongArchState *env, arg_rdtime_d *restrict a) {
#ifndef CONFIG_DIFF
    env->gpr[a->rd] = la_get_tval(env);
    env->gpr[a->rj] = 0;
#endif
    env->pc += 4;
    return true;
}
static bool trans_cpucfg(CPULoongArchState *env, arg_cpucfg *restrict a) {
    int index = env->gpr[a->rj];
    env->gpr[a->rd] = index >= ARRAY_SIZE(env->cpucfg) ? 0 : env->cpucfg[index];
    env->pc += 4;
    return true;
}

static bool gen_fff(CPULoongArchState *env, arg_fff *restrict a, uint64_t (*func)(CPULoongArchState *, uint64_t, uint64_t)) {
    TCGv src1 = get_fpr(ctx, a->fj);
    TCGv src2 = get_fpr(ctx, a->fk);
    TCGv dest = func(env, src1, src2);
    set_fpr(env, a->fd, dest);
    env->pc += 4;
    return true;
}
static bool trans_fadd_s(CPULoongArchState *env, arg_fadd_s *restrict a) {CHECK_FPE(8);return gen_fff(env, a, helper_fadd_s);}
static bool trans_fadd_d(CPULoongArchState *env, arg_fadd_d *restrict a) {CHECK_FPE(8);return gen_fff(env, a, helper_fadd_d);}
static bool trans_fsub_s(CPULoongArchState *env, arg_fadd_s *restrict a) {CHECK_FPE(8);return gen_fff(env, a, helper_fsub_s);}
static bool trans_fsub_d(CPULoongArchState *env, arg_fadd_d *restrict a) {CHECK_FPE(8);return gen_fff(env, a, helper_fsub_d);}
static bool trans_fmul_s(CPULoongArchState *env, arg_fadd_s *restrict a) {CHECK_FPE(8);return gen_fff(env, a, helper_fmul_s);}
static bool trans_fmul_d(CPULoongArchState *env, arg_fadd_d *restrict a) {CHECK_FPE(8);return gen_fff(env, a, helper_fmul_d);}
static bool trans_fdiv_s(CPULoongArchState *env, arg_fadd_s *restrict a) {CHECK_FPE(8);return gen_fff(env, a, helper_fdiv_s);}
static bool trans_fdiv_d(CPULoongArchState *env, arg_fadd_d *restrict a) {CHECK_FPE(8);return gen_fff(env, a, helper_fdiv_d);}
static bool trans_fmax_s(CPULoongArchState *env, arg_fmax_s *restrict a) {CHECK_FPE(8);return gen_fff(env, a, helper_fmax_s);}
static bool trans_fmax_d(CPULoongArchState *env, arg_fmax_d *restrict a) {CHECK_FPE(8);return gen_fff(env, a, helper_fmax_d);}
static bool trans_fmin_s(CPULoongArchState *env, arg_fmin_s *restrict a) {CHECK_FPE(8);return gen_fff(env, a, helper_fmin_s);}
static bool trans_fmin_d(CPULoongArchState *env, arg_fmin_d *restrict a) {CHECK_FPE(8);return gen_fff(env, a, helper_fmin_d);}
static bool trans_fmaxa_s(CPULoongArchState *env, arg_fmaxa_s *restrict a) {CHECK_FPE(8);return gen_fff(env, a, helper_fmaxa_s);}
static bool trans_fmaxa_d(CPULoongArchState *env, arg_fmaxa_d *restrict a) {CHECK_FPE(8);return gen_fff(env, a, helper_fmaxa_d);}
static bool trans_fmina_s(CPULoongArchState *env, arg_fmina_s *restrict a) {CHECK_FPE(8);return gen_fff(env, a, helper_fmina_s);}
static bool trans_fmina_d(CPULoongArchState *env, arg_fmina_d *restrict a) {CHECK_FPE(8);return gen_fff(env, a, helper_fmina_d);}
static bool trans_fscaleb_s(CPULoongArchState *env, arg_fscaleb_s *restrict a) {CHECK_FPE(8);return gen_fff(env, a, helper_fscaleb_s);}
static bool trans_fscaleb_d(CPULoongArchState *env, arg_fscaleb_d *restrict a) {CHECK_FPE(8);return gen_fff(env, a, helper_fscaleb_d);}

static bool gen_ffff(CPULoongArchState *env, arg_ffff *restrict a, uint64_t (*func)(CPULoongArchState *, uint64_t, uint64_t, uint64_t, uint32_t), uint32_t flag) {
    TCGv src1 = get_fpr(ctx, a->fj);
    TCGv src2 = get_fpr(ctx, a->fk);
    TCGv src3 = get_fpr(ctx, a->fa);
    TCGv dest = func(env, src1, src2, src3, flag);
    set_fpr(env, a->fd, dest);
    env->pc += 4;
    return true;
}
static bool trans_fmadd_s(CPULoongArchState *env, arg_fmadd_s *restrict a) {CHECK_FPE(8);return gen_ffff(env, a, helper_fmuladd_s, 0);}
static bool trans_fmadd_d(CPULoongArchState *env, arg_fmadd_d *restrict a) {CHECK_FPE(8);return gen_ffff(env, a, helper_fmuladd_d, 0);}
static bool trans_fmsub_s(CPULoongArchState *env, arg_fmsub_s *restrict a) {CHECK_FPE(8);return gen_ffff(env, a, helper_fmuladd_s, float_muladd_negate_c);}
static bool trans_fmsub_d(CPULoongArchState *env, arg_fmsub_s *restrict a) {CHECK_FPE(8);return gen_ffff(env, a, helper_fmuladd_d, float_muladd_negate_c);}
static bool trans_fnmadd_s(CPULoongArchState *env, arg_fnmadd_s *restrict a) {CHECK_FPE(8);return gen_ffff(env, a, helper_fmuladd_s, float_muladd_negate_result);}
static bool trans_fnmadd_d(CPULoongArchState *env, arg_fnmadd_d *restrict a) {CHECK_FPE(8);return gen_ffff(env, a, helper_fmuladd_d, float_muladd_negate_result);}
static bool trans_fnmsub_s(CPULoongArchState *env, arg_fnmsub_s *restrict a) {CHECK_FPE(8);return gen_ffff(env, a, helper_fmuladd_s, float_muladd_negate_c | float_muladd_negate_result);}
static bool trans_fnmsub_d(CPULoongArchState *env, arg_fnmsub_d *restrict a) {CHECK_FPE(8);return gen_ffff(env, a, helper_fmuladd_d, float_muladd_negate_c | float_muladd_negate_result);}

static bool trans_fabs_s(CPULoongArchState *env, arg_fabs_s *restrict a) {
    CHECK_FPE(8);
    TCGv src = get_fpr(ctx, a->fj);
    TCGv dest = src & MAKE_64BIT_MASK(0, 31);
    set_fpr(env, a->fd, dest);
    env->pc += 4;
    return true;
}
static bool trans_fabs_d(CPULoongArchState *env, arg_fabs_d *restrict a) {
    CHECK_FPE(8);
    TCGv src = get_fpr(ctx, a->fj);
    TCGv dest = src & MAKE_64BIT_MASK(0, 63);
    set_fpr(env, a->fd, dest);
    env->pc += 4;
    return true;
}
static bool trans_fneg_s(CPULoongArchState *env, arg_fneg_s *restrict a) {
    CHECK_FPE(8);
    TCGv src = get_fpr(ctx, a->fj);
    TCGv dest = src ^ 0x80000000ULL;
    set_fpr(env, a->fd, dest);
    env->pc += 4;
    return true;
}
static bool trans_fneg_d(CPULoongArchState *env, arg_fneg_d *restrict a) {
    CHECK_FPE(8);
    TCGv src = get_fpr(ctx, a->fj);
    TCGv dest = src ^ 0x8000000000000000ULL;
    set_fpr(env, a->fd, dest);
    env->pc += 4;
    return true;
}
static bool gen_ff(CPULoongArchState *env, arg_ff *restrict a, uint64_t (*func)(CPULoongArchState *, uint64_t)) {
    TCGv src1 = get_fpr(ctx, a->fj);
    TCGv dest = func(env, src1);
    set_fpr(env, a->fd, dest);
    env->pc += 4;
    return true;
}
static bool trans_fsqrt_s(CPULoongArchState *env, arg_fsqrt_s *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_fsqrt_s);}
static bool trans_fsqrt_d(CPULoongArchState *env, arg_fsqrt_d *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_fsqrt_d);}
static bool trans_frecip_s(CPULoongArchState *env, arg_frecip_s *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_frecip_s);}
static bool trans_frecip_d(CPULoongArchState *env, arg_frecip_d *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_frecip_d);}
static bool trans_frsqrt_s(CPULoongArchState *env, arg_frsqrt_s *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_frsqrt_s);}
static bool trans_frsqrt_d(CPULoongArchState *env, arg_frsqrt_d *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_frsqrt_d);}
static bool trans_flogb_s(CPULoongArchState *env, arg_flogb_s *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_flogb_s);}
static bool trans_flogb_d(CPULoongArchState *env, arg_flogb_d *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_flogb_d);}
static bool trans_fclass_s(CPULoongArchState *env, arg_fclass_s *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_fclass_s);}
static bool trans_fclass_d(CPULoongArchState *env, arg_fclass_d *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_fclass_d);}
static bool trans_fcvt_s_d(CPULoongArchState *env, arg_fcvt_s_d *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_fcvt_s_d);}
static bool trans_fcvt_d_s(CPULoongArchState *env, arg_fcvt_d_s *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_fcvt_d_s);}
static bool trans_ftintrm_w_s(CPULoongArchState *env, arg_ftintrm_w_s *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_ftintrm_w_s);}
static bool trans_ftintrm_w_d(CPULoongArchState *env, arg_ftintrm_w_d *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_ftintrm_w_d);}
static bool trans_ftintrm_l_s(CPULoongArchState *env, arg_ftintrm_l_s *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_ftintrm_l_s);}
static bool trans_ftintrm_l_d(CPULoongArchState *env, arg_ftintrm_l_d *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_ftintrm_l_d);}
static bool trans_ftintrp_w_s(CPULoongArchState *env, arg_ftintrp_w_s *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_ftintrp_w_s);}
static bool trans_ftintrp_w_d(CPULoongArchState *env, arg_ftintrp_w_d *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_ftintrp_w_d);}
static bool trans_ftintrp_l_s(CPULoongArchState *env, arg_ftintrp_l_s *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_ftintrp_l_s);}
static bool trans_ftintrp_l_d(CPULoongArchState *env, arg_ftintrp_l_d *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_ftintrp_l_d);}
static bool trans_ftintrz_w_s(CPULoongArchState *env, arg_ftintrz_w_s *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_ftintrz_w_s);}
static bool trans_ftintrz_w_d(CPULoongArchState *env, arg_ftintrz_w_d *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_ftintrz_w_d);}
static bool trans_ftintrz_l_s(CPULoongArchState *env, arg_ftintrz_l_s *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_ftintrz_l_s);}
static bool trans_ftintrz_l_d(CPULoongArchState *env, arg_ftintrz_l_d *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_ftintrz_l_d);}
static bool trans_ftintrne_w_s(CPULoongArchState *env, arg_ftintrne_w_s *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_ftintrne_w_s);}
static bool trans_ftintrne_w_d(CPULoongArchState *env, arg_ftintrne_w_d *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_ftintrne_w_d);}
static bool trans_ftintrne_l_s(CPULoongArchState *env, arg_ftintrne_l_s *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_ftintrne_l_s);}
static bool trans_ftintrne_l_d(CPULoongArchState *env, arg_ftintrne_l_d *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_ftintrne_l_d);}
static bool trans_ftint_w_s(CPULoongArchState *env, arg_ftint_w_s *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_ftint_w_s);}
static bool trans_ftint_w_d(CPULoongArchState *env, arg_ftint_w_d *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_ftint_w_d);}
static bool trans_ftint_l_s(CPULoongArchState *env, arg_ftint_l_s *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_ftint_l_s);}
static bool trans_ftint_l_d(CPULoongArchState *env, arg_ftint_l_d *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_ftint_l_d);}
static bool trans_ffint_s_w(CPULoongArchState *env, arg_ffint_s_w *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_ffint_s_w);}
static bool trans_ffint_s_l(CPULoongArchState *env, arg_ffint_s_l *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_ffint_s_l);}
static bool trans_ffint_d_w(CPULoongArchState *env, arg_ffint_d_w *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_ffint_d_w);}
static bool trans_ffint_d_l(CPULoongArchState *env, arg_ffint_d_l *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_ffint_d_l);}
static bool trans_frint_s(CPULoongArchState *env, arg_frint_s *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_frint_s);}
static bool trans_frint_d(CPULoongArchState *env, arg_frint_d *restrict a) {CHECK_FPE(8); return gen_ff(env, a, helper_frint_d);}
static bool trans_fcopysign_s(CPULoongArchState *env, arg_fcopysign_s *restrict a) {
    CHECK_FPE(8);
    TCGv src1 = get_fpr(ctx, a->fj);
    TCGv src2 = get_fpr(ctx, a->fk);
    TCGv dest = deposit64(src2, 0, 31, src1);
    set_fpr(env, a->fd, dest);
    env->pc += 4;
    return true;
}
static bool trans_fcopysign_d(CPULoongArchState *env, arg_fcopysign_d *restrict a) {
    CHECK_FPE(8);
    TCGv src1 = get_fpr(ctx, a->fj);
    TCGv src2 = get_fpr(ctx, a->fk);
    TCGv dest = deposit64(src2, 0, 63, src1);
    set_fpr(env, a->fd, dest);
    env->pc += 4;
    return true;
}
static bool trans_fcmp_cond_s(CPULoongArchState *env, arg_fcmp_cond_s *restrict a) {
    CHECK_FPE(8);
    TCGv src1 = get_fpr(ctx, a->fj);
    TCGv src2 = get_fpr(ctx, a->fk);
    uint32_t flags = get_fcmp_flags(a->fcond >> 1);
    int r = (a->fcond & 1) ? helper_fcmp_s_s(env, src1, src2, flags) : helper_fcmp_c_s(env, src1, src2, flags);
    env->cf[a->cd] = r;
    env->pc += 4;
    return true;
}
static bool trans_fcmp_cond_d(CPULoongArchState *env, arg_fcmp_cond_d *restrict a) {
    CHECK_FPE(8);
    TCGv src1 = get_fpr(ctx, a->fj);
    TCGv src2 = get_fpr(ctx, a->fk);
    uint32_t flags = get_fcmp_flags(a->fcond >> 1);
    int r = (a->fcond & 1) ? helper_fcmp_s_d(env, src1, src2, flags) : helper_fcmp_c_d(env, src1, src2, flags);
    env->cf[a->cd] = r;
    env->pc += 4;
    return true;
}
static bool trans_fmov_s(CPULoongArchState *env, arg_fmov_s *restrict a) {
    CHECK_FPE(8);
    env->fpr[a->fd].vreg.W[0] = env->fpr[a->fj].vreg.W[0];
    env->pc += 4;
    return true;
}
static bool trans_fmov_d(CPULoongArchState *env, arg_fmov_d *restrict a) {
    CHECK_FPE(8);
    env->fpr[a->fd].vreg.D[0] = env->fpr[a->fj].vreg.D[0];
    env->pc += 4;
    return true;
}
static bool trans_fsel(CPULoongArchState *env, arg_fsel *restrict a) {
    CHECK_FPE(8);
    TCGv src1 = get_fpr(ctx, a->fj);
    TCGv src2 = get_fpr(ctx, a->fk);
    TCGv dest = env->cf[a->ca] == 0 ? src1 : src2;
    set_fpr(env, a->fd, dest);
    env->pc += 4;
    return true;
}
static bool trans_movgr2fr_w(CPULoongArchState *env, arg_movgr2fr_w *restrict a) {
    CHECK_FPE(8);
    env->fpr[a->fd].vreg.W[0] = env->gpr[a->rj];
    env->pc += 4;
    return true;
}
static bool trans_movgr2fr_d(CPULoongArchState *env, arg_movgr2fr_d *restrict a) {
    CHECK_FPE(8);
    env->fpr[a->fd].vreg.D[0] = env->gpr[a->rj];
    env->pc += 4;
    return true;
}
static bool trans_movgr2frh_w(CPULoongArchState *env, arg_movgr2frh_w *restrict a) {
    CHECK_FPE(8);
    env->fpr[a->fd].vreg.W[1] = env->gpr[a->rj];
    env->pc += 4;
    return true;
}
static bool trans_movfr2gr_s(CPULoongArchState *env, arg_movfr2gr_s *restrict a) {
    CHECK_FPE(8);
    env->gpr[a->rd] = (int64_t)env->fpr[a->fj].vreg.W[0];
    env->pc += 4;
    return true;
}
static bool trans_movfr2gr_d(CPULoongArchState *env, arg_movfr2gr_d *restrict a) {
    CHECK_FPE(8);
    TCGv src = get_fpr(ctx, a->fj);
    TCGv dest = src;
    gen_set_gpr(env, a->rd, dest, EXT_NONE);
    env->pc += 4;
    return true;
}
static bool trans_movfrh2gr_s(CPULoongArchState *env, arg_movfrh2gr_s *restrict a) {
    CHECK_FPE(8);
    TCGv src = get_fpr(ctx, a->fj);
    TCGv dest = src;
    gen_set_gpr(env, a->rd, dest, EXT_SIGN);
    env->pc += 4;
    return true;
}


static const uint32_t fcsr_mask[4] = {
    UINT32_MAX, FCSR0_M1, FCSR0_M2, FCSR0_M3
};
static bool trans_movgr2fcsr(CPULoongArchState *env, arg_movgr2fcsr *restrict a) {
    CHECK_FPE(8);
    uint32_t mask = fcsr_mask[a->fcsrd];
    uint64_t rj = env->gpr[a->rj];
    if (mask == UINT32_MAX) {
        env->fcsr0 = rj;
    } else {
        __NOT_IMPLEMENTED__;
    }

    if (mask & FCSR0_M3) {
        helper_set_rounding_mode(env);
    }
    env->pc += 4;
    return true;
}
static bool trans_movfcsr2gr(CPULoongArchState *env, arg_movfcsr2gr *restrict a) {
    CHECK_FPE(8);
    env->gpr[a->rd] = env->fcsr0 & fcsr_mask[a->fcsrs];
    env->pc += 4;
    return true;
}
static bool trans_movfr2cf(CPULoongArchState *env, arg_movfr2cf *restrict a) {
    CHECK_FPE(8);
    env->cf[a->cd] = env->fpr[a->fj].vreg.D[0] & 1;
    env->pc += 4;
    return true;
}
static bool trans_movcf2fr(CPULoongArchState *env, arg_movcf2fr *restrict a) {
    CHECK_FPE(8);
    env->fpr[a->fd].vreg.D[0] = env->cf[a->cj] & 1;
    env->pc += 4;
    return true;
}
static bool trans_movgr2cf(CPULoongArchState *env, arg_movgr2cf *restrict a) {
    CHECK_FPE(8);
    env->cf[a->cd] = env->gpr[a->rj] & 1;
    env->pc += 4;
    return true;
}
static bool trans_movcf2gr(CPULoongArchState *env, arg_movcf2gr *restrict a) {
    CHECK_FPE(8);
    env->gpr[a->rd] = env->cf[a->cj] & 1;
    env->pc += 4;
    return true;
}
static bool trans_fld_s(CPULoongArchState *env, arg_fld_s *restrict a) {
    CHECK_FPE(8);
    set_fpr(env, a->fd, ld_w(env, add_addr(env->gpr[a->rj], a->imm)));
    env->pc += 4;
    return true;
}
static bool trans_fst_s(CPULoongArchState *env, arg_fst_s *restrict a) {
    CHECK_FPE(8);
    st_w(env, add_addr(env->gpr[a->rj], a->imm), env->fpr[a->fd].vreg.W[0]);
    env->pc += 4;
    return true;
}
static bool trans_fld_d(CPULoongArchState *env, arg_fld_d *restrict a) {
    CHECK_FPE(8);
    set_fpr(env, a->fd, ld_d(env, add_addr(env->gpr[a->rj], a->imm)));
    env->pc += 4;
    return true;
}
static bool trans_fst_d(CPULoongArchState *env, arg_fst_d *restrict a) {
    CHECK_FPE(8);
    st_d(env, add_addr(env->gpr[a->rj], a->imm), env->fpr[a->fd].vreg.D[0]);
    env->pc += 4;
    return true;
}
static bool trans_fldx_s(CPULoongArchState *env, arg_fldx_s *restrict a) {
    CHECK_FPE(8);
    set_fpr(env, a->fd, ld_w(env, add_addr(env->gpr[a->rj], env->gpr[a->rk])));
    env->pc += 4;
    return true;
}
static bool trans_fldx_d(CPULoongArchState *env, arg_fldx_d *restrict a) {
    CHECK_FPE(8);
    set_fpr(env, a->fd, ld_d(env, add_addr(env->gpr[a->rj], env->gpr[a->rk])));
    env->pc += 4;
    return true;
}
static bool trans_fstx_s(CPULoongArchState *env, arg_fstx_s *restrict a) {
    CHECK_FPE(8);
    st_w(env, add_addr(env->gpr[a->rj], env->gpr[a->rk]), env->fpr[a->fd].vreg.W[0]);
    env->pc += 4;
    return true;
}
static bool trans_fstx_d(CPULoongArchState *env, arg_fstx_d *restrict a) {
    CHECK_FPE(8);
    st_d(env, add_addr(env->gpr[a->rj], env->gpr[a->rk]), env->fpr[a->fd].vreg.D[0]);
    env->pc += 4;
    return true;
}
static bool trans_fldgt_s(CPULoongArchState *env, arg_fldgt_s *restrict a) {__NOT_IMPLEMENTED__}
static bool trans_fldgt_d(CPULoongArchState *env, arg_fldgt_d *restrict a) {__NOT_IMPLEMENTED__}
static bool trans_fldle_s(CPULoongArchState *env, arg_fldle_s *restrict a) {__NOT_IMPLEMENTED__}
static bool trans_fldle_d(CPULoongArchState *env, arg_fldle_d *restrict a) {__NOT_IMPLEMENTED__}
static bool trans_fstgt_s(CPULoongArchState *env, arg_fstgt_s *restrict a) {__NOT_IMPLEMENTED__}
static bool trans_fstgt_d(CPULoongArchState *env, arg_fstgt_d *restrict a) {__NOT_IMPLEMENTED__}
static bool trans_fstle_s(CPULoongArchState *env, arg_fstle_s *restrict a) {__NOT_IMPLEMENTED__}
static bool trans_fstle_d(CPULoongArchState *env, arg_fstle_d *restrict a) {__NOT_IMPLEMENTED__}
static bool trans_beqz(CPULoongArchState *env, arg_beqz *restrict a) {
    PERF_INC(COUNTER_INST_BRANCH);
    if ((int64_t)env->gpr[a->rj] == 0) {
        env->pc += a->offs;
    } else {
        env->pc += 4;
    }
    return true;
}
static bool trans_bnez(CPULoongArchState *env, arg_bnez *restrict a) {
    PERF_INC(COUNTER_INST_BRANCH);
    if ((int64_t)env->gpr[a->rj] != 0) {
        env->pc += a->offs;
    } else {
        env->pc += 4;
    }
    return true;
}
static bool trans_bceqz(CPULoongArchState *env, arg_bceqz *restrict a) {
    PERF_INC(COUNTER_INST_BRANCH);
    CHECK_FPE(8);
    if (env->cf[a->cj] == 0) {
        env->pc += a->offs;
    } else {
        env->pc += 4;
    }
    return true;
}
static bool trans_bcnez(CPULoongArchState *env, arg_bcnez *restrict a) {
    PERF_INC(COUNTER_INST_BRANCH);
    CHECK_FPE(8);
    if (env->cf[a->cj] != 0) {
        env->pc += a->offs;
    } else {
        env->pc += 4;
    }
    return true;
}
static bool trans_jirl(CPULoongArchState *env, arg_jirl *restrict a) {
    PERF_INC(COUNTER_INST_BRANCH);
    uint64_t old_pc = env->pc;
    env->pc = env->gpr[a->rj] + a->imm;
    env->gpr[a->rd] = old_pc + 4;
    return true;
}
static bool trans_b(CPULoongArchState *env, arg_b *restrict a) {
    PERF_INC(COUNTER_INST_BRANCH);
#ifndef CONFIG_DIFF
    if (!a->offs) {
        laemu_exit(EXIT_SUCCESS);
    }
#endif
    env->pc += a->offs;
    return true;
}
static bool trans_bl(CPULoongArchState *env, arg_bl *restrict a) {
    PERF_INC(COUNTER_INST_BRANCH);
    env->gpr[1] = env->pc + 4;
    env->pc += a->offs;
    return true;
}
static bool trans_beq(CPULoongArchState *env, arg_beq *restrict a) {
    PERF_INC(COUNTER_INST_BRANCH);
    if ((int64_t)env->gpr[a->rj] == (int64_t)env->gpr[a->rd]) {
        env->pc += a->offs;
    } else {
        env->pc += 4;
    }
    return true;
}
static bool trans_bne(CPULoongArchState *env, arg_bne *restrict a) {
    PERF_INC(COUNTER_INST_BRANCH);
    if ((int64_t)env->gpr[a->rj] != (int64_t)env->gpr[a->rd]) {
        env->pc += a->offs;
    } else {
        env->pc += 4;
    }
    return true;
}
static bool trans_blt(CPULoongArchState *env, arg_blt *restrict a) {
    PERF_INC(COUNTER_INST_BRANCH);
    if ((int64_t)env->gpr[a->rj] < (int64_t)env->gpr[a->rd]) {
        env->pc += a->offs;
    } else {
        env->pc += 4;
    }
    return true;
}
static bool trans_bge(CPULoongArchState *env, arg_bge *restrict a) {
    PERF_INC(COUNTER_INST_BRANCH);
    if ((int64_t)env->gpr[a->rj] >= (int64_t)env->gpr[a->rd]) {
        env->pc += a->offs;
    } else {
        env->pc += 4;
    }
    return true;
}
static bool trans_bltu(CPULoongArchState *env, arg_bltu *restrict a) {
    PERF_INC(COUNTER_INST_BRANCH);
    if ((uint64_t)env->gpr[a->rj] < (uint64_t)env->gpr[a->rd]) {
        env->pc += a->offs;
    } else {
        env->pc += 4;
    }
    return true;
}
static bool trans_bgeu(CPULoongArchState *env, arg_bgeu *restrict a) {
    PERF_INC(COUNTER_INST_BRANCH);
    if ((uint64_t)env->gpr[a->rj] >= (uint64_t)env->gpr[a->rd]) {
        env->pc += a->offs;
    } else {
        env->pc += 4;
    }
    return true;
}

#ifdef CONFIG_USER_ONLY
static bool trans_csrrd(CPULoongArchState *env, arg_csrrd *restrict a) {return false;}
static bool trans_csrwr(CPULoongArchState *env, arg_csrwr *restrict a) {return false;}
static bool trans_csrxchg(CPULoongArchState *env, arg_csrxchg *restrict a) {return false;}
static bool trans_iocsrrd_b(CPULoongArchState *env, arg_iocsrrd_b *restrict a) {return false;}
static bool trans_iocsrrd_h(CPULoongArchState *env, arg_iocsrrd_h *restrict a) {return false;}
static bool trans_iocsrrd_w(CPULoongArchState *env, arg_iocsrrd_w *restrict a) {return false;}
static bool trans_iocsrrd_d(CPULoongArchState *env, arg_iocsrrd_d *restrict a) {return false;}
static bool trans_iocsrwr_b(CPULoongArchState *env, arg_iocsrwr_b *restrict a) {return false;}
static bool trans_iocsrwr_h(CPULoongArchState *env, arg_iocsrwr_h *restrict a) {return false;}
static bool trans_iocsrwr_w(CPULoongArchState *env, arg_iocsrwr_w *restrict a) {return false;}
static bool trans_iocsrwr_d(CPULoongArchState *env, arg_iocsrwr_d *restrict a) {return false;}
static bool trans_tlbsrch(CPULoongArchState *env, arg_tlbsrch *restrict a) {return false;}
static bool trans_tlbrd(CPULoongArchState *env, arg_tlbrd *restrict a) {return false;}
static bool trans_tlbwr(CPULoongArchState *env, arg_tlbwr *restrict a) {return false;}
static bool trans_tlbfill(CPULoongArchState *env, arg_tlbfill *restrict a) {return false;}
static bool trans_tlbclr(CPULoongArchState *env, arg_tlbclr *restrict a) {return false;}
static bool trans_tlbflush(CPULoongArchState *env, arg_tlbflush *restrict a) {return false;}
static bool trans_invtlb(CPULoongArchState *env, arg_invtlb *restrict a) {return false;}
static bool trans_cacop(CPULoongArchState *env, arg_cacop *restrict a) {return false;}
static bool trans_ldpte(CPULoongArchState *env, arg_ldpte *restrict a) {return false;}
static bool trans_lddir(CPULoongArchState *env, arg_lddir *restrict a) {return false;}
static bool trans_ertn(CPULoongArchState *env, arg_ertn *restrict a) {return false;}
static bool trans_dbcl(CPULoongArchState *env, arg_dbcl *restrict a) {return false;}
static bool trans_idle(CPULoongArchState *env, arg_idle *restrict a) {return false;}

#else

uint64_t helper_read_csr(CPULoongArchState *env, int csr_index) {
    uint64_t old_v = 0;
    switch (csr_index) {
        case LOONGARCH_CSR_CRMD           :old_v = env->CSR_CRMD; break;
        case LOONGARCH_CSR_PRMD           :old_v = env->CSR_PRMD; break;
        case LOONGARCH_CSR_EUEN           :old_v = env->CSR_EUEN; break;
        case LOONGARCH_CSR_MISC           :old_v = env->CSR_MISC; break;
        case LOONGARCH_CSR_ECFG           :old_v = env->CSR_ECFG; break;
        case LOONGARCH_CSR_ESTAT          :old_v = env->CSR_ESTAT; break;
        case LOONGARCH_CSR_ERA            :old_v = env->CSR_ERA; break;
        case LOONGARCH_CSR_BADV           :old_v = env->CSR_BADV; break;
        case LOONGARCH_CSR_BADI           :old_v = env->CSR_BADI; break;
        case LOONGARCH_CSR_EENTRY         :old_v = env->CSR_EENTRY; break;
        case LOONGARCH_CSR_TLBIDX         :old_v = sextract64(env->CSR_TLBIDX, 0, 32); break;
        case LOONGARCH_CSR_TLBEHI         :old_v = sextract64(env->CSR_TLBEHI, 0, FIELD_EX64(env->cpucfg[1], CPUCFG1, VALEN) + 1); break;
        case LOONGARCH_CSR_TLBELO0        :old_v = env->CSR_TLBELO0; break;
        case LOONGARCH_CSR_TLBELO1        :old_v = env->CSR_TLBELO1; break;
        case LOONGARCH_CSR_ASID           :old_v = env->CSR_ASID; break;
        case LOONGARCH_CSR_PGDL           :old_v = env->CSR_PGDL; break;
        case LOONGARCH_CSR_PGDH           :old_v = env->CSR_PGDH; break;
        case LOONGARCH_CSR_PGD            :old_v = helper_csrrd_pgd(env); break;
        case LOONGARCH_CSR_PWCL           :old_v = sextract64(env->CSR_PWCL, 0, 32); break;
        case LOONGARCH_CSR_PWCH           :old_v = env->CSR_PWCH; break;
        case LOONGARCH_CSR_STLBPS         :old_v = env->CSR_STLBPS; break;
        case LOONGARCH_CSR_RVACFG         :old_v = env->CSR_RVACFG; break;
        case LOONGARCH_CSR_CPUID          :old_v = env->CSR_CPUID; break;
        case LOONGARCH_CSR_PRCFG1         :old_v = env->CSR_PRCFG1; break;
        case LOONGARCH_CSR_PRCFG2         :old_v = env->CSR_PRCFG2; break;
        case LOONGARCH_CSR_PRCFG3         :old_v = env->CSR_PRCFG3; break;
        case LOONGARCH_CSR_SAVE(0)        :old_v = env->CSR_SAVE[0]; break;
        case LOONGARCH_CSR_SAVE(1)        :old_v = env->CSR_SAVE[1]; break;
        case LOONGARCH_CSR_SAVE(2)        :old_v = env->CSR_SAVE[2]; break;
        case LOONGARCH_CSR_SAVE(3)        :old_v = env->CSR_SAVE[3]; break;
        case LOONGARCH_CSR_SAVE(4)        :old_v = env->CSR_SAVE[4]; break;
        case LOONGARCH_CSR_SAVE(5)        :old_v = env->CSR_SAVE[5]; break;
        case LOONGARCH_CSR_SAVE(6)        :old_v = env->CSR_SAVE[6]; break;
        case LOONGARCH_CSR_SAVE(7)        :old_v = env->CSR_SAVE[7]; break;
        case LOONGARCH_CSR_TID            :old_v = sextract64(env->CSR_TID, 0, 32); break;
        case LOONGARCH_CSR_TCFG           :old_v = env->CSR_TCFG; break;
        case LOONGARCH_CSR_TVAL           :old_v = env->timer_counter; break;
        case LOONGARCH_CSR_CNTC           :old_v = env->CSR_CNTC; break;
        case LOONGARCH_CSR_TICLR          :old_v = 0; break;
        case LOONGARCH_CSR_LLBCTL         :old_v = env->CSR_LLBCTL; break;
        case LOONGARCH_CSR_IMPCTL1        :old_v = env->CSR_IMPCTL1; break;
        case LOONGARCH_CSR_IMPCTL2        :old_v = env->CSR_IMPCTL2; break;
        case LOONGARCH_CSR_TLBRENTRY      :old_v = env->CSR_TLBRENTRY; break;
        case LOONGARCH_CSR_TLBRBADV       :old_v = env->CSR_TLBRBADV; break;
        case LOONGARCH_CSR_TLBRERA        :old_v = env->CSR_TLBRERA; break;
        case LOONGARCH_CSR_TLBRSAVE       :old_v = env->CSR_TLBRSAVE; break;
        case LOONGARCH_CSR_TLBRELO0       :old_v = env->CSR_TLBRELO0; break;
        case LOONGARCH_CSR_TLBRELO1       :old_v = env->CSR_TLBRELO1; break;
        case LOONGARCH_CSR_TLBREHI        :old_v = sextract64(env->CSR_TLBREHI, 0, FIELD_EX64(env->cpucfg[1], CPUCFG1, VALEN) + 1); break;
        case LOONGARCH_CSR_TLBRPRMD       :old_v = env->CSR_TLBRPRMD; break;
        case LOONGARCH_CSR_MERRCTL        :old_v = env->CSR_MERRCTL; break;
        case LOONGARCH_CSR_MERRINFO1      :old_v = env->CSR_MERRINFO1; break;
        case LOONGARCH_CSR_MERRINFO2      :old_v = env->CSR_MERRINFO2; break;
        case LOONGARCH_CSR_MERRENTRY      :old_v = env->CSR_MERRENTRY; break;
        case LOONGARCH_CSR_MERRERA        :old_v = env->CSR_MERRERA; break;
        case LOONGARCH_CSR_MERRSAVE       :old_v = env->CSR_MERRSAVE; break;
        case LOONGARCH_CSR_CTAG           :old_v = env->CSR_CTAG; break;
        case LOONGARCH_CSR_DMW(0)         :old_v = env->CSR_DMW[0]; break;
        case LOONGARCH_CSR_DMW(1)         :old_v = env->CSR_DMW[1]; break;
        case LOONGARCH_CSR_DMW(2)         :old_v = env->CSR_DMW[2]; break;
        case LOONGARCH_CSR_DMW(3)         :old_v = env->CSR_DMW[3]; break;
        case LOONGARCH_CSR_DBG            :old_v = env->CSR_DBG; break;
        case LOONGARCH_CSR_DERA           :old_v = env->CSR_DERA; break;
        case LOONGARCH_CSR_DSAVE          :old_v = env->CSR_DSAVE; break;
        default:
            fprintf(stderr, "NOT IMPLEMENTED %s %x\n", __func__, csr_index);
    }
    return old_v;
}

static bool trans_csrrd(CPULoongArchState *env, arg_csrrd *restrict a) {
    env->gpr[a->rd] = helper_read_csr(env, a->csr);
    switch (a->csr)
    {
    case LOONGARCH_CSR_TID:    qemu_log_mask(CPU_LOG_TIMER, "%s pc:%lx CSR_TID:%lx\n", __func__, env->pc, env->CSR_TID); break;
    case LOONGARCH_CSR_TCFG:   qemu_log_mask(CPU_LOG_TIMER, "%s pc:%lx CSR_TCFG:%lx\n", __func__, env->pc, env->CSR_TCFG); break;
    case LOONGARCH_CSR_TVAL:   qemu_log_mask(CPU_LOG_TIMER, "%s pc:%lx CSR_TVAL:%lx\n", __func__, env->pc, env->CSR_TVAL); break;
    case LOONGARCH_CSR_CNTC:   qemu_log_mask(CPU_LOG_TIMER, "%s pc:%lx CSR_CNTC:%lx\n", __func__, env->pc, env->CSR_CNTC); break;
    case LOONGARCH_CSR_TICLR:  qemu_log_mask(CPU_LOG_TIMER, "%s pc:%lx CSR_TICLR:%lx\n", __func__, env->pc, env->CSR_TICLR); break;
    default:
        break;
    }
    env->pc += 4;
    return true;
}

uint64_t mask_write(uint64_t old, uint64_t new, uint64_t mask) {
    return (old & ~mask) | (new & mask);
}

uint64_t helper_write_csr(CPULoongArchState *env, int csr_index, uint64_t new_v, uint64_t mask) {
    uint64_t old_v = 0;
    switch (csr_index) {
        case LOONGARCH_CSR_CRMD           :old_v = env->CSR_CRMD; env->CSR_CRMD = mask_write(env->CSR_CRMD, new_v, mask & LOONGARCH_CSR_CRMD_WMASK); break;
        case LOONGARCH_CSR_PRMD           :old_v = env->CSR_PRMD; env->CSR_PRMD = mask_write(env->CSR_PRMD, new_v, mask & LOONGARCH_CSR_PRMD_WMASK); break;
        case LOONGARCH_CSR_EUEN           :old_v = env->CSR_EUEN; env->CSR_EUEN = mask_write(env->CSR_EUEN, new_v, mask & LOONGARCH_CSR_EUEN_WMASK); break;
        case LOONGARCH_CSR_MISC           :old_v = env->CSR_MISC; env->CSR_MISC = mask_write(env->CSR_MISC, new_v, mask & LOONGARCH_CSR_MISC_WMASK); break;
        case LOONGARCH_CSR_ECFG           :old_v = env->CSR_ECFG; env->CSR_ECFG = mask_write(env->CSR_ECFG, new_v, mask & LOONGARCH_CSR_ECFG_WMASK); break;
        case LOONGARCH_CSR_ESTAT          :old_v = env->CSR_ESTAT; env->CSR_ESTAT = mask_write(env->CSR_ESTAT, new_v, mask & LOONGARCH_CSR_ESTAT_WMASK); break;
        case LOONGARCH_CSR_ERA            :old_v = env->CSR_ERA; env->CSR_ERA = mask_write(env->CSR_ERA, new_v, mask); break;
        case LOONGARCH_CSR_BADV           :old_v = env->CSR_BADV; env->CSR_BADV = mask_write(env->CSR_BADV, new_v, mask); break;
        case LOONGARCH_CSR_BADI           :old_v = env->CSR_BADI; break;
        case LOONGARCH_CSR_EENTRY         :old_v = env->CSR_EENTRY; env->CSR_EENTRY = mask_write(env->CSR_EENTRY, new_v, mask & LOONGARCH_CSR_EENTRY_WMASK); break;
        case LOONGARCH_CSR_TLBIDX         :old_v = sextract64(env->CSR_TLBIDX, 0, 32); env->CSR_TLBIDX = mask_write(env->CSR_TLBIDX, new_v, mask & LOONGARCH_CSR_TLBIDX_WMASK); break;
        case LOONGARCH_CSR_TLBEHI         :old_v = sextract64(env->CSR_TLBEHI, 0, FIELD_EX64(env->cpucfg[1], CPUCFG1, VALEN) + 1); env->CSR_TLBEHI = mask_write(env->CSR_TLBEHI, new_v, mask & LOONGARCH_CSR_TLBEHI_64_WMASK); break;
        case LOONGARCH_CSR_TLBELO0        :old_v = env->CSR_TLBELO0; env->CSR_TLBELO0 = mask_write(env->CSR_TLBELO0, new_v, mask & LOONGARCH_CSR_TLBELO_64_WMASK); break;
        case LOONGARCH_CSR_TLBELO1        :old_v = env->CSR_TLBELO1; env->CSR_TLBELO1 = mask_write(env->CSR_TLBELO1, new_v, mask & LOONGARCH_CSR_TLBELO_64_WMASK); break;
        case LOONGARCH_CSR_ASID           :old_v = env->CSR_ASID; env->CSR_ASID = mask_write(env->CSR_ASID, new_v, mask & LOONGARCH_CSR_ASID_WMASK); cpu_clear_tc(env); break;
        case LOONGARCH_CSR_PGDL           :old_v = env->CSR_PGDL; env->CSR_PGDL = mask_write(env->CSR_PGDL, new_v, mask & LOONGARCH_CSR_PGDL_WMASK); break;
        case LOONGARCH_CSR_PGDH           :old_v = env->CSR_PGDH; env->CSR_PGDH = mask_write(env->CSR_PGDH, new_v, mask & LOONGARCH_CSR_PGDH_WMASK); break;
        case LOONGARCH_CSR_PGD            :old_v = helper_csrrd_pgd(env); break;
        case LOONGARCH_CSR_PWCL           :old_v = sextract64(env->CSR_PWCL, 0, 32); env->CSR_PWCL = mask_write(env->CSR_PWCL, new_v, mask & LOONGARCH_CSR_PWCL_WMASK); break;
        case LOONGARCH_CSR_PWCH           :old_v = env->CSR_PWCH; env->CSR_PWCH = mask_write(env->CSR_PWCH, new_v, mask & LOONGARCH_CSR_PWCH_WMASK); break;
        case LOONGARCH_CSR_STLBPS         :old_v = env->CSR_STLBPS; env->CSR_STLBPS = mask_write(env->CSR_STLBPS, new_v, mask & LOONGARCH_CSR_STLBPS_WMASK); cpu_clear_tc(env); break;
        case LOONGARCH_CSR_RVACFG         :old_v = env->CSR_RVACFG; env->CSR_RVACFG = mask_write(env->CSR_RVACFG, new_v, mask & LOONGARCH_CSR_RVACFG_WMASK); break;
        case LOONGARCH_CSR_CPUID          :old_v = env->CSR_CPUID; break;
        case LOONGARCH_CSR_PRCFG1         :old_v = env->CSR_PRCFG1; break;
        case LOONGARCH_CSR_PRCFG2         :old_v = env->CSR_PRCFG2; break;
        case LOONGARCH_CSR_PRCFG3         :old_v = env->CSR_PRCFG3; break;
        case LOONGARCH_CSR_SAVE(0)        :old_v = env->CSR_SAVE[0]; env->CSR_SAVE[0] = mask_write(env->CSR_SAVE[0], new_v, mask); break;
        case LOONGARCH_CSR_SAVE(1)        :old_v = env->CSR_SAVE[1]; env->CSR_SAVE[1] = mask_write(env->CSR_SAVE[1], new_v, mask); break;
        case LOONGARCH_CSR_SAVE(2)        :old_v = env->CSR_SAVE[2]; env->CSR_SAVE[2] = mask_write(env->CSR_SAVE[2], new_v, mask); break;
        case LOONGARCH_CSR_SAVE(3)        :old_v = env->CSR_SAVE[3]; env->CSR_SAVE[3] = mask_write(env->CSR_SAVE[3], new_v, mask); break;
        case LOONGARCH_CSR_SAVE(4)        :old_v = env->CSR_SAVE[4]; env->CSR_SAVE[4] = mask_write(env->CSR_SAVE[4], new_v, mask); break;
        case LOONGARCH_CSR_SAVE(5)        :old_v = env->CSR_SAVE[5]; env->CSR_SAVE[5] = mask_write(env->CSR_SAVE[5], new_v, mask); break;
        case LOONGARCH_CSR_SAVE(6)        :old_v = env->CSR_SAVE[6]; env->CSR_SAVE[6] = mask_write(env->CSR_SAVE[6], new_v, mask); break;
        case LOONGARCH_CSR_SAVE(7)        :old_v = env->CSR_SAVE[7]; env->CSR_SAVE[7] = mask_write(env->CSR_SAVE[7], new_v, mask); break;
        case LOONGARCH_CSR_TID            :old_v = sextract64(env->CSR_TID, 0, 32); env->CSR_TID = mask_write(env->CSR_TID, new_v, mask & LOONGARCH_CSR_TID_WMASK); break;
        case LOONGARCH_CSR_TCFG           :old_v = env->CSR_TCFG; env->CSR_TCFG = mask_write(env->CSR_TCFG, new_v, mask);
#ifndef CONFIG_DIFF
            if (env->CSR_TCFG & 1) {
                if (determined) {
                    env->timer_counter = (env->CSR_TCFG & CONSTANT_TIMER_TICK_MASK) / TIME_SCALE;
                } else {
                    cpu_settimer(env, env->CSR_TCFG & CONSTANT_TIMER_TICK_MASK);
                }
            } else {
                if (determined) {
                    env->timer_counter = -1;
                } else {
                    cpu_disable_timer(env);
                }
            }
#endif
            break;
        case LOONGARCH_CSR_TVAL           :old_v = env->CSR_TVAL; break;
        case LOONGARCH_CSR_CNTC           :old_v = env->CSR_CNTC; env->CSR_CNTC = mask_write(env->CSR_CNTC, new_v, mask); break;
        case LOONGARCH_CSR_TICLR          :old_v = 0;
            if (new_v & mask & 1) {
                env->timer_int = 0;
                loongarch_cpu_set_irq(env, IRQ_TIMER, 0);
            }
        break;
        case LOONGARCH_CSR_LLBCTL         :old_v = env->CSR_LLBCTL;
            if (new_v & mask & R_CSR_LLBCTL_WCLLB_MASK) {
                env->CSR_LLBCTL = FIELD_DP64(env->CSR_LLBCTL, CSR_LLBCTL, ROLLB, 0);
            }
            if (mask & R_CSR_LLBCTL_KLO_MASK) {
                env->CSR_LLBCTL = FIELD_DP64(env->CSR_LLBCTL, CSR_LLBCTL, KLO, (new_v & R_CSR_LLBCTL_KLO_MASK) >> R_CSR_LLBCTL_KLO_SHIFT);
            }
        break;
        case LOONGARCH_CSR_IMPCTL1        :old_v = env->CSR_IMPCTL1; env->CSR_IMPCTL1 = mask_write(env->CSR_IMPCTL1, new_v, mask); break;
        case LOONGARCH_CSR_IMPCTL2        :old_v = env->CSR_IMPCTL2; env->CSR_IMPCTL2 = mask_write(env->CSR_IMPCTL2, new_v, mask); break;
        case LOONGARCH_CSR_TLBRENTRY      :old_v = env->CSR_TLBRENTRY; env->CSR_TLBRENTRY = mask_write(env->CSR_TLBRENTRY, new_v, mask & LOONGARCH_CSR_TLBRENTRY_64_WMASK); break;
        case LOONGARCH_CSR_TLBRBADV       :old_v = env->CSR_TLBRBADV; env->CSR_TLBRBADV = mask_write(env->CSR_TLBRBADV, new_v, mask); break;
        case LOONGARCH_CSR_TLBRERA        :old_v = env->CSR_TLBRERA; env->CSR_TLBRERA = mask_write(env->CSR_TLBRERA, new_v, mask & LOONGARCH_CSR_TLBRERA_WMASK); break;
        case LOONGARCH_CSR_TLBRSAVE       :old_v = env->CSR_TLBRSAVE; env->CSR_TLBRSAVE = mask_write(env->CSR_TLBRSAVE, new_v, mask); break;
        case LOONGARCH_CSR_TLBRELO0       :old_v = env->CSR_TLBRELO0; env->CSR_TLBRELO0 = mask_write(env->CSR_TLBRELO0, new_v, mask & LOONGARCH_CSR_TLBRELO_64_WMASK); break;
        case LOONGARCH_CSR_TLBRELO1       :old_v = env->CSR_TLBRELO1; env->CSR_TLBRELO1 = mask_write(env->CSR_TLBRELO1, new_v, mask & LOONGARCH_CSR_TLBRELO_64_WMASK); break;
        case LOONGARCH_CSR_TLBREHI        :old_v = sextract64(env->CSR_TLBREHI, 0, FIELD_EX64(env->cpucfg[1], CPUCFG1, VALEN) + 1); env->CSR_TLBREHI = mask_write(env->CSR_TLBREHI, new_v, mask & LOONGARCH_CSR_TLBREHI_64_WMASK); break;
        case LOONGARCH_CSR_TLBRPRMD       :old_v = env->CSR_TLBRPRMD; env->CSR_TLBRPRMD = mask_write(env->CSR_TLBRPRMD, new_v, mask & LOONGARCH_CSR_TLBRPRMD_WMASK); break;
        case LOONGARCH_CSR_MERRCTL        :old_v = env->CSR_MERRCTL; env->CSR_MERRCTL = mask_write(env->CSR_MERRCTL, new_v, mask); break;
        case LOONGARCH_CSR_MERRINFO1      :old_v = env->CSR_MERRINFO1; env->CSR_MERRINFO1 = mask_write(env->CSR_MERRINFO1, new_v, mask); break;
        case LOONGARCH_CSR_MERRINFO2      :old_v = env->CSR_MERRINFO2; env->CSR_MERRINFO2 = mask_write(env->CSR_MERRINFO2, new_v, mask); break;
        case LOONGARCH_CSR_MERRENTRY      :old_v = env->CSR_MERRENTRY; env->CSR_MERRENTRY = mask_write(env->CSR_MERRENTRY, new_v, mask); break;
        case LOONGARCH_CSR_MERRERA        :old_v = env->CSR_MERRERA; env->CSR_MERRERA = mask_write(env->CSR_MERRERA, new_v, mask); break;
        case LOONGARCH_CSR_MERRSAVE       :old_v = env->CSR_MERRSAVE; env->CSR_MERRSAVE = mask_write(env->CSR_MERRSAVE, new_v, mask); break;
        case LOONGARCH_CSR_CTAG           :old_v = env->CSR_CTAG; env->CSR_CTAG = mask_write(env->CSR_CTAG, new_v, mask); break;
        case LOONGARCH_CSR_DMW(0)         :old_v = env->CSR_DMW[0]; env->CSR_DMW[0] = mask_write(env->CSR_DMW[0], new_v, mask & LOONGARCH_CSR_DMW_64_WMASK); cpu_clear_tc(env); break;
        case LOONGARCH_CSR_DMW(1)         :old_v = env->CSR_DMW[1]; env->CSR_DMW[1] = mask_write(env->CSR_DMW[1], new_v, mask & LOONGARCH_CSR_DMW_64_WMASK); cpu_clear_tc(env); break;
        case LOONGARCH_CSR_DMW(2)         :old_v = env->CSR_DMW[2]; env->CSR_DMW[2] = mask_write(env->CSR_DMW[2], new_v, mask & LOONGARCH_CSR_DMW_64_WMASK); cpu_clear_tc(env); break;
        case LOONGARCH_CSR_DMW(3)         :old_v = env->CSR_DMW[3]; env->CSR_DMW[3] = mask_write(env->CSR_DMW[3], new_v, mask & LOONGARCH_CSR_DMW_64_WMASK); cpu_clear_tc(env); break;
        case LOONGARCH_CSR_DBG            :old_v = env->CSR_DBG; break;
        case LOONGARCH_CSR_DERA           :old_v = env->CSR_DERA; env->CSR_DERA = mask_write(env->CSR_DERA, new_v, mask); break;
        case LOONGARCH_CSR_DSAVE          :old_v = env->CSR_DSAVE; env->CSR_DSAVE = mask_write(env->CSR_DSAVE, new_v, mask); break;
        default:
            fprintf(stderr, "NOT IMPLEMENTED %s %x\n", __func__, csr_index);
    }
    switch (csr_index)
    {
    case LOONGARCH_CSR_TID:    qemu_log_mask(CPU_LOG_TIMER, "%s pc:%lx CSR_TID:   %016lx old:%016lx new:%016lx mask:%016lx\n", __func__, env->pc, env->CSR_TID, old_v, new_v, mask); break;
    case LOONGARCH_CSR_TCFG:   qemu_log_mask(CPU_LOG_TIMER, "%s pc:%lx CSR_TCFG:  %016lx old:%016lx new:%016lx mask:%016lx\n", __func__, env->pc, env->CSR_TCFG, old_v, new_v, mask); break;
    case LOONGARCH_CSR_TVAL:   qemu_log_mask(CPU_LOG_TIMER, "%s pc:%lx CSR_TVAL:  %016lx old:%016lx new:%016lx mask:%016lx\n", __func__, env->pc, env->CSR_TVAL, old_v, new_v, mask); break;
    case LOONGARCH_CSR_CNTC:   qemu_log_mask(CPU_LOG_TIMER, "%s pc:%lx CSR_CNTC:  %016lx old:%016lx new:%016lx mask:%016lx\n", __func__, env->pc, env->CSR_CNTC, old_v, new_v, mask); break;
    case LOONGARCH_CSR_TICLR:  qemu_log_mask(CPU_LOG_TIMER, "%s pc:%lx CSR_TICLR: %016lx old:%016lx new:%016lx mask:%016lx\n", __func__, env->pc, env->CSR_TICLR, old_v, new_v, mask); break;
    default:
        break;
    }
    return old_v;
}

static bool trans_csrwr(CPULoongArchState *env, arg_csrwr *restrict a) {
    env->gpr[a->rd] = helper_write_csr(env, a->csr, env->gpr[a->rd], -1);
    env->pc += 4;
    return true;
}
static bool trans_csrxchg(CPULoongArchState *env, arg_csrxchg *restrict a) {
    env->gpr[a->rd] = helper_write_csr(env, a->csr, env->gpr[a->rd], env->gpr[a->rj]);
    env->pc += 4;
    return true;
}
static bool trans_iocsrrd_b(CPULoongArchState *env, arg_iocsrrd_b *restrict a) {
    fprintf(stderr, "NOT IMPLEMENTED %s pc:%lx addr:%lx\n", __func__, env->pc, env->gpr[a->rj]);
    env->pc += 4;
    return true;
}
static bool trans_iocsrrd_h(CPULoongArchState *env, arg_iocsrrd_h *restrict a) {
    fprintf(stderr, "NOT IMPLEMENTED %s pc:%lx addr:%lx\n", __func__, env->pc, env->gpr[a->rj]);
    a->rd = 0;
    env->pc += 4;
    return true;
}
static bool trans_iocsrrd_w(CPULoongArchState *env, arg_iocsrrd_w *restrict a) {
    fprintf(stderr, "NOT IMPLEMENTED %s pc:%lx addr:%lx\n", __func__, env->pc, env->gpr[a->rj]);
    a->rd = 0;
    env->pc += 4;
    return true;
}
static bool trans_iocsrrd_d(CPULoongArchState *env, arg_iocsrrd_d *restrict a) {
    fprintf(stderr, "NOT IMPLEMENTED %s pc:%lx addr:%lx\n", __func__, env->pc, env->gpr[a->rj]);
    a->rd = 0;
    env->pc += 4;
    return true;
}
static bool trans_iocsrwr_b(CPULoongArchState *env, arg_iocsrwr_b *restrict a) {
    fprintf(stderr, "NOT IMPLEMENTED %s pc:%lx addr:%lx\n", __func__, env->pc, env->gpr[a->rj]);
    a->rd = 0;
    env->pc += 4;
    return true;
}
static bool trans_iocsrwr_h(CPULoongArchState *env, arg_iocsrwr_h *restrict a) {
    fprintf(stderr, "NOT IMPLEMENTED %s pc:%lx addr:%lx\n", __func__, env->pc, env->gpr[a->rj]);
    env->pc += 4;
    return true;
}
static bool trans_iocsrwr_w(CPULoongArchState *env, arg_iocsrwr_w *restrict a) {
    fprintf(stderr, "NOT IMPLEMENTED %s pc:%lx addr:%lx\n", __func__, env->pc, env->gpr[a->rj]);
    env->pc += 4;
    return true;
}
static bool trans_iocsrwr_d(CPULoongArchState *env, arg_iocsrwr_d *restrict a) {
    fprintf(stderr, "NOT IMPLEMENTED %s pc:%lx addr:%lx\n", __func__, env->pc, env->gpr[a->rj]);
    env->pc += 4;
    return true;
}
static bool trans_tlbsrch(CPULoongArchState *env, arg_tlbsrch *restrict a) {
    helper_tlbsrch(env);
    env->pc += 4;
    return true;
}
static bool trans_tlbrd(CPULoongArchState *env, arg_tlbrd *restrict a) {
    helper_tlbrd(env);
    env->pc += 4;
    return true;
}
static bool trans_tlbwr(CPULoongArchState *env, arg_tlbwr *restrict a) {
    helper_tlbwr(env);
    cpu_clear_tc(env);
    env->pc += 4;
    return true;
}
static bool trans_tlbfill(CPULoongArchState *env, arg_tlbfill *restrict a) {
    helper_tlbfill(env);
    env->pc += 4;
    return true;
}
static bool trans_tlbclr(CPULoongArchState *env, arg_tlbclr *restrict a) {__NOT_IMPLEMENTED__}
static bool trans_tlbflush(CPULoongArchState *env, arg_tlbflush *restrict a) {__NOT_IMPLEMENTED__}
static bool trans_invtlb(CPULoongArchState *env, arg_invtlb *restrict a) {
    helper_invtlb_all(env);
    cpu_clear_tc(env);
    env->pc += 4;
    return true;
}
static bool trans_cacop(CPULoongArchState *env, arg_cacop *restrict a) {
    env->pc += 4;
    return true;
}
static bool trans_lddir(CPULoongArchState *env, arg_lddir *restrict a) {
    uint64_t dir_phys_addr;
    env->gpr[a->rd] = helper_lddir(env, env->gpr[a->rj], a->imm, 0, &dir_phys_addr);
    env->pc += 4;
    return true;
}
static bool trans_ldpte(CPULoongArchState *env, arg_ldpte *restrict a) {
    uint64_t pte_phys_addr;
    helper_ldpte(env, env->gpr[a->rj], a->imm, 0, &pte_phys_addr);
    env->pc += 4;
    return true;
}
static bool trans_ertn(CPULoongArchState *env, arg_ertn *restrict a) {
    helper_ertn(env);
    cpu_clear_tc(env);
    return true;
}
static bool trans_idle(CPULoongArchState *env, arg_idle *restrict a) {
#ifndef CONFIG_DIFF
    if (FIELD_EX64(env->CSR_CRMD, CSR_CRMD, IE) == 0) {
        fprintf(stderr, "idle while CRMD.IE is disabled\n");
        laemu_exit(0);
    }
    // fprintf(stderr, "NOT CORRECTED IMPLEMENTED %s, pc:%lx\n", __func__, env->pc);
    if (!determined) {
        while (loongarch_cpu_check_irq(env), !loongarch_cpu_has_irq(env)) {
            sleep(1);
        }
    }
#endif
    env->pc += 4;
    return true;
}
static bool trans_dbcl(CPULoongArchState *env, arg_dbcl *restrict a) {__NOT_IMPLEMENTED__}
#endif

#define gen_trans_vvid(op, size, helper_name) \
static bool glue(trans_, op)(CPULoongArchState *env, arg_vv_i *restrict a) {      \
    CHECK_FPE(size);                                                    \
    int oprsz = size;                                                   \
    uint32_t desc = simd_desc(oprsz, oprsz, 0);                         \
    glue(helper_, helper_name)(&env->fpr[a->vd], &env->fpr[a->vj], a->imm, desc);   \
    env->pc += 4;                                                       \
    return true;                                                        \
}
#define gen_trans_vvvd(op, size, helper_name) \
static bool glue(trans_, op)(CPULoongArchState *env, arg_vvv *restrict a) {   \
    CHECK_FPE(size);                                                    \
    int oprsz = size;                                                   \
    uint32_t desc = simd_desc(oprsz, oprsz, 0);                         \
    glue(helper_, helper_name)(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], desc);          \
    env->pc += 4;                                                       \
    return true;                                                        \
}
#define gen_trans_vvvvd(op, size, helper_name) \
static bool glue(trans_, op)(CPULoongArchState *env, arg_vvvv *restrict a) {   \
    CHECK_FPE(size);                                                    \
    int oprsz = size;                                                   \
    uint32_t desc = simd_desc(oprsz, oprsz, 0);                         \
    glue(helper_, helper_name)(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], &env->fpr[a->va], desc);          \
    env->pc += 4;                                                       \
    return true;                                                        \
}
static inline bool vadd_b(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.B[i] = env->fpr[a->vj].vreg.B[i] + env->fpr[a->vk].vreg.B[i];}env->pc += 4;return true;}
static inline bool vadd_h(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.H[i] = env->fpr[a->vj].vreg.H[i] + env->fpr[a->vk].vreg.H[i];}env->pc += 4;return true;}
static inline bool vadd_w(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.W[i] = env->fpr[a->vj].vreg.W[i] + env->fpr[a->vk].vreg.W[i];}env->pc += 4;return true;}
static inline bool vadd_d(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = env->fpr[a->vj].vreg.D[i] + env->fpr[a->vk].vreg.D[i];}env->pc += 4;return true;}
static bool trans_vadd_b(CPULoongArchState *env, arg_vadd_b *restrict a) {CHECK_FPE(16); return vadd_b(env, a, 16);}
static bool trans_vadd_h(CPULoongArchState *env, arg_vadd_h *restrict a) {CHECK_FPE(16); return vadd_h(env, a, 16);}
static bool trans_vadd_w(CPULoongArchState *env, arg_vadd_w *restrict a) {CHECK_FPE(16); return vadd_w(env, a, 16);}
static bool trans_vadd_d(CPULoongArchState *env, arg_vadd_d *restrict a) {CHECK_FPE(16); return vadd_d(env, a, 16);}
static bool trans_xvadd_b(CPULoongArchState *env, arg_vadd_b *restrict a) {CHECK_FPE(32); return vadd_b(env, a, 32);}
static bool trans_xvadd_h(CPULoongArchState *env, arg_vadd_h *restrict a) {CHECK_FPE(32); return vadd_h(env, a, 32);}
static bool trans_xvadd_w(CPULoongArchState *env, arg_vadd_w *restrict a) {CHECK_FPE(32); return vadd_w(env, a, 32);}
static bool trans_xvadd_d(CPULoongArchState *env, arg_vadd_d *restrict a) {CHECK_FPE(32); return vadd_d(env, a, 32);}
static bool trans_vadd_q(CPULoongArchState *env, arg_vadd_q *restrict a) {
    CHECK_FPE(16);
    env->fpr[a->vd].vreg.Q[0] = env->fpr[a->vj].vreg.Q[0] + env->fpr[a->vk].vreg.Q[0];
    env->pc += 4;
    return true;
}
static inline bool vsub_b(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.B[i] = env->fpr[a->vj].vreg.B[i] - env->fpr[a->vk].vreg.B[i];}env->pc += 4;return true;}
static inline bool vsub_h(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.H[i] = env->fpr[a->vj].vreg.H[i] - env->fpr[a->vk].vreg.H[i];}env->pc += 4;return true;}
static inline bool vsub_w(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.W[i] = env->fpr[a->vj].vreg.W[i] - env->fpr[a->vk].vreg.W[i];}env->pc += 4;return true;}
static inline bool vsub_d(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = env->fpr[a->vj].vreg.D[i] - env->fpr[a->vk].vreg.D[i];}env->pc += 4;return true;}
static bool trans_vsub_b(CPULoongArchState *env, arg_vsub_b *restrict a) {CHECK_FPE(16); return vsub_b(env, a, 16);}
static bool trans_vsub_h(CPULoongArchState *env, arg_vsub_h *restrict a) {CHECK_FPE(16); return vsub_h(env, a, 16);}
static bool trans_vsub_w(CPULoongArchState *env, arg_vsub_w *restrict a) {CHECK_FPE(16); return vsub_w(env, a, 16);}
static bool trans_vsub_d(CPULoongArchState *env, arg_vsub_d *restrict a) {CHECK_FPE(16); return vsub_d(env, a, 16);}
static bool trans_xvsub_b(CPULoongArchState *env, arg_vsub_b *restrict a) {CHECK_FPE(32); return vsub_b(env, a, 32);}
static bool trans_xvsub_h(CPULoongArchState *env, arg_vsub_h *restrict a) {CHECK_FPE(32); return vsub_h(env, a, 32);}
static bool trans_xvsub_w(CPULoongArchState *env, arg_vsub_w *restrict a) {CHECK_FPE(32); return vsub_w(env, a, 32);}
static bool trans_xvsub_d(CPULoongArchState *env, arg_vsub_d *restrict a) {CHECK_FPE(32); return vsub_d(env, a, 32);}
static bool trans_vsub_q(CPULoongArchState *env, arg_vsub_q *restrict a) {
    CHECK_FPE(16);
    env->fpr[a->vd].vreg.Q[0] = env->fpr[a->vj].vreg.Q[0] - env->fpr[a->vk].vreg.Q[0];
    env->pc += 4;
    return true;
}
static inline bool vaddi_bu(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.B[i] = env->fpr[a->vj].vreg.B[i] + a->imm;}env->pc += 4;return true;}
static inline bool vaddi_hu(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.H[i] = env->fpr[a->vj].vreg.H[i] + a->imm;}env->pc += 4;return true;}
static inline bool vaddi_wu(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.W[i] = env->fpr[a->vj].vreg.W[i] + a->imm;}env->pc += 4;return true;}
static inline bool vaddi_du(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = env->fpr[a->vj].vreg.D[i] + a->imm;}env->pc += 4;return true;}
static bool trans_vaddi_bu(CPULoongArchState *env, arg_vaddi_bu *restrict a) {CHECK_FPE(16); return vaddi_bu(env, a, 16);}
static bool trans_vaddi_hu(CPULoongArchState *env, arg_vaddi_hu *restrict a) {CHECK_FPE(16); return vaddi_hu(env, a, 16);}
static bool trans_vaddi_wu(CPULoongArchState *env, arg_vaddi_wu *restrict a) {CHECK_FPE(16); return vaddi_wu(env, a, 16);}
static bool trans_vaddi_du(CPULoongArchState *env, arg_vaddi_du *restrict a) {CHECK_FPE(16); return vaddi_du(env, a, 16);}
static bool trans_xvaddi_bu(CPULoongArchState *env, arg_vaddi_bu *restrict a) {CHECK_FPE(32); return vaddi_bu(env, a, 32);}
static bool trans_xvaddi_hu(CPULoongArchState *env, arg_vaddi_hu *restrict a) {CHECK_FPE(32); return vaddi_hu(env, a, 32);}
static bool trans_xvaddi_wu(CPULoongArchState *env, arg_vaddi_wu *restrict a) {CHECK_FPE(32); return vaddi_wu(env, a, 32);}
static bool trans_xvaddi_du(CPULoongArchState *env, arg_vaddi_du *restrict a) {CHECK_FPE(32); return vaddi_du(env, a, 32);}
static inline bool vsubi_bu(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.B[i] = env->fpr[a->vj].vreg.B[i] - a->imm;}env->pc += 4;return true;}
static inline bool vsubi_hu(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.H[i] = env->fpr[a->vj].vreg.H[i] - a->imm;}env->pc += 4;return true;}
static inline bool vsubi_wu(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.W[i] = env->fpr[a->vj].vreg.W[i] - a->imm;}env->pc += 4;return true;}
static inline bool vsubi_du(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = env->fpr[a->vj].vreg.D[i] - a->imm;}env->pc += 4;return true;}
static bool trans_vsubi_bu(CPULoongArchState *env, arg_vsubi_bu *restrict a) {CHECK_FPE(16); return vsubi_bu(env, a, 16);}
static bool trans_vsubi_hu(CPULoongArchState *env, arg_vsubi_hu *restrict a) {CHECK_FPE(16); return vsubi_hu(env, a, 16);}
static bool trans_vsubi_wu(CPULoongArchState *env, arg_vsubi_wu *restrict a) {CHECK_FPE(16); return vsubi_wu(env, a, 16);}
static bool trans_vsubi_du(CPULoongArchState *env, arg_vsubi_du *restrict a) {CHECK_FPE(16); return vsubi_du(env, a, 16);}
static bool trans_xvsubi_bu(CPULoongArchState *env, arg_vsubi_bu *restrict a) {CHECK_FPE(32); return vsubi_bu(env, a, 32);}
static bool trans_xvsubi_hu(CPULoongArchState *env, arg_vsubi_hu *restrict a) {CHECK_FPE(32); return vsubi_hu(env, a, 32);}
static bool trans_xvsubi_wu(CPULoongArchState *env, arg_vsubi_wu *restrict a) {CHECK_FPE(32); return vsubi_wu(env, a, 32);}
static bool trans_xvsubi_du(CPULoongArchState *env, arg_vsubi_du *restrict a) {CHECK_FPE(32); return vsubi_du(env, a, 32);}
static inline bool vneg_b(CPULoongArchState *env, arg_vv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.B[i] = - env->fpr[a->vj].vreg.B[i];}env->pc += 4;return true;}
static inline bool vneg_h(CPULoongArchState *env, arg_vv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.H[i] = - env->fpr[a->vj].vreg.H[i];}env->pc += 4;return true;}
static inline bool vneg_w(CPULoongArchState *env, arg_vv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.W[i] = - env->fpr[a->vj].vreg.W[i];}env->pc += 4;return true;}
static inline bool vneg_d(CPULoongArchState *env, arg_vv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = - env->fpr[a->vj].vreg.D[i];}env->pc += 4;return true;}
static bool trans_vneg_b(CPULoongArchState *env, arg_vneg_b *restrict a) {CHECK_FPE(16); return vneg_b(env, a, 16);}
static bool trans_vneg_h(CPULoongArchState *env, arg_vneg_h *restrict a) {CHECK_FPE(16); return vneg_h(env, a, 16);}
static bool trans_vneg_w(CPULoongArchState *env, arg_vneg_w *restrict a) {CHECK_FPE(16); return vneg_w(env, a, 16);}
static bool trans_vneg_d(CPULoongArchState *env, arg_vneg_d *restrict a) {CHECK_FPE(16); return vneg_d(env, a, 16);}
static bool trans_xvneg_b(CPULoongArchState *env, arg_vneg_b *restrict a) {CHECK_FPE(32); return vneg_b(env, a, 32);}
static bool trans_xvneg_h(CPULoongArchState *env, arg_vneg_h *restrict a) {CHECK_FPE(32); return vneg_h(env, a, 32);}
static bool trans_xvneg_w(CPULoongArchState *env, arg_vneg_w *restrict a) {CHECK_FPE(32); return vneg_w(env, a, 32);}
static bool trans_xvneg_d(CPULoongArchState *env, arg_vneg_d *restrict a) {CHECK_FPE(32); return vneg_d(env, a, 32);}
gen_trans_vvvd(vsadd_b, 16, gvec_ssadd8)
gen_trans_vvvd(vsadd_h, 16, gvec_ssadd16)
gen_trans_vvvd(vsadd_w, 16, gvec_ssadd32)
gen_trans_vvvd(vsadd_d, 16, gvec_ssadd64)
gen_trans_vvvd(vsadd_bu, 16, gvec_usadd8)
gen_trans_vvvd(vsadd_hu, 16, gvec_usadd16)
gen_trans_vvvd(vsadd_wu, 16, gvec_usadd32)
gen_trans_vvvd(vsadd_du, 16, gvec_usadd64)
gen_trans_vvvd(vssub_b, 16, gvec_sssub8)
gen_trans_vvvd(vssub_h, 16, gvec_sssub16)
gen_trans_vvvd(vssub_w, 16, gvec_sssub32)
gen_trans_vvvd(vssub_d, 16, gvec_sssub64)
gen_trans_vvvd(vssub_bu, 16, gvec_ussub8)
gen_trans_vvvd(vssub_hu, 16, gvec_ussub16)
gen_trans_vvvd(vssub_wu, 16, gvec_ussub32)
gen_trans_vvvd(vssub_du, 16, gvec_ussub64)
static bool trans_vhaddw_h_b(CPULoongArchState *env, arg_vhaddw_h_b *restrict a) {
    CHECK_FPE(16);
    CHECK_FPE(16);
    int oprsz = 16;
    uint32_t desc = simd_desc(oprsz, oprsz, 0);
    helper_vhaddw_h_b(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], desc);
    env->pc += 4;
    return true;
}
static bool trans_vhaddw_w_h(CPULoongArchState *env, arg_vhaddw_w_h *restrict a) {
    CHECK_FPE(16);
    int oprsz = 16;
    uint32_t desc = simd_desc(oprsz, oprsz, 0);
    helper_vhaddw_w_h(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], desc);
    env->pc += 4;
    return true;
}
static bool trans_vhaddw_d_w(CPULoongArchState *env, arg_vhaddw_d_w *restrict a) {
    CHECK_FPE(16);
    int oprsz = 16;
    uint32_t desc = simd_desc(oprsz, oprsz, 0);
    helper_vhaddw_d_w(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], desc);
    env->pc += 4;
    return true;
}
static bool trans_vhaddw_q_d(CPULoongArchState *env, arg_vhaddw_q_d *restrict a) {
    CHECK_FPE(16);
    int oprsz = 16;
    uint32_t desc = simd_desc(oprsz, oprsz, 0);
    helper_vhaddw_q_d(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], desc);
    env->pc += 4;
    return true;
}
gen_trans_vvvd(vhaddw_hu_bu, 16, vhaddw_hu_bu)
gen_trans_vvvd(vhaddw_wu_hu, 16, vhaddw_wu_hu)
gen_trans_vvvd(vhaddw_du_wu, 16, vhaddw_du_wu)
gen_trans_vvvd(vhaddw_qu_du, 16, vhaddw_qu_du)
gen_trans_vvvd(vhsubw_h_b, 16, vhsubw_h_b)
gen_trans_vvvd(vhsubw_w_h, 16, vhsubw_w_h)
gen_trans_vvvd(vhsubw_d_w, 16, vhsubw_d_w)
gen_trans_vvvd(vhsubw_q_d, 16, vhsubw_q_d)
gen_trans_vvvd(vhsubw_hu_bu, 16, vhsubw_hu_bu)
gen_trans_vvvd(vhsubw_wu_hu, 16, vhsubw_wu_hu)
gen_trans_vvvd(vhsubw_du_wu, 16, vhsubw_du_wu)
gen_trans_vvvd(vhsubw_qu_du, 16, vhsubw_qu_du)
gen_trans_vvvd(vaddwev_h_b, 16, vaddwev_h_b)
gen_trans_vvvd(vaddwev_w_h, 16, vaddwev_w_h)
gen_trans_vvvd(vaddwev_d_w, 16, vaddwev_d_w)
gen_trans_vvvd(vaddwev_q_d, 16, vaddwev_q_d)
gen_trans_vvvd(vaddwod_h_b, 16, vaddwod_h_b)
gen_trans_vvvd(vaddwod_w_h, 16, vaddwod_w_h)
gen_trans_vvvd(vaddwod_d_w, 16, vaddwod_d_w)
gen_trans_vvvd(vaddwod_q_d, 16, vaddwod_q_d)
gen_trans_vvvd(vsubwev_h_b, 16, vsubwev_h_b)
gen_trans_vvvd(vsubwev_w_h, 16, vsubwev_w_h)
gen_trans_vvvd(vsubwev_d_w, 16, vsubwev_d_w)
gen_trans_vvvd(vsubwev_q_d, 16, vsubwev_q_d)
gen_trans_vvvd(vsubwod_h_b, 16, vsubwod_h_b)
gen_trans_vvvd(vsubwod_w_h, 16, vsubwod_w_h)
gen_trans_vvvd(vsubwod_d_w, 16, vsubwod_d_w)
gen_trans_vvvd(vsubwod_q_d, 16, vsubwod_q_d)
gen_trans_vvvd(vaddwev_h_bu, 16, vaddwev_h_bu)
gen_trans_vvvd(vaddwev_w_hu, 16, vaddwev_w_hu)
gen_trans_vvvd(vaddwev_d_wu, 16, vaddwev_d_wu)
gen_trans_vvvd(vaddwev_q_du, 16, vaddwev_q_du)
gen_trans_vvvd(vaddwod_h_bu, 16, vaddwod_h_bu)
gen_trans_vvvd(vaddwod_w_hu, 16, vaddwod_w_hu)
gen_trans_vvvd(vaddwod_d_wu, 16, vaddwod_d_wu)
gen_trans_vvvd(vaddwod_q_du, 16, vaddwod_q_du)
gen_trans_vvvd(vsubwev_h_bu, 16, vsubwev_h_bu)
gen_trans_vvvd(vsubwev_w_hu, 16, vsubwev_w_hu)
gen_trans_vvvd(vsubwev_d_wu, 16, vsubwev_d_wu)
gen_trans_vvvd(vsubwev_q_du, 16, vsubwev_q_du)
gen_trans_vvvd(vsubwod_h_bu, 16, vsubwod_h_bu)
gen_trans_vvvd(vsubwod_w_hu, 16, vsubwod_w_hu)
gen_trans_vvvd(vsubwod_d_wu, 16, vsubwod_d_wu)
gen_trans_vvvd(vsubwod_q_du, 16, vsubwod_q_du)
gen_trans_vvvd(vaddwev_h_bu_b, 16, vaddwev_h_bu_b)
gen_trans_vvvd(vaddwev_w_hu_h, 16, vaddwev_w_hu_h)
gen_trans_vvvd(vaddwev_d_wu_w, 16, vaddwev_d_wu_w)
gen_trans_vvvd(vaddwev_q_du_d, 16, vaddwev_q_du_d)
gen_trans_vvvd(vaddwod_h_bu_b, 16, vaddwod_h_bu_b)
gen_trans_vvvd(vaddwod_w_hu_h, 16, vaddwod_w_hu_h)
gen_trans_vvvd(vaddwod_d_wu_w, 16, vaddwod_d_wu_w)
gen_trans_vvvd(vaddwod_q_du_d, 16, vaddwod_q_du_d)
#define DO_VAVG(a, b)  ((a >> 1) + (b >> 1) + (a & b & 1))
#define DO_VAVGR(a, b) ((a >> 1) + (b >> 1) + ((a | b) & 1))
static inline bool vavg_b(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.B[i] = DO_VAVG(env->fpr[a->vj].vreg.B[i], env->fpr[a->vk].vreg.B[i]) ;}env->pc += 4;return true;}
static inline bool vavg_h(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.H[i] = DO_VAVG(env->fpr[a->vj].vreg.H[i], env->fpr[a->vk].vreg.H[i]) ;}env->pc += 4;return true;}
static inline bool vavg_w(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.W[i] = DO_VAVG(env->fpr[a->vj].vreg.W[i], env->fpr[a->vk].vreg.W[i]) ;}env->pc += 4;return true;}
static inline bool vavg_d(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = DO_VAVG(env->fpr[a->vj].vreg.D[i], env->fpr[a->vk].vreg.D[i]) ;}env->pc += 4;return true;}
static bool trans_vavg_b(CPULoongArchState *env, arg_vavg_b *restrict a) {CHECK_FPE(16); return vavg_b(env, a, 16);}
static bool trans_vavg_h(CPULoongArchState *env, arg_vavg_h *restrict a) {CHECK_FPE(16); return vavg_h(env, a, 16);}
static bool trans_vavg_w(CPULoongArchState *env, arg_vavg_w *restrict a) {CHECK_FPE(16); return vavg_w(env, a, 16);}
static bool trans_vavg_d(CPULoongArchState *env, arg_vavg_d *restrict a) {CHECK_FPE(16); return vavg_d(env, a, 16);}
static bool trans_xvavg_b(CPULoongArchState *env, arg_vavg_b *restrict a) {CHECK_FPE(32); return vavg_b(env, a, 32);}
static bool trans_xvavg_h(CPULoongArchState *env, arg_vavg_h *restrict a) {CHECK_FPE(32); return vavg_h(env, a, 32);}
static bool trans_xvavg_w(CPULoongArchState *env, arg_vavg_w *restrict a) {CHECK_FPE(32); return vavg_w(env, a, 32);}
static bool trans_xvavg_d(CPULoongArchState *env, arg_vavg_d *restrict a) {CHECK_FPE(32); return vavg_d(env, a, 32);}
static inline bool vavg_bu(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UB[i] = DO_VAVG(env->fpr[a->vj].vreg.UB[i], env->fpr[a->vk].vreg.UB[i]) ;}env->pc += 4;return true;}
static inline bool vavg_hu(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UH[i] = DO_VAVG(env->fpr[a->vj].vreg.UH[i], env->fpr[a->vk].vreg.UH[i]) ;}env->pc += 4;return true;}
static inline bool vavg_wu(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UW[i] = DO_VAVG(env->fpr[a->vj].vreg.UW[i], env->fpr[a->vk].vreg.UW[i]) ;}env->pc += 4;return true;}
static inline bool vavg_du(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UD[i] = DO_VAVG(env->fpr[a->vj].vreg.UD[i], env->fpr[a->vk].vreg.UD[i]) ;}env->pc += 4;return true;}
static bool trans_vavg_bu(CPULoongArchState *env, arg_vavg_bu *restrict a) {CHECK_FPE(16); return vavg_bu(env, a, 16);}
static bool trans_vavg_hu(CPULoongArchState *env, arg_vavg_hu *restrict a) {CHECK_FPE(16); return vavg_hu(env, a, 16);}
static bool trans_vavg_wu(CPULoongArchState *env, arg_vavg_wu *restrict a) {CHECK_FPE(16); return vavg_wu(env, a, 16);}
static bool trans_vavg_du(CPULoongArchState *env, arg_vavg_du *restrict a) {CHECK_FPE(16); return vavg_du(env, a, 16);}
static bool trans_xvavg_bu(CPULoongArchState *env, arg_vavg_bu *restrict a) {CHECK_FPE(32); return vavg_bu(env, a, 32);}
static bool trans_xvavg_hu(CPULoongArchState *env, arg_vavg_hu *restrict a) {CHECK_FPE(32); return vavg_hu(env, a, 32);}
static bool trans_xvavg_wu(CPULoongArchState *env, arg_vavg_wu *restrict a) {CHECK_FPE(32); return vavg_wu(env, a, 32);}
static bool trans_xvavg_du(CPULoongArchState *env, arg_vavg_du *restrict a) {CHECK_FPE(32); return vavg_du(env, a, 32);}
static inline bool vavgr_b(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.B[i] = DO_VAVGR(env->fpr[a->vj].vreg.B[i], env->fpr[a->vk].vreg.B[i]) ;}env->pc += 4;return true;}
static inline bool vavgr_h(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.H[i] = DO_VAVGR(env->fpr[a->vj].vreg.H[i], env->fpr[a->vk].vreg.H[i]) ;}env->pc += 4;return true;}
static inline bool vavgr_w(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.W[i] = DO_VAVGR(env->fpr[a->vj].vreg.W[i], env->fpr[a->vk].vreg.W[i]) ;}env->pc += 4;return true;}
static inline bool vavgr_d(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = DO_VAVGR(env->fpr[a->vj].vreg.D[i], env->fpr[a->vk].vreg.D[i]) ;}env->pc += 4;return true;}
static bool trans_vavgr_b(CPULoongArchState *env, arg_vavgr_b *restrict a) {CHECK_FPE(16); return vavgr_b(env, a, 16);}
static bool trans_vavgr_h(CPULoongArchState *env, arg_vavgr_h *restrict a) {CHECK_FPE(16); return vavgr_h(env, a, 16);}
static bool trans_vavgr_w(CPULoongArchState *env, arg_vavgr_w *restrict a) {CHECK_FPE(16); return vavgr_w(env, a, 16);}
static bool trans_vavgr_d(CPULoongArchState *env, arg_vavgr_d *restrict a) {CHECK_FPE(16); return vavgr_d(env, a, 16);}
static bool trans_xvavgr_b(CPULoongArchState *env, arg_vavgr_b *restrict a) {CHECK_FPE(32); return vavgr_b(env, a, 32);}
static bool trans_xvavgr_h(CPULoongArchState *env, arg_vavgr_h *restrict a) {CHECK_FPE(32); return vavgr_h(env, a, 32);}
static bool trans_xvavgr_w(CPULoongArchState *env, arg_vavgr_w *restrict a) {CHECK_FPE(32); return vavgr_w(env, a, 32);}
static bool trans_xvavgr_d(CPULoongArchState *env, arg_vavgr_d *restrict a) {CHECK_FPE(32); return vavgr_d(env, a, 32);}
static inline bool vavgr_bu(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UB[i] = DO_VAVGR(env->fpr[a->vj].vreg.UB[i], env->fpr[a->vk].vreg.UB[i]) ;}env->pc += 4;return true;}
static inline bool vavgr_hu(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UH[i] = DO_VAVGR(env->fpr[a->vj].vreg.UH[i], env->fpr[a->vk].vreg.UH[i]) ;}env->pc += 4;return true;}
static inline bool vavgr_wu(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UW[i] = DO_VAVGR(env->fpr[a->vj].vreg.UW[i], env->fpr[a->vk].vreg.UW[i]) ;}env->pc += 4;return true;}
static inline bool vavgr_du(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UD[i] = DO_VAVGR(env->fpr[a->vj].vreg.UD[i], env->fpr[a->vk].vreg.UD[i]) ;}env->pc += 4;return true;}
static bool trans_vavgr_bu(CPULoongArchState *env, arg_vavgr_bu *restrict a) {CHECK_FPE(16); return vavgr_bu(env, a, 16);}
static bool trans_vavgr_hu(CPULoongArchState *env, arg_vavgr_hu *restrict a) {CHECK_FPE(16); return vavgr_hu(env, a, 16);}
static bool trans_vavgr_wu(CPULoongArchState *env, arg_vavgr_wu *restrict a) {CHECK_FPE(16); return vavgr_wu(env, a, 16);}
static bool trans_vavgr_du(CPULoongArchState *env, arg_vavgr_du *restrict a) {CHECK_FPE(16); return vavgr_du(env, a, 16);}
static bool trans_xvavgr_bu(CPULoongArchState *env, arg_vavgr_bu *restrict a) {CHECK_FPE(32); return vavgr_bu(env, a, 32);}
static bool trans_xvavgr_hu(CPULoongArchState *env, arg_vavgr_hu *restrict a) {CHECK_FPE(32); return vavgr_hu(env, a, 32);}
static bool trans_xvavgr_wu(CPULoongArchState *env, arg_vavgr_wu *restrict a) {CHECK_FPE(32); return vavgr_wu(env, a, 32);}
static bool trans_xvavgr_du(CPULoongArchState *env, arg_vavgr_du *restrict a) {CHECK_FPE(32); return vavgr_du(env, a, 32);}
#define DO_VABSD(a, b)  ((a > b) ? (a -b) : (b-a))
static inline bool vabsd_b(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.B[i] = DO_VABSD(env->fpr[a->vj].vreg.B[i], env->fpr[a->vk].vreg.B[i]) ;}env->pc += 4;return true;}
static inline bool vabsd_h(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.H[i] = DO_VABSD(env->fpr[a->vj].vreg.H[i], env->fpr[a->vk].vreg.H[i]) ;}env->pc += 4;return true;}
static inline bool vabsd_w(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.W[i] = DO_VABSD(env->fpr[a->vj].vreg.W[i], env->fpr[a->vk].vreg.W[i]) ;}env->pc += 4;return true;}
static inline bool vabsd_d(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = DO_VABSD(env->fpr[a->vj].vreg.D[i], env->fpr[a->vk].vreg.D[i]) ;}env->pc += 4;return true;}
static bool trans_vabsd_b(CPULoongArchState *env, arg_vabsd_b *restrict a) {CHECK_FPE(16); return vabsd_b(env, a, 16);}
static bool trans_vabsd_h(CPULoongArchState *env, arg_vabsd_h *restrict a) {CHECK_FPE(16); return vabsd_h(env, a, 16);}
static bool trans_vabsd_w(CPULoongArchState *env, arg_vabsd_w *restrict a) {CHECK_FPE(16); return vabsd_w(env, a, 16);}
static bool trans_vabsd_d(CPULoongArchState *env, arg_vabsd_d *restrict a) {CHECK_FPE(16); return vabsd_d(env, a, 16);}
static bool trans_xvabsd_b(CPULoongArchState *env, arg_vabsd_b *restrict a) {CHECK_FPE(32); return vabsd_b(env, a, 32);}
static bool trans_xvabsd_h(CPULoongArchState *env, arg_vabsd_h *restrict a) {CHECK_FPE(32); return vabsd_h(env, a, 32);}
static bool trans_xvabsd_w(CPULoongArchState *env, arg_vabsd_w *restrict a) {CHECK_FPE(32); return vabsd_w(env, a, 32);}
static bool trans_xvabsd_d(CPULoongArchState *env, arg_vabsd_d *restrict a) {CHECK_FPE(32); return vabsd_d(env, a, 32);}
static inline bool vabsd_bu(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UB[i] = DO_VABSD(env->fpr[a->vj].vreg.UB[i], env->fpr[a->vk].vreg.UB[i]) ;}env->pc += 4;return true;}
static inline bool vabsd_hu(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UH[i] = DO_VABSD(env->fpr[a->vj].vreg.UH[i], env->fpr[a->vk].vreg.UH[i]) ;}env->pc += 4;return true;}
static inline bool vabsd_wu(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UW[i] = DO_VABSD(env->fpr[a->vj].vreg.UW[i], env->fpr[a->vk].vreg.UW[i]) ;}env->pc += 4;return true;}
static inline bool vabsd_du(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UD[i] = DO_VABSD(env->fpr[a->vj].vreg.UD[i], env->fpr[a->vk].vreg.UD[i]) ;}env->pc += 4;return true;}
static bool trans_vabsd_bu(CPULoongArchState *env, arg_vabsd_bu *restrict a) {CHECK_FPE(16); return vabsd_bu(env, a, 16);}
static bool trans_vabsd_hu(CPULoongArchState *env, arg_vabsd_hu *restrict a) {CHECK_FPE(16); return vabsd_hu(env, a, 16);}
static bool trans_vabsd_wu(CPULoongArchState *env, arg_vabsd_wu *restrict a) {CHECK_FPE(16); return vabsd_wu(env, a, 16);}
static bool trans_vabsd_du(CPULoongArchState *env, arg_vabsd_du *restrict a) {CHECK_FPE(16); return vabsd_du(env, a, 16);}
static bool trans_xvabsd_bu(CPULoongArchState *env, arg_vabsd_bu *restrict a) {CHECK_FPE(32); return vabsd_bu(env, a, 32);}
static bool trans_xvabsd_hu(CPULoongArchState *env, arg_vabsd_hu *restrict a) {CHECK_FPE(32); return vabsd_hu(env, a, 32);}
static bool trans_xvabsd_wu(CPULoongArchState *env, arg_vabsd_wu *restrict a) {CHECK_FPE(32); return vabsd_wu(env, a, 32);}
static bool trans_xvabsd_du(CPULoongArchState *env, arg_vabsd_du *restrict a) {CHECK_FPE(32); return vabsd_du(env, a, 32);}
#define DO_VABS(a)  ((a < 0) ? (-a) : (a))
static inline bool vadda_b(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.B[i] = DO_VABS(env->fpr[a->vj].vreg.B[i]) + DO_VABS(env->fpr[a->vk].vreg.B[i]) ;}env->pc += 4;return true;}
static inline bool vadda_h(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.H[i] = DO_VABS(env->fpr[a->vj].vreg.H[i]) + DO_VABS(env->fpr[a->vk].vreg.H[i]) ;}env->pc += 4;return true;}
static inline bool vadda_w(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.W[i] = DO_VABS(env->fpr[a->vj].vreg.W[i]) + DO_VABS(env->fpr[a->vk].vreg.W[i]) ;}env->pc += 4;return true;}
static inline bool vadda_d(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = DO_VABS(env->fpr[a->vj].vreg.D[i]) + DO_VABS(env->fpr[a->vk].vreg.D[i]) ;}env->pc += 4;return true;}
static bool trans_vadda_b(CPULoongArchState *env, arg_vadda_b *restrict a) {CHECK_FPE(16); return vadda_b(env, a, 16);}
static bool trans_vadda_h(CPULoongArchState *env, arg_vadda_h *restrict a) {CHECK_FPE(16); return vadda_h(env, a, 16);}
static bool trans_vadda_w(CPULoongArchState *env, arg_vadda_w *restrict a) {CHECK_FPE(16); return vadda_w(env, a, 16);}
static bool trans_vadda_d(CPULoongArchState *env, arg_vadda_d *restrict a) {CHECK_FPE(16); return vadda_d(env, a, 16);}
static bool trans_xvadda_b(CPULoongArchState *env, arg_vadda_b *restrict a) {CHECK_FPE(32); return vadda_b(env, a, 32);}
static bool trans_xvadda_h(CPULoongArchState *env, arg_vadda_h *restrict a) {CHECK_FPE(32); return vadda_h(env, a, 32);}
static bool trans_xvadda_w(CPULoongArchState *env, arg_vadda_w *restrict a) {CHECK_FPE(32); return vadda_w(env, a, 32);}
static bool trans_xvadda_d(CPULoongArchState *env, arg_vadda_d *restrict a) {CHECK_FPE(32); return vadda_d(env, a, 32);}
static inline bool vmax_b(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.B[i] = MAX(env->fpr[a->vj].vreg.B[i], env->fpr[a->vk].vreg.B[i]) ;}env->pc += 4;return true;}
static inline bool vmax_h(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.H[i] = MAX(env->fpr[a->vj].vreg.H[i], env->fpr[a->vk].vreg.H[i]) ;}env->pc += 4;return true;}
static inline bool vmax_w(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.W[i] = MAX(env->fpr[a->vj].vreg.W[i], env->fpr[a->vk].vreg.W[i]) ;}env->pc += 4;return true;}
static inline bool vmax_d(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = MAX(env->fpr[a->vj].vreg.D[i], env->fpr[a->vk].vreg.D[i]) ;}env->pc += 4;return true;}
static bool trans_vmax_b(CPULoongArchState *env, arg_vmax_b *restrict a) {CHECK_FPE(16); return vmax_b(env, a, 16);}
static bool trans_vmax_h(CPULoongArchState *env, arg_vmax_h *restrict a) {CHECK_FPE(16); return vmax_h(env, a, 16);}
static bool trans_vmax_w(CPULoongArchState *env, arg_vmax_w *restrict a) {CHECK_FPE(16); return vmax_w(env, a, 16);}
static bool trans_vmax_d(CPULoongArchState *env, arg_vmax_d *restrict a) {CHECK_FPE(16); return vmax_d(env, a, 16);}
static bool trans_xvmax_b(CPULoongArchState *env, arg_vmax_b *restrict a) {CHECK_FPE(32); return vmax_b(env, a, 32);}
static bool trans_xvmax_h(CPULoongArchState *env, arg_vmax_h *restrict a) {CHECK_FPE(32); return vmax_h(env, a, 32);}
static bool trans_xvmax_w(CPULoongArchState *env, arg_vmax_w *restrict a) {CHECK_FPE(32); return vmax_w(env, a, 32);}
static bool trans_xvmax_d(CPULoongArchState *env, arg_vmax_d *restrict a) {CHECK_FPE(32); return vmax_d(env, a, 32);}
static inline bool vmaxi_b(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.B[i] = MAX(env->fpr[a->vj].vreg.B[i], a->imm) ;}env->pc += 4;return true;}
static inline bool vmaxi_h(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.H[i] = MAX(env->fpr[a->vj].vreg.H[i], a->imm) ;}env->pc += 4;return true;}
static inline bool vmaxi_w(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.W[i] = MAX(env->fpr[a->vj].vreg.W[i], a->imm) ;}env->pc += 4;return true;}
static inline bool vmaxi_d(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = MAX(env->fpr[a->vj].vreg.D[i], a->imm) ;}env->pc += 4;return true;}
static bool trans_vmaxi_b(CPULoongArchState *env, arg_vmaxi_b *restrict a) {CHECK_FPE(16); return vmaxi_b(env, a, 16);}
static bool trans_vmaxi_h(CPULoongArchState *env, arg_vmaxi_h *restrict a) {CHECK_FPE(16); return vmaxi_h(env, a, 16);}
static bool trans_vmaxi_w(CPULoongArchState *env, arg_vmaxi_w *restrict a) {CHECK_FPE(16); return vmaxi_w(env, a, 16);}
static bool trans_vmaxi_d(CPULoongArchState *env, arg_vmaxi_d *restrict a) {CHECK_FPE(16); return vmaxi_d(env, a, 16);}
static bool trans_xvmaxi_b(CPULoongArchState *env, arg_vmaxi_b *restrict a) {CHECK_FPE(32); return vmaxi_b(env, a, 32);}
static bool trans_xvmaxi_h(CPULoongArchState *env, arg_vmaxi_h *restrict a) {CHECK_FPE(32); return vmaxi_h(env, a, 32);}
static bool trans_xvmaxi_w(CPULoongArchState *env, arg_vmaxi_w *restrict a) {CHECK_FPE(32); return vmaxi_w(env, a, 32);}
static bool trans_xvmaxi_d(CPULoongArchState *env, arg_vmaxi_d *restrict a) {CHECK_FPE(32); return vmaxi_d(env, a, 32);}
static inline bool vmax_bu(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UB[i] = MAX(env->fpr[a->vj].vreg.UB[i], env->fpr[a->vk].vreg.UB[i]) ;}env->pc += 4;return true;}
static inline bool vmax_hu(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UH[i] = MAX(env->fpr[a->vj].vreg.UH[i], env->fpr[a->vk].vreg.UH[i]) ;}env->pc += 4;return true;}
static inline bool vmax_wu(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UW[i] = MAX(env->fpr[a->vj].vreg.UW[i], env->fpr[a->vk].vreg.UW[i]) ;}env->pc += 4;return true;}
static inline bool vmax_du(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UD[i] = MAX(env->fpr[a->vj].vreg.UD[i], env->fpr[a->vk].vreg.UD[i]) ;}env->pc += 4;return true;}
static bool trans_vmax_bu(CPULoongArchState *env, arg_vmax_bu *restrict a) {CHECK_FPE(16); return vmax_bu(env, a, 16);}
static bool trans_vmax_hu(CPULoongArchState *env, arg_vmax_hu *restrict a) {CHECK_FPE(16); return vmax_hu(env, a, 16);}
static bool trans_vmax_wu(CPULoongArchState *env, arg_vmax_wu *restrict a) {CHECK_FPE(16); return vmax_wu(env, a, 16);}
static bool trans_vmax_du(CPULoongArchState *env, arg_vmax_du *restrict a) {CHECK_FPE(16); return vmax_du(env, a, 16);}
static bool trans_xvmax_bu(CPULoongArchState *env, arg_vmax_bu *restrict a) {CHECK_FPE(32); return vmax_bu(env, a, 32);}
static bool trans_xvmax_hu(CPULoongArchState *env, arg_vmax_hu *restrict a) {CHECK_FPE(32); return vmax_hu(env, a, 32);}
static bool trans_xvmax_wu(CPULoongArchState *env, arg_vmax_wu *restrict a) {CHECK_FPE(32); return vmax_wu(env, a, 32);}
static bool trans_xvmax_du(CPULoongArchState *env, arg_vmax_du *restrict a) {CHECK_FPE(32); return vmax_du(env, a, 32);}
static inline bool vmaxi_bu(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UB[i] = MAX(env->fpr[a->vj].vreg.UB[i], (uint64_t)a->imm) ;}env->pc += 4;return true;}
static inline bool vmaxi_hu(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UH[i] = MAX(env->fpr[a->vj].vreg.UH[i], (uint64_t)a->imm) ;}env->pc += 4;return true;}
static inline bool vmaxi_wu(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UW[i] = MAX(env->fpr[a->vj].vreg.UW[i], (uint64_t)a->imm) ;}env->pc += 4;return true;}
static inline bool vmaxi_du(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UD[i] = MAX(env->fpr[a->vj].vreg.UD[i], (uint64_t)a->imm) ;}env->pc += 4;return true;}
static bool trans_vmaxi_bu(CPULoongArchState *env, arg_vmaxi_bu *restrict a) {CHECK_FPE(16); return vmaxi_bu(env, a, 16);}
static bool trans_vmaxi_hu(CPULoongArchState *env, arg_vmaxi_hu *restrict a) {CHECK_FPE(16); return vmaxi_hu(env, a, 16);}
static bool trans_vmaxi_wu(CPULoongArchState *env, arg_vmaxi_wu *restrict a) {CHECK_FPE(16); return vmaxi_wu(env, a, 16);}
static bool trans_vmaxi_du(CPULoongArchState *env, arg_vmaxi_du *restrict a) {CHECK_FPE(16); return vmaxi_du(env, a, 16);}
static bool trans_xvmaxi_bu(CPULoongArchState *env, arg_vmaxi_bu *restrict a) {CHECK_FPE(32); return vmaxi_bu(env, a, 32);}
static bool trans_xvmaxi_hu(CPULoongArchState *env, arg_vmaxi_hu *restrict a) {CHECK_FPE(32); return vmaxi_hu(env, a, 32);}
static bool trans_xvmaxi_wu(CPULoongArchState *env, arg_vmaxi_wu *restrict a) {CHECK_FPE(32); return vmaxi_wu(env, a, 32);}
static bool trans_xvmaxi_du(CPULoongArchState *env, arg_vmaxi_du *restrict a) {CHECK_FPE(32); return vmaxi_du(env, a, 32);}
static inline bool vmin_b(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.B[i] = MIN(env->fpr[a->vj].vreg.B[i], env->fpr[a->vk].vreg.B[i]) ;}env->pc += 4;return true;}
static inline bool vmin_h(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.H[i] = MIN(env->fpr[a->vj].vreg.H[i], env->fpr[a->vk].vreg.H[i]) ;}env->pc += 4;return true;}
static inline bool vmin_w(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.W[i] = MIN(env->fpr[a->vj].vreg.W[i], env->fpr[a->vk].vreg.W[i]) ;}env->pc += 4;return true;}
static inline bool vmin_d(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = MIN(env->fpr[a->vj].vreg.D[i], env->fpr[a->vk].vreg.D[i]) ;}env->pc += 4;return true;}
static bool trans_vmin_b(CPULoongArchState *env, arg_vmin_b *restrict a) {CHECK_FPE(16); return vmin_b(env, a, 16);}
static bool trans_vmin_h(CPULoongArchState *env, arg_vmin_h *restrict a) {CHECK_FPE(16); return vmin_h(env, a, 16);}
static bool trans_vmin_w(CPULoongArchState *env, arg_vmin_w *restrict a) {CHECK_FPE(16); return vmin_w(env, a, 16);}
static bool trans_vmin_d(CPULoongArchState *env, arg_vmin_d *restrict a) {CHECK_FPE(16); return vmin_d(env, a, 16);}
static bool trans_xvmin_b(CPULoongArchState *env, arg_vmin_b *restrict a) {CHECK_FPE(32); return vmin_b(env, a, 32);}
static bool trans_xvmin_h(CPULoongArchState *env, arg_vmin_h *restrict a) {CHECK_FPE(32); return vmin_h(env, a, 32);}
static bool trans_xvmin_w(CPULoongArchState *env, arg_vmin_w *restrict a) {CHECK_FPE(32); return vmin_w(env, a, 32);}
static bool trans_xvmin_d(CPULoongArchState *env, arg_vmin_d *restrict a) {CHECK_FPE(32); return vmin_d(env, a, 32);}
static inline bool vmini_b(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.B[i] = MIN(env->fpr[a->vj].vreg.B[i], a->imm) ;}env->pc += 4;return true;}
static inline bool vmini_h(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.H[i] = MIN(env->fpr[a->vj].vreg.H[i], a->imm) ;}env->pc += 4;return true;}
static inline bool vmini_w(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.W[i] = MIN(env->fpr[a->vj].vreg.W[i], a->imm) ;}env->pc += 4;return true;}
static inline bool vmini_d(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = MIN(env->fpr[a->vj].vreg.D[i], a->imm) ;}env->pc += 4;return true;}
static bool trans_vmini_b(CPULoongArchState *env, arg_vmini_b *restrict a) {CHECK_FPE(16); return vmini_b(env, a, 16);}
static bool trans_vmini_h(CPULoongArchState *env, arg_vmini_h *restrict a) {CHECK_FPE(16); return vmini_h(env, a, 16);}
static bool trans_vmini_w(CPULoongArchState *env, arg_vmini_w *restrict a) {CHECK_FPE(16); return vmini_w(env, a, 16);}
static bool trans_vmini_d(CPULoongArchState *env, arg_vmini_d *restrict a) {CHECK_FPE(16); return vmini_d(env, a, 16);}
static bool trans_xvmini_b(CPULoongArchState *env, arg_vmini_b *restrict a) {CHECK_FPE(32); return vmini_b(env, a, 32);}
static bool trans_xvmini_h(CPULoongArchState *env, arg_vmini_h *restrict a) {CHECK_FPE(32); return vmini_h(env, a, 32);}
static bool trans_xvmini_w(CPULoongArchState *env, arg_vmini_w *restrict a) {CHECK_FPE(32); return vmini_w(env, a, 32);}
static bool trans_xvmini_d(CPULoongArchState *env, arg_vmini_d *restrict a) {CHECK_FPE(32); return vmini_d(env, a, 32);}
static inline bool vmin_bu(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UB[i] = MIN(env->fpr[a->vj].vreg.UB[i], env->fpr[a->vk].vreg.UB[i]) ;}env->pc += 4;return true;}
static inline bool vmin_hu(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UH[i] = MIN(env->fpr[a->vj].vreg.UH[i], env->fpr[a->vk].vreg.UH[i]) ;}env->pc += 4;return true;}
static inline bool vmin_wu(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UW[i] = MIN(env->fpr[a->vj].vreg.UW[i], env->fpr[a->vk].vreg.UW[i]) ;}env->pc += 4;return true;}
static inline bool vmin_du(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UD[i] = MIN(env->fpr[a->vj].vreg.UD[i], env->fpr[a->vk].vreg.UD[i]) ;}env->pc += 4;return true;}
static bool trans_vmin_bu(CPULoongArchState *env, arg_vmin_bu *restrict a) {CHECK_FPE(16); return vmin_bu(env, a, 16);}
static bool trans_vmin_hu(CPULoongArchState *env, arg_vmin_hu *restrict a) {CHECK_FPE(16); return vmin_hu(env, a, 16);}
static bool trans_vmin_wu(CPULoongArchState *env, arg_vmin_wu *restrict a) {CHECK_FPE(16); return vmin_wu(env, a, 16);}
static bool trans_vmin_du(CPULoongArchState *env, arg_vmin_du *restrict a) {CHECK_FPE(16); return vmin_du(env, a, 16);}
static bool trans_xvmin_bu(CPULoongArchState *env, arg_vmin_bu *restrict a) {CHECK_FPE(32); return vmin_bu(env, a, 32);}
static bool trans_xvmin_hu(CPULoongArchState *env, arg_vmin_hu *restrict a) {CHECK_FPE(32); return vmin_hu(env, a, 32);}
static bool trans_xvmin_wu(CPULoongArchState *env, arg_vmin_wu *restrict a) {CHECK_FPE(32); return vmin_wu(env, a, 32);}
static bool trans_xvmin_du(CPULoongArchState *env, arg_vmin_du *restrict a) {CHECK_FPE(32); return vmin_du(env, a, 32);}
static inline bool vmini_bu(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UB[i] = MIN(env->fpr[a->vj].vreg.UB[i], (uint64_t)a->imm) ;}env->pc += 4;return true;}
static inline bool vmini_hu(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UH[i] = MIN(env->fpr[a->vj].vreg.UH[i], (uint64_t)a->imm) ;}env->pc += 4;return true;}
static inline bool vmini_wu(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UW[i] = MIN(env->fpr[a->vj].vreg.UW[i], (uint64_t)a->imm) ;}env->pc += 4;return true;}
static inline bool vmini_du(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UD[i] = MIN(env->fpr[a->vj].vreg.UD[i], (uint64_t)a->imm) ;}env->pc += 4;return true;}
static bool trans_vmini_bu(CPULoongArchState *env, arg_vmini_bu *restrict a) {CHECK_FPE(16); return vmini_bu(env, a, 16);}
static bool trans_vmini_hu(CPULoongArchState *env, arg_vmini_hu *restrict a) {CHECK_FPE(16); return vmini_hu(env, a, 16);}
static bool trans_vmini_wu(CPULoongArchState *env, arg_vmini_wu *restrict a) {CHECK_FPE(16); return vmini_wu(env, a, 16);}
static bool trans_vmini_du(CPULoongArchState *env, arg_vmini_du *restrict a) {CHECK_FPE(16); return vmini_du(env, a, 16);}
static bool trans_xvmini_bu(CPULoongArchState *env, arg_vmini_bu *restrict a) {CHECK_FPE(32); return vmini_bu(env, a, 32);}
static bool trans_xvmini_hu(CPULoongArchState *env, arg_vmini_hu *restrict a) {CHECK_FPE(32); return vmini_hu(env, a, 32);}
static bool trans_xvmini_wu(CPULoongArchState *env, arg_vmini_wu *restrict a) {CHECK_FPE(32); return vmini_wu(env, a, 32);}
static bool trans_xvmini_du(CPULoongArchState *env, arg_vmini_du *restrict a) {CHECK_FPE(32); return vmini_du(env, a, 32);}
static inline bool vmul_b(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.B[i] = (env->fpr[a->vj].vreg.B[i] * env->fpr[a->vk].vreg.B[i]);}env->pc += 4;return true;}
static inline bool vmul_h(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.H[i] = (env->fpr[a->vj].vreg.H[i] * env->fpr[a->vk].vreg.H[i]);}env->pc += 4;return true;}
static inline bool vmul_w(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.W[i] = (env->fpr[a->vj].vreg.W[i] * env->fpr[a->vk].vreg.W[i]);}env->pc += 4;return true;}
static inline bool vmul_d(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = (env->fpr[a->vj].vreg.D[i] * env->fpr[a->vk].vreg.D[i]);}env->pc += 4;return true;}
static bool trans_vmul_b(CPULoongArchState *env, arg_vmul_b *restrict a) {CHECK_FPE(16); return vmul_b(env, a, 16);}
static bool trans_vmul_h(CPULoongArchState *env, arg_vmul_h *restrict a) {CHECK_FPE(16); return vmul_h(env, a, 16);}
static bool trans_vmul_w(CPULoongArchState *env, arg_vmul_w *restrict a) {CHECK_FPE(16); return vmul_w(env, a, 16);}
static bool trans_vmul_d(CPULoongArchState *env, arg_vmul_d *restrict a) {CHECK_FPE(16); return vmul_d(env, a, 16);}
static bool trans_xvmul_b(CPULoongArchState *env, arg_vmul_b *restrict a) {CHECK_FPE(32); return vmul_b(env, a, 32);}
static bool trans_xvmul_h(CPULoongArchState *env, arg_vmul_h *restrict a) {CHECK_FPE(32); return vmul_h(env, a, 32);}
static bool trans_xvmul_w(CPULoongArchState *env, arg_vmul_w *restrict a) {CHECK_FPE(32); return vmul_w(env, a, 32);}
static bool trans_xvmul_d(CPULoongArchState *env, arg_vmul_d *restrict a) {CHECK_FPE(32); return vmul_d(env, a, 32);}
gen_trans_vvvd(vmuh_b, 16, vmuh_b)
gen_trans_vvvd(vmuh_bu, 16, vmuh_bu)
gen_trans_vvvd(vmuh_d, 16, vmuh_d)
gen_trans_vvvd(vmuh_du, 16, vmuh_du)
gen_trans_vvvd(vmuh_h, 16, vmuh_h)
gen_trans_vvvd(vmuh_hu, 16, vmuh_hu)
gen_trans_vvvd(vmuh_w, 16, vmuh_w)
gen_trans_vvvd(vmuh_wu, 16, vmuh_wu)
gen_trans_vvvd(vmulwev_h_b, 16, vmulwev_h_b)
gen_trans_vvvd(vmulwev_w_h, 16, vmulwev_w_h)
gen_trans_vvvd(vmulwev_d_w, 16, vmulwev_d_w)
gen_trans_vvvd(vmulwod_h_b, 16, vmulwod_h_b)
gen_trans_vvvd(vmulwod_w_h, 16, vmulwod_w_h)
gen_trans_vvvd(vmulwod_d_w, 16, vmulwod_d_w)
gen_trans_vvvd(vmulwev_h_bu, 16, vmulwev_h_bu)
gen_trans_vvvd(vmulwev_w_hu, 16, vmulwev_w_hu)
gen_trans_vvvd(vmulwev_d_wu, 16, vmulwev_d_wu)
gen_trans_vvvd(vmulwod_h_bu, 16, vmulwod_h_bu)
gen_trans_vvvd(vmulwod_w_hu, 16, vmulwod_w_hu)
gen_trans_vvvd(vmulwod_d_wu, 16, vmulwod_d_wu)
gen_trans_vvvd(vmulwev_h_bu_b, 16, vmulwev_h_bu_b)
gen_trans_vvvd(vmulwev_w_hu_h, 16, vmulwev_w_hu_h)
gen_trans_vvvd(vmulwev_d_wu_w, 16, vmulwev_d_wu_w)
gen_trans_vvvd(vmulwod_h_bu_b, 16, vmulwod_h_bu_b)
gen_trans_vvvd(vmulwod_w_hu_h, 16, vmulwod_w_hu_h)
gen_trans_vvvd(vmulwod_d_wu_w, 16, vmulwod_d_wu_w)
static bool trans_vmulwev_q_d(CPULoongArchState *env, arg_vmulwev_q_d *restrict a) {
    int size = 16;
    int index = 0;
    CHECK_FPE(size);
    for (int i = 0; i < (size / 16); i++) {
        env->fpr[a->vd].vreg.Q[i] = (__int128_t)env->fpr[a->vj].vreg.D[i * 2 + index] * (__int128_t)env->fpr[a->vk].vreg.D[i *2 + index];
    }
    env->pc += 4;
    return true;
}
static bool trans_vmulwod_q_d(CPULoongArchState *env, arg_vmulwod_q_d *restrict a) {
    int size = 16;
    int index = 1;
    CHECK_FPE(size);
    for (int i = 0; i < (size / 16); i++) {
        env->fpr[a->vd].vreg.Q[i] = (__int128_t)env->fpr[a->vj].vreg.D[i * 2 + index] * (__int128_t)env->fpr[a->vk].vreg.D[i *2 + index];
    }
    env->pc += 4;
    return true;
}
static bool trans_vmulwev_q_du(CPULoongArchState *env, arg_vmulwev_q_du *restrict a) {
    int size = 16;
    int index = 0;
    CHECK_FPE(size);
    for (int i = 0; i < (size / 16); i++) {
        env->fpr[a->vd].vreg.Q[i] = (__uint128_t)env->fpr[a->vj].vreg.UD[i * 2 + index] * (__uint128_t)env->fpr[a->vk].vreg.UD[i *2 + index];
    }
    env->pc += 4;
    return true;
}
static bool trans_vmulwod_q_du(CPULoongArchState *env, arg_vmulwod_q_du *restrict a) {
    int size = 16;
    int index = 1;
    CHECK_FPE(size);
    for (int i = 0; i < (size / 16); i++) {
        env->fpr[a->vd].vreg.Q[i] = (__uint128_t)env->fpr[a->vj].vreg.UD[i * 2 + index] * (__uint128_t)env->fpr[a->vk].vreg.UD[i *2 + index];
    }
    env->pc += 4;
    return true;
}
static bool trans_vmulwev_q_du_d(CPULoongArchState *env, arg_vmulwev_q_du_d *restrict a) {
    int size = 16;
    int index = 0;
    CHECK_FPE(size);
    for (int i = 0; i < (size / 16); i++) {
        env->fpr[a->vd].vreg.Q[i] = (__uint128_t)env->fpr[a->vj].vreg.UD[i * 2 + index] * (__int128_t)env->fpr[a->vk].vreg.D[i *2 + index];
    }
    env->pc += 4;
    return true;
}
static bool trans_vmulwod_q_du_d(CPULoongArchState *env, arg_vmulwod_q_du_d *restrict a) {
    int size = 16;
    int index = 1;
    CHECK_FPE(size);
    for (int i = 0; i < (size / 16); i++) {
        env->fpr[a->vd].vreg.Q[i] = (__uint128_t)env->fpr[a->vj].vreg.UD[i * 2 + index] * (__int128_t)env->fpr[a->vk].vreg.D[i *2 + index];
    }
    env->pc += 4;
    return true;
}
static inline bool vmadd_b(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.B[i] = env->fpr[a->vd].vreg.B[i] + (env->fpr[a->vj].vreg.B[i] * env->fpr[a->vk].vreg.B[i]);}env->pc += 4;return true;}
static inline bool vmadd_h(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.H[i] = env->fpr[a->vd].vreg.H[i] + (env->fpr[a->vj].vreg.H[i] * env->fpr[a->vk].vreg.H[i]);}env->pc += 4;return true;}
static inline bool vmadd_w(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.W[i] = env->fpr[a->vd].vreg.W[i] + (env->fpr[a->vj].vreg.W[i] * env->fpr[a->vk].vreg.W[i]);}env->pc += 4;return true;}
static inline bool vmadd_d(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = env->fpr[a->vd].vreg.D[i] + (env->fpr[a->vj].vreg.D[i] * env->fpr[a->vk].vreg.D[i]);}env->pc += 4;return true;}
static bool trans_vmadd_b(CPULoongArchState *env, arg_vmadd_b *restrict a) {CHECK_FPE(16); return vmadd_b(env, a, 16);}
static bool trans_vmadd_h(CPULoongArchState *env, arg_vmadd_h *restrict a) {CHECK_FPE(16); return vmadd_h(env, a, 16);}
static bool trans_vmadd_w(CPULoongArchState *env, arg_vmadd_w *restrict a) {CHECK_FPE(16); return vmadd_w(env, a, 16);}
static bool trans_vmadd_d(CPULoongArchState *env, arg_vmadd_d *restrict a) {CHECK_FPE(16); return vmadd_d(env, a, 16);}
static bool trans_xvmadd_b(CPULoongArchState *env, arg_vmadd_b *restrict a) {CHECK_FPE(32); return vmadd_b(env, a, 32);}
static bool trans_xvmadd_h(CPULoongArchState *env, arg_vmadd_h *restrict a) {CHECK_FPE(32); return vmadd_h(env, a, 32);}
static bool trans_xvmadd_w(CPULoongArchState *env, arg_vmadd_w *restrict a) {CHECK_FPE(32); return vmadd_w(env, a, 32);}
static bool trans_xvmadd_d(CPULoongArchState *env, arg_vmadd_d *restrict a) {CHECK_FPE(32); return vmadd_d(env, a, 32);}
static inline bool vmsub_b(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.B[i] = env->fpr[a->vd].vreg.B[i] - (env->fpr[a->vj].vreg.B[i] * env->fpr[a->vk].vreg.B[i]);}env->pc += 4;return true;}
static inline bool vmsub_h(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.H[i] = env->fpr[a->vd].vreg.H[i] - (env->fpr[a->vj].vreg.H[i] * env->fpr[a->vk].vreg.H[i]);}env->pc += 4;return true;}
static inline bool vmsub_w(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.W[i] = env->fpr[a->vd].vreg.W[i] - (env->fpr[a->vj].vreg.W[i] * env->fpr[a->vk].vreg.W[i]);}env->pc += 4;return true;}
static inline bool vmsub_d(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = env->fpr[a->vd].vreg.D[i] - (env->fpr[a->vj].vreg.D[i] * env->fpr[a->vk].vreg.D[i]);}env->pc += 4;return true;}
static bool trans_vmsub_b(CPULoongArchState *env, arg_vmsub_b *restrict a) {CHECK_FPE(16); return vmsub_b(env, a, 16);}
static bool trans_vmsub_h(CPULoongArchState *env, arg_vmsub_h *restrict a) {CHECK_FPE(16); return vmsub_h(env, a, 16);}
static bool trans_vmsub_w(CPULoongArchState *env, arg_vmsub_w *restrict a) {CHECK_FPE(16); return vmsub_w(env, a, 16);}
static bool trans_vmsub_d(CPULoongArchState *env, arg_vmsub_d *restrict a) {CHECK_FPE(16); return vmsub_d(env, a, 16);}
static bool trans_xvmsub_b(CPULoongArchState *env, arg_vmsub_b *restrict a) {CHECK_FPE(32); return vmsub_b(env, a, 32);}
static bool trans_xvmsub_h(CPULoongArchState *env, arg_vmsub_h *restrict a) {CHECK_FPE(32); return vmsub_h(env, a, 32);}
static bool trans_xvmsub_w(CPULoongArchState *env, arg_vmsub_w *restrict a) {CHECK_FPE(32); return vmsub_w(env, a, 32);}
static bool trans_xvmsub_d(CPULoongArchState *env, arg_vmsub_d *restrict a) {CHECK_FPE(32); return vmsub_d(env, a, 32);}

gen_trans_vvvd(vmaddwev_h_b, 16, vmaddwev_h_b)
gen_trans_vvvd(vmaddwev_w_h, 16, vmaddwev_w_h)
gen_trans_vvvd(vmaddwev_d_w, 16, vmaddwev_d_w)
gen_trans_vvvd(vmaddwod_h_b, 16, vmaddwod_h_b)
gen_trans_vvvd(vmaddwod_w_h, 16, vmaddwod_w_h)
gen_trans_vvvd(vmaddwod_d_w, 16, vmaddwod_d_w)
gen_trans_vvvd(vmaddwev_h_bu, 16, vmaddwev_h_bu)
gen_trans_vvvd(vmaddwev_w_hu, 16, vmaddwev_w_hu)
gen_trans_vvvd(vmaddwev_d_wu, 16, vmaddwev_d_wu)
gen_trans_vvvd(vmaddwod_h_bu, 16, vmaddwod_h_bu)
gen_trans_vvvd(vmaddwod_w_hu, 16, vmaddwod_w_hu)
gen_trans_vvvd(vmaddwod_d_wu, 16, vmaddwod_d_wu)
gen_trans_vvvd(vmaddwev_h_bu_b, 16, vmaddwev_h_bu_b)
gen_trans_vvvd(vmaddwev_w_hu_h, 16, vmaddwev_w_hu_h)
gen_trans_vvvd(vmaddwev_d_wu_w, 16, vmaddwev_d_wu_w)
gen_trans_vvvd(vmaddwod_h_bu_b, 16, vmaddwod_h_bu_b)
gen_trans_vvvd(vmaddwod_w_hu_h, 16, vmaddwod_w_hu_h)
gen_trans_vvvd(vmaddwod_d_wu_w, 16, vmaddwod_d_wu_w)
static bool trans_vmaddwev_q_d(CPULoongArchState *env, arg_vmaddwev_q_d *restrict a) {
    int size = 16;
    int index = 0;
    CHECK_FPE(size);
    for (int i = 0; i < (size / 16); i++) {
        env->fpr[a->vd].vreg.Q[i] += (__int128_t)env->fpr[a->vj].vreg.D[i * 2 + index] * (__int128_t)env->fpr[a->vk].vreg.D[i *2 + index];
    }
    env->pc += 4;
    return true;
}
static bool trans_vmaddwod_q_d(CPULoongArchState *env, arg_vmaddwod_q_d *restrict a) {
    int size = 16;
    int index = 1;
    CHECK_FPE(size);
    for (int i = 0; i < (size / 16); i++) {
        env->fpr[a->vd].vreg.Q[i] += (__int128_t)env->fpr[a->vj].vreg.D[i * 2 + index] * (__int128_t)env->fpr[a->vk].vreg.D[i *2 + index];
    }
    env->pc += 4;
    return true;
}
static bool trans_vmaddwev_q_du(CPULoongArchState *env, arg_vmaddwev_q_du *restrict a) {
    int size = 16;
    int index = 0;
    CHECK_FPE(size);
    for (int i = 0; i < (size / 16); i++) {
        env->fpr[a->vd].vreg.Q[i] += (__uint128_t)env->fpr[a->vj].vreg.UD[i * 2 + index] * (__uint128_t)env->fpr[a->vk].vreg.UD[i *2 + index];
    }
    env->pc += 4;
    return true;
}
static bool trans_vmaddwod_q_du(CPULoongArchState *env, arg_vmaddwod_q_du *restrict a) {
    int size = 16;
    int index = 1;
    CHECK_FPE(size);
    for (int i = 0; i < (size / 16); i++) {
        env->fpr[a->vd].vreg.Q[i] += (__uint128_t)env->fpr[a->vj].vreg.UD[i * 2 + index] * (__uint128_t)env->fpr[a->vk].vreg.UD[i *2 + index];
    }
    env->pc += 4;
    return true;
}
static bool trans_vmaddwev_q_du_d(CPULoongArchState *env, arg_vmaddwev_q_du_d *restrict a) {
    int size = 16;
    int index = 0;
    CHECK_FPE(size);
    for (int i = 0; i < (size / 16); i++) {
        env->fpr[a->vd].vreg.Q[i] += (__uint128_t)env->fpr[a->vj].vreg.UD[i * 2 + index] * (__int128_t)env->fpr[a->vk].vreg.D[i *2 + index];
    }
    env->pc += 4;
    return true;
}
static bool trans_vmaddwod_q_du_d(CPULoongArchState *env, arg_vmaddwod_q_du_d *restrict a) {
    int size = 16;
    int index = 1;
    CHECK_FPE(size);
    for (int i = 0; i < (size / 16); i++) {
        env->fpr[a->vd].vreg.Q[i] += (__uint128_t)env->fpr[a->vj].vreg.UD[i * 2 + index] * (__int128_t)env->fpr[a->vk].vreg.D[i *2 + index];
    }
    env->pc += 4;
    return true;
}
static inline bool vdiv_b(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.B[i] = env->fpr[a->vj].vreg.B[i] / env->fpr[a->vk].vreg.B[i];}env->pc += 4;return true;}
static inline bool vdiv_h(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.H[i] = env->fpr[a->vj].vreg.H[i] / env->fpr[a->vk].vreg.H[i];}env->pc += 4;return true;}
static inline bool vdiv_w(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.W[i] = env->fpr[a->vj].vreg.W[i] / env->fpr[a->vk].vreg.W[i];}env->pc += 4;return true;}
static inline bool vdiv_d(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = env->fpr[a->vj].vreg.D[i] / env->fpr[a->vk].vreg.D[i];}env->pc += 4;return true;}
static bool trans_vdiv_b(CPULoongArchState *env, arg_vdiv_b *restrict a) {CHECK_FPE(16); return vdiv_b(env, a, 16);}
static bool trans_vdiv_h(CPULoongArchState *env, arg_vdiv_h *restrict a) {CHECK_FPE(16); return vdiv_h(env, a, 16);}
static bool trans_vdiv_w(CPULoongArchState *env, arg_vdiv_w *restrict a) {CHECK_FPE(16); return vdiv_w(env, a, 16);}
static bool trans_vdiv_d(CPULoongArchState *env, arg_vdiv_d *restrict a) {CHECK_FPE(16); return vdiv_d(env, a, 16);}
static bool trans_xvdiv_b(CPULoongArchState *env, arg_vdiv_b *restrict a) {CHECK_FPE(32); return vdiv_b(env, a, 32);}
static bool trans_xvdiv_h(CPULoongArchState *env, arg_vdiv_h *restrict a) {CHECK_FPE(32); return vdiv_h(env, a, 32);}
static bool trans_xvdiv_w(CPULoongArchState *env, arg_vdiv_w *restrict a) {CHECK_FPE(32); return vdiv_w(env, a, 32);}
static bool trans_xvdiv_d(CPULoongArchState *env, arg_vdiv_d *restrict a) {CHECK_FPE(32); return vdiv_d(env, a, 32);}
static inline bool vdiv_bu(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UB[i] = env->fpr[a->vj].vreg.UB[i] / env->fpr[a->vk].vreg.UB[i];}env->pc += 4;return true;}
static inline bool vdiv_hu(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UH[i] = env->fpr[a->vj].vreg.UH[i] / env->fpr[a->vk].vreg.UH[i];}env->pc += 4;return true;}
static inline bool vdiv_wu(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UW[i] = env->fpr[a->vj].vreg.UW[i] / env->fpr[a->vk].vreg.UW[i];}env->pc += 4;return true;}
static inline bool vdiv_du(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UD[i] = env->fpr[a->vj].vreg.UD[i] / env->fpr[a->vk].vreg.UD[i];}env->pc += 4;return true;}
static bool trans_vdiv_bu(CPULoongArchState *env, arg_vdiv_bu *restrict a) {CHECK_FPE(16); return vdiv_bu(env, a, 16);}
static bool trans_vdiv_hu(CPULoongArchState *env, arg_vdiv_hu *restrict a) {CHECK_FPE(16); return vdiv_hu(env, a, 16);}
static bool trans_vdiv_wu(CPULoongArchState *env, arg_vdiv_wu *restrict a) {CHECK_FPE(16); return vdiv_wu(env, a, 16);}
static bool trans_vdiv_du(CPULoongArchState *env, arg_vdiv_du *restrict a) {CHECK_FPE(16); return vdiv_du(env, a, 16);}
static bool trans_xvdiv_bu(CPULoongArchState *env, arg_vdiv_bu *restrict a) {CHECK_FPE(32); return vdiv_bu(env, a, 32);}
static bool trans_xvdiv_hu(CPULoongArchState *env, arg_vdiv_hu *restrict a) {CHECK_FPE(32); return vdiv_hu(env, a, 32);}
static bool trans_xvdiv_wu(CPULoongArchState *env, arg_vdiv_wu *restrict a) {CHECK_FPE(32); return vdiv_wu(env, a, 32);}
static bool trans_xvdiv_du(CPULoongArchState *env, arg_vdiv_du *restrict a) {CHECK_FPE(32); return vdiv_du(env, a, 32);}
static inline bool vmod_b(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.B[i] = env->fpr[a->vj].vreg.B[i] % env->fpr[a->vk].vreg.B[i];}env->pc += 4;return true;}
static inline bool vmod_h(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.H[i] = env->fpr[a->vj].vreg.H[i] % env->fpr[a->vk].vreg.H[i];}env->pc += 4;return true;}
static inline bool vmod_w(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.W[i] = env->fpr[a->vj].vreg.W[i] % env->fpr[a->vk].vreg.W[i];}env->pc += 4;return true;}
static inline bool vmod_d(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = env->fpr[a->vj].vreg.D[i] % env->fpr[a->vk].vreg.D[i];}env->pc += 4;return true;}
static bool trans_vmod_b(CPULoongArchState *env, arg_vmod_b *restrict a) {CHECK_FPE(16); return vmod_b(env, a, 16);}
static bool trans_vmod_h(CPULoongArchState *env, arg_vmod_h *restrict a) {CHECK_FPE(16); return vmod_h(env, a, 16);}
static bool trans_vmod_w(CPULoongArchState *env, arg_vmod_w *restrict a) {CHECK_FPE(16); return vmod_w(env, a, 16);}
static bool trans_vmod_d(CPULoongArchState *env, arg_vmod_d *restrict a) {CHECK_FPE(16); return vmod_d(env, a, 16);}
static bool trans_xvmod_b(CPULoongArchState *env, arg_vmod_b *restrict a) {CHECK_FPE(32); return vmod_b(env, a, 32);}
static bool trans_xvmod_h(CPULoongArchState *env, arg_vmod_h *restrict a) {CHECK_FPE(32); return vmod_h(env, a, 32);}
static bool trans_xvmod_w(CPULoongArchState *env, arg_vmod_w *restrict a) {CHECK_FPE(32); return vmod_w(env, a, 32);}
static bool trans_xvmod_d(CPULoongArchState *env, arg_vmod_d *restrict a) {CHECK_FPE(32); return vmod_d(env, a, 32);}
static inline bool vmod_bu(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UB[i] = env->fpr[a->vj].vreg.UB[i] % env->fpr[a->vk].vreg.UB[i];}env->pc += 4;return true;}
static inline bool vmod_hu(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UH[i] = env->fpr[a->vj].vreg.UH[i] % env->fpr[a->vk].vreg.UH[i];}env->pc += 4;return true;}
static inline bool vmod_wu(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UW[i] = env->fpr[a->vj].vreg.UW[i] % env->fpr[a->vk].vreg.UW[i];}env->pc += 4;return true;}
static inline bool vmod_du(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UD[i] = env->fpr[a->vj].vreg.UD[i] % env->fpr[a->vk].vreg.UD[i];}env->pc += 4;return true;}
static bool trans_vmod_bu(CPULoongArchState *env, arg_vmod_bu *restrict a) {CHECK_FPE(16); return vmod_bu(env, a, 16);}
static bool trans_vmod_hu(CPULoongArchState *env, arg_vmod_hu *restrict a) {CHECK_FPE(16); return vmod_hu(env, a, 16);}
static bool trans_vmod_wu(CPULoongArchState *env, arg_vmod_wu *restrict a) {CHECK_FPE(16); return vmod_wu(env, a, 16);}
static bool trans_vmod_du(CPULoongArchState *env, arg_vmod_du *restrict a) {CHECK_FPE(16); return vmod_du(env, a, 16);}
static bool trans_xvmod_bu(CPULoongArchState *env, arg_vmod_bu *restrict a) {CHECK_FPE(32); return vmod_bu(env, a, 32);}
static bool trans_xvmod_hu(CPULoongArchState *env, arg_vmod_hu *restrict a) {CHECK_FPE(32); return vmod_hu(env, a, 32);}
static bool trans_xvmod_wu(CPULoongArchState *env, arg_vmod_wu *restrict a) {CHECK_FPE(32); return vmod_wu(env, a, 32);}
static bool trans_xvmod_du(CPULoongArchState *env, arg_vmod_du *restrict a) {CHECK_FPE(32); return vmod_du(env, a, 32);}
#define gen_trans_vvid_sat(op, size, helper_name) \
static bool glue(trans_, op)(CPULoongArchState *env, arg_vv_i *restrict a) {      \
    CHECK_FPE(size);                                                    \
    int oprsz = size;                                                   \
    uint32_t desc = simd_desc(oprsz, oprsz, 0);                         \
    glue(helper_, helper_name)(&env->fpr[a->vd], &env->fpr[a->vj], (1ll << a->imm) - 1, desc);   \
    env->pc += 4;                                                       \
    return true;                                                        \
}
#define gen_trans_vvid_satu(op, size, helper_name) \
static bool glue(trans_, op)(CPULoongArchState *env, arg_vv_i *restrict a) {      \
    CHECK_FPE(size);                                                    \
    int oprsz = size;                                                   \
    uint32_t desc = simd_desc(oprsz, oprsz, 0);                         \
    uint64_t max = (a->imm == 0x3f) ? UINT64_MAX : (1ull << (a->imm + 1)) - 1; \
    glue(helper_, helper_name)(&env->fpr[a->vd], &env->fpr[a->vj], max, desc);   \
    env->pc += 4;                                                       \
    return true;                                                        \
}
gen_trans_vvid_sat(vsat_b, 16, vsat_b)
gen_trans_vvid_sat(vsat_h, 16, vsat_h)
gen_trans_vvid_sat(vsat_w, 16, vsat_w)
gen_trans_vvid_sat(vsat_d, 16, vsat_d)
gen_trans_vvid_satu(vsat_bu, 16, vsat_bu)
gen_trans_vvid_satu(vsat_hu, 16, vsat_hu)
gen_trans_vvid_satu(vsat_wu, 16, vsat_wu)
gen_trans_vvid_satu(vsat_du, 16, vsat_du)
#define gen_trans_vvd(op, size, helper_name) \
static bool glue(trans_, op)(CPULoongArchState *env, arg_vv *restrict a) {      \
    CHECK_FPE(size);                                                      \
    int oprsz = size;                                                   \
    uint32_t desc = simd_desc(oprsz, oprsz, 0);                         \
    glue(helper_, helper_name)(&env->fpr[a->vd], &env->fpr[a->vj], desc);   \
    env->pc += 4;                                                       \
    return true;                                                        \
}
gen_trans_vvd(vexth_h_b, 16, vexth_h_b)
gen_trans_vvd(vexth_w_h, 16, vexth_w_h)
gen_trans_vvd(vexth_d_w, 16, vexth_d_w)
gen_trans_vvd(vexth_q_d, 16, vexth_q_d)
gen_trans_vvd(vextl_q_d, 16, vextl_q_d)
gen_trans_vvd(vexth_hu_bu, 16, vexth_hu_bu)
gen_trans_vvd(vexth_wu_hu, 16, vexth_wu_hu)
gen_trans_vvd(vexth_du_wu, 16, vexth_du_wu)
gen_trans_vvd(vexth_qu_du, 16, vexth_qu_du)
gen_trans_vvd(vextl_qu_du, 16, vextl_qu_du)


gen_trans_vvvd(vsigncov_b, 16, vsigncov_b)
gen_trans_vvvd(vsigncov_h, 16, vsigncov_h)
gen_trans_vvvd(vsigncov_w, 16, vsigncov_w)
gen_trans_vvvd(vsigncov_d, 16, vsigncov_d)
gen_trans_vvd(vmskltz_b, 16, vmskltz_b)
gen_trans_vvd(vmskltz_h, 16, vmskltz_h)
gen_trans_vvd(vmskltz_w, 16, vmskltz_w)
gen_trans_vvd(vmskltz_d, 16, vmskltz_d)
static bool trans_vmskgez_b(CPULoongArchState *env, arg_vmskgez_b *restrict a) {
    CHECK_FPE(16);
    int oprsz = 16;
    uint32_t desc = simd_desc(oprsz, oprsz, 0);
    helper_vmskgez_b(&env->fpr[a->vd], &env->fpr[a->vj], desc);
    env->pc += 4;
    return true;
}
static bool trans_vmsknz_b(CPULoongArchState *env, arg_vmsknz_b *restrict a) {
    CHECK_FPE(16);
    int oprsz = 16;
    uint32_t desc = simd_desc(oprsz, oprsz, 0);
    helper_vmsknz_b(&env->fpr[a->vd], &env->fpr[a->vj], desc);
    env->pc += 4;
    return true;
}
#define EXPAND_BYTE(bit)  ((uint64_t)(bit ? 0xff : 0))

static uint64_t vldi_get_value(DisasContext *ctx, uint32_t imm)
{
    int mode;
    uint64_t data, t;

    /*
     * imm bit [11:8] is mode, mode value is 0-12.
     * other values are invalid.
     */
    mode = (imm >> 8) & 0xf;
    t =  imm & 0xff;
    switch (mode) {
    case 0:
        /* data: {2{24'0, imm[7:0]}} */
        data =  (t << 32) | t ;
        break;
    case 1:
        /* data: {2{16'0, imm[7:0], 8'0}} */
        data = (t << 24) | (t << 8);
        break;
    case 2:
        /* data: {2{8'0, imm[7:0], 16'0}} */
        data = (t << 48) | (t << 16);
        break;
    case 3:
        /* data: {2{imm[7:0], 24'0}} */
        data = (t << 56) | (t << 24);
        break;
    case 4:
        /* data: {4{8'0, imm[7:0]}} */
        data = (t << 48) | (t << 32) | (t << 16) | t;
        break;
    case 5:
        /* data: {4{imm[7:0], 8'0}} */
        data = (t << 56) |(t << 40) | (t << 24) | (t << 8);
        break;
    case 6:
        /* data: {2{16'0, imm[7:0], 8'1}} */
        data = (t << 40) | ((uint64_t)0xff << 32) | (t << 8) | 0xff;
        break;
    case 7:
        /* data: {2{8'0, imm[7:0], 16'1}} */
        data = (t << 48) | ((uint64_t)0xffff << 32) | (t << 16) | 0xffff;
        break;
    case 8:
        /* data: {8{imm[7:0]}} */
        data =(t << 56) | (t << 48) | (t << 40) | (t << 32) |
              (t << 24) | (t << 16) | (t << 8) | t;
        break;
    case 9:
        /* data: {{8{imm[7]}, ..., 8{imm[0]}}} */
        {
            uint64_t b0,b1,b2,b3,b4,b5,b6,b7;
            b0 = t& 0x1;
            b1 = (t & 0x2) >> 1;
            b2 = (t & 0x4) >> 2;
            b3 = (t & 0x8) >> 3;
            b4 = (t & 0x10) >> 4;
            b5 = (t & 0x20) >> 5;
            b6 = (t & 0x40) >> 6;
            b7 = (t & 0x80) >> 7;
            data = (EXPAND_BYTE(b7) << 56) |
                   (EXPAND_BYTE(b6) << 48) |
                   (EXPAND_BYTE(b5) << 40) |
                   (EXPAND_BYTE(b4) << 32) |
                   (EXPAND_BYTE(b3) << 24) |
                   (EXPAND_BYTE(b2) << 16) |
                   (EXPAND_BYTE(b1) <<  8) |
                   EXPAND_BYTE(b0);
        }
        break;
    case 10:
        /* data: {2{imm[7], ~imm[6], {5{imm[6]}}, imm[5:0], 19'0}} */
        {
            uint64_t b6, b7;
            uint64_t t0, t1;
            b6 = (imm & 0x40) >> 6;
            b7 = (imm & 0x80) >> 7;
            t0 = (imm & 0x3f);
            t1 = (b7 << 6) | ((1-b6) << 5) | (uint64_t)(b6 ? 0x1f : 0);
            data  = (t1 << 57) | (t0 << 51) | (t1 << 25) | (t0 << 19);
        }
        break;
    case 11:
        /* data: {32'0, imm[7], ~{imm[6]}, 5{imm[6]}, imm[5:0], 19'0} */
        {
            uint64_t b6,b7;
            uint64_t t0, t1;
            b6 = (imm & 0x40) >> 6;
            b7 = (imm & 0x80) >> 7;
            t0 = (imm & 0x3f);
            t1 = (b7 << 6) | ((1-b6) << 5) | (b6 ? 0x1f : 0);
            data = (t1 << 25) | (t0 << 19);
        }
        break;
    case 12:
        /* data: {imm[7], ~imm[6], 8{imm[6]}, imm[5:0], 48'0} */
        {
            uint64_t b6,b7;
            uint64_t t0, t1;
            b6 = (imm & 0x40) >> 6;
            b7 = (imm & 0x80) >> 7;
            t0 = (imm & 0x3f);
            t1 = (b7 << 9) | ((1-b6) << 8) | (b6 ? 0xff : 0);
            data = (t1 << 54) | (t0 << 48);
        }
        break;
    default:
        g_assert_not_reached();
    }
    return data;
}
static bool vldi(CPULoongArchState *env, arg_vldi *restrict a, uint32_t vlen) {
    int sel;
    uint64_t value;
    uint32_t ele_cnt = vlen / 8;

    sel = (a->imm >> 12) & 0x1;

    if (sel) {
        value = vldi_get_value(ctx, a->imm);
        for (uint32_t i = 0; i < ele_cnt; i++) {
            env->fpr[a->vd].vreg.UD[i] = value;
        }
    } else {
        value = ((int32_t)(a->imm << 22)) >> 22;
        switch ((a->imm >> 10) & 0x3) {
            case 0: for (uint32_t i = 0; i < (ele_cnt * 8); i++) {env->fpr[a->vd].vreg.B[i] = value;} break;
            case 1: for (uint32_t i = 0; i < (ele_cnt * 4); i++) {env->fpr[a->vd].vreg.H[i] = value;} break;
            case 2: for (uint32_t i = 0; i < (ele_cnt * 2); i++) {env->fpr[a->vd].vreg.W[i] = value;} break;
            case 3: for (uint32_t i = 0; i < (ele_cnt * 1); i++) {env->fpr[a->vd].vreg.D[i] = value;} break;
            default: g_assert_not_reached();
        }
    }
    env->pc += 4;
    return true;
}

static inline bool trans_vldi(CPULoongArchState *env, arg_vldi *restrict a) {CHECK_FPE(16); return vldi(env, a, 16); }
static inline bool trans_xvldi(CPULoongArchState *env, arg_vldi *restrict a) {CHECK_FPE(32); return vldi(env, a, 32); }

static inline bool vand_v(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = env->fpr[a->vj].vreg.D[i] & env->fpr[a->vk].vreg.D[i] ;}env->pc += 4;return true;}
static inline bool vor_v(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = env->fpr[a->vj].vreg.D[i] | env->fpr[a->vk].vreg.D[i] ;}env->pc += 4;return true;}
static inline bool vxor_v(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = env->fpr[a->vj].vreg.D[i] ^ env->fpr[a->vk].vreg.D[i] ;}env->pc += 4;return true;}
static inline bool vnor_v(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = ~(env->fpr[a->vj].vreg.D[i] | env->fpr[a->vk].vreg.D[i]) ;}env->pc += 4;return true;}
static inline bool vandn_v(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = (~env->fpr[a->vj].vreg.D[i]) & env->fpr[a->vk].vreg.D[i] ;}env->pc += 4;return true;}
static inline bool vorn_v(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = env->fpr[a->vj].vreg.D[i] | (~env->fpr[a->vk].vreg.D[i]) ;}env->pc += 4;return true;}
static bool trans_vand_v(CPULoongArchState *env, arg_vand_v *restrict a) {CHECK_FPE(16); return vand_v(env, a, 16);}
static bool trans_vor_v(CPULoongArchState *env, arg_vor_v *restrict a) {CHECK_FPE(16); return vor_v(env, a, 16);}
static bool trans_vxor_v(CPULoongArchState *env, arg_vxor_v *restrict a) {CHECK_FPE(16); return vxor_v(env, a, 16);}
static bool trans_vnor_v(CPULoongArchState *env, arg_vnor_v *restrict a) {CHECK_FPE(16); return vnor_v(env, a, 16);}
static bool trans_vandn_v(CPULoongArchState *env, arg_vandn_v *restrict a) {CHECK_FPE(16); return vandn_v(env, a, 16);}
static bool trans_vorn_v(CPULoongArchState *env, arg_vorn_v *restrict a) {CHECK_FPE(16); return vorn_v(env, a, 16);}
static bool trans_xvand_v(CPULoongArchState *env, arg_vand_v *restrict a) {CHECK_FPE(32); return vand_v(env, a, 32);}
static bool trans_xvor_v(CPULoongArchState *env, arg_vor_v *restrict a) {CHECK_FPE(32); return vor_v(env, a, 32);}
static bool trans_xvxor_v(CPULoongArchState *env, arg_vxor_v *restrict a) {CHECK_FPE(32); return vxor_v(env, a, 32);}
static bool trans_xvnor_v(CPULoongArchState *env, arg_vnor_v *restrict a) {CHECK_FPE(32); return vnor_v(env, a, 32);}
static bool trans_xvandn_v(CPULoongArchState *env, arg_vandn_v *restrict a) {CHECK_FPE(32); return vandn_v(env, a, 32);}
static bool trans_xvorn_v(CPULoongArchState *env, arg_vorn_v *restrict a) {CHECK_FPE(32); return vorn_v(env, a, 32);}
static inline bool vandi_b(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.B[i] = env->fpr[a->vj].vreg.B[i] & a->imm;}env->pc += 4;return true;}
static inline bool vori_b(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.B[i] = env->fpr[a->vj].vreg.B[i] | a->imm;}env->pc += 4;return true;}
static inline bool vxori_b(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.B[i] = env->fpr[a->vj].vreg.B[i] ^ a->imm;}env->pc += 4;return true;}
static inline bool vnori_b(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.B[i] = ~(env->fpr[a->vj].vreg.B[i] | a->imm);}env->pc += 4;return true;}
static bool trans_vandi_b(CPULoongArchState *env, arg_vandi_b *restrict a) {CHECK_FPE(16); return vandi_b(env, a, 16);}
static bool trans_vori_b(CPULoongArchState *env, arg_vori_b *restrict a) {CHECK_FPE(16); return vori_b(env, a, 16);}
static bool trans_vxori_b(CPULoongArchState *env, arg_vxori_b *restrict a) {CHECK_FPE(16); return vxori_b(env, a, 16);}
static bool trans_vnori_b(CPULoongArchState *env, arg_vnori_b *restrict a) {CHECK_FPE(16); return vnori_b(env, a, 16);}
static bool trans_xvandi_b(CPULoongArchState *env, arg_vandi_b *restrict a) {CHECK_FPE(32); return vandi_b(env, a, 32);}
static bool trans_xvori_b(CPULoongArchState *env, arg_vori_b *restrict a) {CHECK_FPE(32); return vori_b(env, a, 32);}
static bool trans_xvxori_b(CPULoongArchState *env, arg_vxori_b *restrict a) {CHECK_FPE(32); return vxori_b(env, a, 32);}
static bool trans_xvnori_b(CPULoongArchState *env, arg_vnori_b *restrict a) {CHECK_FPE(32); return vnori_b(env, a, 32);}
static inline bool vsll_b(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.B[i] = env->fpr[a->vj].vreg.B[i] << (env->fpr[a->vk].vreg.B[i] & 0x7);}env->pc += 4;return true;}
static inline bool vsll_h(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.H[i] = env->fpr[a->vj].vreg.H[i] << (env->fpr[a->vk].vreg.H[i] & 0xf);}env->pc += 4;return true;}
static inline bool vsll_w(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.W[i] = env->fpr[a->vj].vreg.W[i] << (env->fpr[a->vk].vreg.W[i] & 0x1f);}env->pc += 4;return true;}
static inline bool vsll_d(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = env->fpr[a->vj].vreg.D[i] << (env->fpr[a->vk].vreg.D[i] & 0x3f);}env->pc += 4;return true;}
static bool trans_vsll_b(CPULoongArchState *env, arg_vsll_b *restrict a) {CHECK_FPE(16); return vsll_b(env, a, 16);}
static bool trans_vsll_h(CPULoongArchState *env, arg_vsll_h *restrict a) {CHECK_FPE(16); return vsll_h(env, a, 16);}
static bool trans_vsll_w(CPULoongArchState *env, arg_vsll_w *restrict a) {CHECK_FPE(16); return vsll_w(env, a, 16);}
static bool trans_vsll_d(CPULoongArchState *env, arg_vsll_d *restrict a) {CHECK_FPE(16); return vsll_d(env, a, 16);}
static bool trans_xvsll_b(CPULoongArchState *env, arg_vsll_b *restrict a) {CHECK_FPE(32); return vsll_b(env, a, 32);}
static bool trans_xvsll_h(CPULoongArchState *env, arg_vsll_h *restrict a) {CHECK_FPE(32); return vsll_h(env, a, 32);}
static bool trans_xvsll_w(CPULoongArchState *env, arg_vsll_w *restrict a) {CHECK_FPE(32); return vsll_w(env, a, 32);}
static bool trans_xvsll_d(CPULoongArchState *env, arg_vsll_d *restrict a) {CHECK_FPE(32); return vsll_d(env, a, 32);}
static inline bool vslli_b(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.B[i] = env->fpr[a->vj].vreg.B[i] << a->imm;}env->pc += 4;return true;}
static inline bool vslli_h(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.H[i] = env->fpr[a->vj].vreg.H[i] << a->imm;}env->pc += 4;return true;}
static inline bool vslli_w(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.W[i] = env->fpr[a->vj].vreg.W[i] << a->imm;}env->pc += 4;return true;}
static inline bool vslli_d(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = env->fpr[a->vj].vreg.D[i] << a->imm;}env->pc += 4;return true;}
static bool trans_vslli_b(CPULoongArchState *env, arg_vslli_b *restrict a) {CHECK_FPE(16); return vslli_b(env, a, 16);}
static bool trans_vslli_h(CPULoongArchState *env, arg_vslli_h *restrict a) {CHECK_FPE(16); return vslli_h(env, a, 16);}
static bool trans_vslli_w(CPULoongArchState *env, arg_vslli_w *restrict a) {CHECK_FPE(16); return vslli_w(env, a, 16);}
static bool trans_vslli_d(CPULoongArchState *env, arg_vslli_d *restrict a) {CHECK_FPE(16); return vslli_d(env, a, 16);}
static bool trans_xvslli_b(CPULoongArchState *env, arg_vslli_b *restrict a) {CHECK_FPE(32); return vslli_b(env, a, 32);}
static bool trans_xvslli_h(CPULoongArchState *env, arg_vslli_h *restrict a) {CHECK_FPE(32); return vslli_h(env, a, 32);}
static bool trans_xvslli_w(CPULoongArchState *env, arg_vslli_w *restrict a) {CHECK_FPE(32); return vslli_w(env, a, 32);}
static bool trans_xvslli_d(CPULoongArchState *env, arg_vslli_d *restrict a) {CHECK_FPE(32); return vslli_d(env, a, 32);}
static inline bool vsrl_b(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UB[i] = env->fpr[a->vj].vreg.UB[i] >> (env->fpr[a->vk].vreg.B[i] & 0x7);}env->pc += 4;return true;}
static inline bool vsrl_h(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UH[i] = env->fpr[a->vj].vreg.UH[i] >> (env->fpr[a->vk].vreg.H[i] & 0xf);}env->pc += 4;return true;}
static inline bool vsrl_w(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UW[i] = env->fpr[a->vj].vreg.UW[i] >> (env->fpr[a->vk].vreg.W[i] & 0x1f);}env->pc += 4;return true;}
static inline bool vsrl_d(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UD[i] = env->fpr[a->vj].vreg.UD[i] >> (env->fpr[a->vk].vreg.D[i] & 0x3f);}env->pc += 4;return true;}
static bool trans_vsrl_b(CPULoongArchState *env, arg_vsrl_b *restrict a) {CHECK_FPE(16); return vsrl_b(env, a, 16);}
static bool trans_vsrl_h(CPULoongArchState *env, arg_vsrl_h *restrict a) {CHECK_FPE(16); return vsrl_h(env, a, 16);}
static bool trans_vsrl_w(CPULoongArchState *env, arg_vsrl_w *restrict a) {CHECK_FPE(16); return vsrl_w(env, a, 16);}
static bool trans_vsrl_d(CPULoongArchState *env, arg_vsrl_d *restrict a) {CHECK_FPE(16); return vsrl_d(env, a, 16);}
static bool trans_xvsrl_b(CPULoongArchState *env, arg_vsrl_b *restrict a) {CHECK_FPE(32); return vsrl_b(env, a, 32);}
static bool trans_xvsrl_h(CPULoongArchState *env, arg_vsrl_h *restrict a) {CHECK_FPE(32); return vsrl_h(env, a, 32);}
static bool trans_xvsrl_w(CPULoongArchState *env, arg_vsrl_w *restrict a) {CHECK_FPE(32); return vsrl_w(env, a, 32);}
static bool trans_xvsrl_d(CPULoongArchState *env, arg_vsrl_d *restrict a) {CHECK_FPE(32); return vsrl_d(env, a, 32);}
static inline bool vsrli_b(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UB[i] = env->fpr[a->vj].vreg.UB[i] >> a->imm;}env->pc += 4;return true;}
static inline bool vsrli_h(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UH[i] = env->fpr[a->vj].vreg.UH[i] >> a->imm;}env->pc += 4;return true;}
static inline bool vsrli_w(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UW[i] = env->fpr[a->vj].vreg.UW[i] >> a->imm;}env->pc += 4;return true;}
static inline bool vsrli_d(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UD[i] = env->fpr[a->vj].vreg.UD[i] >> a->imm;}env->pc += 4;return true;}
static bool trans_vsrli_b(CPULoongArchState *env, arg_vsrli_b *restrict a) {CHECK_FPE(16); return vsrli_b(env, a, 16);}
static bool trans_vsrli_h(CPULoongArchState *env, arg_vsrli_h *restrict a) {CHECK_FPE(16); return vsrli_h(env, a, 16);}
static bool trans_vsrli_w(CPULoongArchState *env, arg_vsrli_w *restrict a) {CHECK_FPE(16); return vsrli_w(env, a, 16);}
static bool trans_vsrli_d(CPULoongArchState *env, arg_vsrli_d *restrict a) {CHECK_FPE(16); return vsrli_d(env, a, 16);}
static bool trans_xvsrli_b(CPULoongArchState *env, arg_vsrli_b *restrict a) {CHECK_FPE(32); return vsrli_b(env, a, 32);}
static bool trans_xvsrli_h(CPULoongArchState *env, arg_vsrli_h *restrict a) {CHECK_FPE(32); return vsrli_h(env, a, 32);}
static bool trans_xvsrli_w(CPULoongArchState *env, arg_vsrli_w *restrict a) {CHECK_FPE(32); return vsrli_w(env, a, 32);}
static bool trans_xvsrli_d(CPULoongArchState *env, arg_vsrli_d *restrict a) {CHECK_FPE(32); return vsrli_d(env, a, 32);}
static inline bool vsra_b(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.B[i] = env->fpr[a->vj].vreg.B[i] >> (env->fpr[a->vk].vreg.B[i] & 0x7);}env->pc += 4;return true;}
static inline bool vsra_h(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.H[i] = env->fpr[a->vj].vreg.H[i] >> (env->fpr[a->vk].vreg.H[i] & 0xf);}env->pc += 4;return true;}
static inline bool vsra_w(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.W[i] = env->fpr[a->vj].vreg.W[i] >> (env->fpr[a->vk].vreg.W[i] & 0x1f);}env->pc += 4;return true;}
static inline bool vsra_d(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = env->fpr[a->vj].vreg.D[i] >> (env->fpr[a->vk].vreg.D[i] & 0x3f);}env->pc += 4;return true;}
static bool trans_vsra_b(CPULoongArchState *env, arg_vsra_b *restrict a) {CHECK_FPE(16); return vsra_b(env, a, 16);}
static bool trans_vsra_h(CPULoongArchState *env, arg_vsra_h *restrict a) {CHECK_FPE(16); return vsra_h(env, a, 16);}
static bool trans_vsra_w(CPULoongArchState *env, arg_vsra_w *restrict a) {CHECK_FPE(16); return vsra_w(env, a, 16);}
static bool trans_vsra_d(CPULoongArchState *env, arg_vsra_d *restrict a) {CHECK_FPE(16); return vsra_d(env, a, 16);}

static bool trans_xvsra_b(CPULoongArchState *env, arg_vsra_b *restrict a) {CHECK_FPE(32); return vsra_b(env, a, 32);}
static bool trans_xvsra_h(CPULoongArchState *env, arg_vsra_h *restrict a) {CHECK_FPE(32); return vsra_h(env, a, 32);}
static bool trans_xvsra_w(CPULoongArchState *env, arg_vsra_w *restrict a) {CHECK_FPE(32); return vsra_w(env, a, 32);}
static bool trans_xvsra_d(CPULoongArchState *env, arg_vsra_d *restrict a) {CHECK_FPE(32); return vsra_d(env, a, 32);}
static inline bool vsrai_b(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.B[i] = env->fpr[a->vj].vreg.B[i] >> a->imm;}env->pc += 4;return true;}
static inline bool vsrai_h(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.H[i] = env->fpr[a->vj].vreg.H[i] >> a->imm;}env->pc += 4;return true;}
static inline bool vsrai_w(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.W[i] = env->fpr[a->vj].vreg.W[i] >> a->imm;}env->pc += 4;return true;}
static inline bool vsrai_d(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = env->fpr[a->vj].vreg.D[i] >> a->imm;}env->pc += 4;return true;}
static bool trans_vsrai_b(CPULoongArchState *env, arg_vsrai_b *restrict a) {CHECK_FPE(16); return vsrai_b(env, a, 16);}
static bool trans_vsrai_h(CPULoongArchState *env, arg_vsrai_h *restrict a) {CHECK_FPE(16); return vsrai_h(env, a, 16);}
static bool trans_vsrai_w(CPULoongArchState *env, arg_vsrai_w *restrict a) {CHECK_FPE(16); return vsrai_w(env, a, 16);}
static bool trans_vsrai_d(CPULoongArchState *env, arg_vsrai_d *restrict a) {CHECK_FPE(16); return vsrai_d(env, a, 16);}
static bool trans_xvsrai_b(CPULoongArchState *env, arg_vsrai_b *restrict a) {CHECK_FPE(32); return vsrai_b(env, a, 32);}
static bool trans_xvsrai_h(CPULoongArchState *env, arg_vsrai_h *restrict a) {CHECK_FPE(32); return vsrai_h(env, a, 32);}
static bool trans_xvsrai_w(CPULoongArchState *env, arg_vsrai_w *restrict a) {CHECK_FPE(32); return vsrai_w(env, a, 32);}
static bool trans_xvsrai_d(CPULoongArchState *env, arg_vsrai_d *restrict a) {CHECK_FPE(32); return vsrai_d(env, a, 32);}
gen_trans_vvvd(vrotr_b, 16, gvec_rotr8v)
gen_trans_vvvd(vrotr_h, 16, gvec_rotr16v)
gen_trans_vvvd(vrotr_w, 16, gvec_rotr32v)
gen_trans_vvvd(vrotr_d, 16, gvec_rotr64v)
static bool trans_vrotri_b(CPULoongArchState *env, arg_vrotri_b *restrict a) {
    int size = 16;
    CHECK_FPE(size);
    int oprsz = size;
    uint32_t desc = simd_desc(oprsz, oprsz, 8 - a->imm);
    helper_gvec_rotl8i(&env->fpr[a->vd], &env->fpr[a->vj], desc);
    env->pc += 4;
    return true;
}
static bool trans_vrotri_h(CPULoongArchState *env, arg_vrotri_h *restrict a) {
    int size = 16;
    CHECK_FPE(size);
    int oprsz = size;
    uint32_t desc = simd_desc(oprsz, oprsz, 16 - a->imm);
    helper_gvec_rotl16i(&env->fpr[a->vd], &env->fpr[a->vj], desc);
    env->pc += 4;
    return true;
}
static bool trans_vrotri_w(CPULoongArchState *env, arg_vrotri_w *restrict a) {
    int size = 16;
    CHECK_FPE(size);
    int oprsz = size;
    uint32_t desc = simd_desc(oprsz, oprsz, 32 - a->imm);
    helper_gvec_rotl32i(&env->fpr[a->vd], &env->fpr[a->vj], desc);
    env->pc += 4;
    return true;
}
static bool trans_vrotri_d(CPULoongArchState *env, arg_vrotri_d *restrict a) {
    int size = 16;
    CHECK_FPE(size);
    int oprsz = size;
    uint32_t desc = simd_desc(oprsz, oprsz, 64 - a->imm);
    helper_gvec_rotl64i(&env->fpr[a->vd], &env->fpr[a->vj], desc);
    env->pc += 4;
    return true;
}
gen_trans_vvid(vsllwil_h_b, 16, vsllwil_h_b)
gen_trans_vvid(vsllwil_w_h, 16, vsllwil_w_h)
gen_trans_vvid(vsllwil_d_w, 16, vsllwil_d_w)
gen_trans_vvid(vsllwil_hu_bu, 16, vsllwil_hu_bu)
gen_trans_vvid(vsllwil_wu_hu, 16, vsllwil_wu_hu)
gen_trans_vvid(vsllwil_du_wu, 16, vsllwil_du_wu)
gen_trans_vvvd(vsrlr_b, 16, vsrlr_b)
gen_trans_vvvd(vsrlr_h, 16, vsrlr_h)
gen_trans_vvvd(vsrlr_w, 16, vsrlr_w)
gen_trans_vvvd(vsrlr_d, 16, vsrlr_d)
gen_trans_vvid(vsrlri_b, 16, vsrlri_b)
gen_trans_vvid(vsrlri_h, 16, vsrlri_h)
gen_trans_vvid(vsrlri_w, 16, vsrlri_w)
gen_trans_vvid(vsrlri_d, 16, vsrlri_d)
gen_trans_vvvd(vsrar_b, 16, vsrar_b)
gen_trans_vvvd(vsrar_h, 16, vsrar_h)
gen_trans_vvvd(vsrar_w, 16, vsrar_w)
gen_trans_vvvd(vsrar_d, 16, vsrar_d)
gen_trans_vvid(vsrari_b, 16, vsrari_b)
gen_trans_vvid(vsrari_h, 16, vsrari_h)
gen_trans_vvid(vsrari_w, 16, vsrari_w)
gen_trans_vvid(vsrari_d, 16, vsrari_d)

gen_trans_vvvd(vsrln_b_h, 16, vsrln_b_h)
gen_trans_vvvd(vsrln_h_w, 16, vsrln_h_w)
gen_trans_vvvd(vsrln_w_d, 16, vsrln_w_d)
gen_trans_vvvd(vsran_b_h, 16, vsran_b_h)
gen_trans_vvvd(vsran_h_w, 16, vsran_h_w)
gen_trans_vvvd(vsran_w_d, 16, vsran_w_d)

gen_trans_vvid(vsrlni_b_h, 16, vsrlni_b_h)
gen_trans_vvid(vsrlni_h_w, 16, vsrlni_h_w)
gen_trans_vvid(vsrlni_w_d, 16, vsrlni_w_d)
gen_trans_vvid(vsrlni_d_q, 16, vsrlni_d_q)
gen_trans_vvid(vsrani_b_h, 16, vsrani_b_h)
gen_trans_vvid(vsrani_h_w, 16, vsrani_h_w)
gen_trans_vvid(vsrani_w_d, 16, vsrani_w_d)
gen_trans_vvid(vsrani_d_q, 16, vsrani_d_q)


gen_trans_vvvd(vsrlrn_b_h, 16, vsrlrn_b_h)
gen_trans_vvvd(vsrlrn_h_w, 16, vsrlrn_h_w)
gen_trans_vvvd(vsrlrn_w_d, 16, vsrlrn_w_d)
gen_trans_vvvd(vsrarn_b_h, 16, vsrarn_b_h)
gen_trans_vvvd(vsrarn_h_w, 16, vsrarn_h_w)
gen_trans_vvvd(vsrarn_w_d, 16, vsrarn_w_d)


gen_trans_vvid(vsrlrni_b_h, 16, vsrlrni_b_h)
gen_trans_vvid(vsrlrni_h_w, 16, vsrlrni_h_w)
gen_trans_vvid(vsrlrni_w_d, 16, vsrlrni_w_d)
gen_trans_vvid(vsrlrni_d_q, 16, vsrlrni_d_q)
gen_trans_vvid(vsrarni_b_h, 16, vsrarni_b_h)
gen_trans_vvid(vsrarni_h_w, 16, vsrarni_h_w)
gen_trans_vvid(vsrarni_w_d, 16, vsrarni_w_d)
gen_trans_vvid(vsrarni_d_q, 16, vsrarni_d_q)

gen_trans_vvvd(vssrln_b_h, 16, vssrln_b_h)
gen_trans_vvvd(vssrln_h_w, 16, vssrln_h_w)
gen_trans_vvvd(vssrln_w_d, 16, vssrln_w_d)
gen_trans_vvvd(vssran_b_h, 16, vssran_b_h)
gen_trans_vvvd(vssran_h_w, 16, vssran_h_w)
gen_trans_vvvd(vssran_w_d, 16, vssran_w_d)
gen_trans_vvvd(vssrln_bu_h, 16, vssrln_bu_h)
gen_trans_vvvd(vssrln_hu_w, 16, vssrln_hu_w)
gen_trans_vvvd(vssrln_wu_d, 16, vssrln_wu_d)
gen_trans_vvvd(vssran_bu_h, 16, vssran_bu_h)
gen_trans_vvvd(vssran_hu_w, 16, vssran_hu_w)
gen_trans_vvvd(vssran_wu_d, 16, vssran_wu_d)

gen_trans_vvid(vssrlni_b_h, 16, vssrlni_b_h)
gen_trans_vvid(vssrlni_h_w, 16, vssrlni_h_w)
gen_trans_vvid(vssrlni_w_d, 16, vssrlni_w_d)
gen_trans_vvid(vssrlni_d_q, 16, vssrlni_d_q)
gen_trans_vvid(vssrani_b_h, 16, vssrani_b_h)
gen_trans_vvid(vssrani_h_w, 16, vssrani_h_w)
gen_trans_vvid(vssrani_w_d, 16, vssrani_w_d)
gen_trans_vvid(vssrani_d_q, 16, vssrani_d_q)
gen_trans_vvid(vssrlni_bu_h, 16, vssrlni_bu_h)
gen_trans_vvid(vssrlni_hu_w, 16, vssrlni_hu_w)
gen_trans_vvid(vssrlni_wu_d, 16, vssrlni_wu_d)
gen_trans_vvid(vssrlni_du_q, 16, vssrlni_du_q)
gen_trans_vvid(vssrani_bu_h, 16, vssrani_bu_h)
gen_trans_vvid(vssrani_hu_w, 16, vssrani_hu_w)
gen_trans_vvid(vssrani_wu_d, 16, vssrani_wu_d)
gen_trans_vvid(vssrani_du_q, 16, vssrani_du_q)

gen_trans_vvvd(vssrlrn_b_h, 16, vssrlrn_b_h)
gen_trans_vvvd(vssrlrn_h_w, 16, vssrlrn_h_w)
gen_trans_vvvd(vssrlrn_w_d, 16, vssrlrn_w_d)
gen_trans_vvvd(vssrarn_b_h, 16, vssrarn_b_h)
gen_trans_vvvd(vssrarn_h_w, 16, vssrarn_h_w)
gen_trans_vvvd(vssrarn_w_d, 16, vssrarn_w_d)
gen_trans_vvvd(vssrlrn_bu_h, 16, vssrlrn_bu_h)
gen_trans_vvvd(vssrlrn_hu_w, 16, vssrlrn_hu_w)
gen_trans_vvvd(vssrlrn_wu_d, 16, vssrlrn_wu_d)
gen_trans_vvvd(vssrarn_bu_h, 16, vssrarn_bu_h)
gen_trans_vvvd(vssrarn_hu_w, 16, vssrarn_hu_w)
gen_trans_vvvd(vssrarn_wu_d, 16, vssrarn_wu_d)


gen_trans_vvid(vssrlrni_b_h, 16, vssrlrni_b_h)
gen_trans_vvid(vssrlrni_h_w, 16, vssrlrni_h_w)
gen_trans_vvid(vssrlrni_w_d, 16, vssrlrni_w_d)
gen_trans_vvid(vssrlrni_d_q, 16, vssrlrni_d_q)
gen_trans_vvid(vssrarni_b_h, 16, vssrarni_b_h)
gen_trans_vvid(vssrarni_h_w, 16, vssrarni_h_w)
gen_trans_vvid(vssrarni_w_d, 16, vssrarni_w_d)
gen_trans_vvid(vssrarni_d_q, 16, vssrarni_d_q)
gen_trans_vvid(vssrlrni_bu_h, 16, vssrlrni_bu_h)
gen_trans_vvid(vssrlrni_hu_w, 16, vssrlrni_hu_w)
gen_trans_vvid(vssrlrni_wu_d, 16, vssrlrni_wu_d)
gen_trans_vvid(vssrlrni_du_q, 16, vssrlrni_du_q)
gen_trans_vvid(vssrarni_bu_h, 16, vssrarni_bu_h)
gen_trans_vvid(vssrarni_hu_w, 16, vssrarni_hu_w)
gen_trans_vvid(vssrarni_wu_d, 16, vssrarni_wu_d)
gen_trans_vvid(vssrarni_du_q, 16, vssrarni_du_q)


gen_trans_vvd(vclo_b, 16, vclo_b)
gen_trans_vvd(vclo_h, 16, vclo_h)
gen_trans_vvd(vclo_w, 16, vclo_w)
gen_trans_vvd(vclo_d, 16, vclo_d)
gen_trans_vvd(vclz_b, 16, vclz_b)
gen_trans_vvd(vclz_h, 16, vclz_h)
gen_trans_vvd(vclz_w, 16, vclz_w)
gen_trans_vvd(vclz_d, 16, vclz_d)
gen_trans_vvd(vpcnt_b, 16, vpcnt_b)
gen_trans_vvd(vpcnt_h, 16, vpcnt_h)
gen_trans_vvd(vpcnt_w, 16, vpcnt_w)
gen_trans_vvd(vpcnt_d, 16, vpcnt_d)


static inline bool vbitclr_b(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UB[i] = env->fpr[a->vj].vreg.UB[i] & (~(1ull << (env->fpr[a->vk].vreg.UB[i] & 0x7)));}env->pc += 4;return true;}
static inline bool vbitclr_h(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UH[i] = env->fpr[a->vj].vreg.UH[i] & (~(1ull << (env->fpr[a->vk].vreg.UH[i] & 0xf)));}env->pc += 4;return true;}
static inline bool vbitclr_w(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UW[i] = env->fpr[a->vj].vreg.UW[i] & (~(1ull << (env->fpr[a->vk].vreg.UW[i] & 0x1f)));}env->pc += 4;return true;}
static inline bool vbitclr_d(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UD[i] = env->fpr[a->vj].vreg.UD[i] & (~(1ull << (env->fpr[a->vk].vreg.UD[i] & 0x3f)));}env->pc += 4;return true;}
static bool trans_vbitclr_b(CPULoongArchState *env, arg_vbitclr_b *restrict a) {CHECK_FPE(16); return vbitclr_b(env, a, 16);}
static bool trans_vbitclr_h(CPULoongArchState *env, arg_vbitclr_h *restrict a) {CHECK_FPE(16); return vbitclr_h(env, a, 16);}
static bool trans_vbitclr_w(CPULoongArchState *env, arg_vbitclr_w *restrict a) {CHECK_FPE(16); return vbitclr_w(env, a, 16);}
static bool trans_vbitclr_d(CPULoongArchState *env, arg_vbitclr_d *restrict a) {CHECK_FPE(16); return vbitclr_d(env, a, 16);}
static bool trans_xvbitclr_b(CPULoongArchState *env, arg_vbitclr_b *restrict a) {CHECK_FPE(32); return vbitclr_b(env, a, 32);}
static bool trans_xvbitclr_h(CPULoongArchState *env, arg_vbitclr_h *restrict a) {CHECK_FPE(32); return vbitclr_h(env, a, 32);}
static bool trans_xvbitclr_w(CPULoongArchState *env, arg_vbitclr_w *restrict a) {CHECK_FPE(32); return vbitclr_w(env, a, 32);}
static bool trans_xvbitclr_d(CPULoongArchState *env, arg_vbitclr_d *restrict a) {CHECK_FPE(32); return vbitclr_d(env, a, 32);}
static inline bool vbitclri_b(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UB[i] = env->fpr[a->vj].vreg.UB[i] & (~(1ull << a->imm));}env->pc += 4;return true;}
static inline bool vbitclri_h(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UH[i] = env->fpr[a->vj].vreg.UH[i] & (~(1ull << a->imm));}env->pc += 4;return true;}
static inline bool vbitclri_w(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UW[i] = env->fpr[a->vj].vreg.UW[i] & (~(1ull << a->imm));}env->pc += 4;return true;}
static inline bool vbitclri_d(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UD[i] = env->fpr[a->vj].vreg.UD[i] & (~(1ull << a->imm));}env->pc += 4;return true;}
static bool trans_vbitclri_b(CPULoongArchState *env, arg_vbitclri_b *restrict a) {CHECK_FPE(16); return vbitclri_b(env, a, 16);}
static bool trans_vbitclri_h(CPULoongArchState *env, arg_vbitclri_h *restrict a) {CHECK_FPE(16); return vbitclri_h(env, a, 16);}
static bool trans_vbitclri_w(CPULoongArchState *env, arg_vbitclri_w *restrict a) {CHECK_FPE(16); return vbitclri_w(env, a, 16);}
static bool trans_vbitclri_d(CPULoongArchState *env, arg_vbitclri_d *restrict a) {CHECK_FPE(16); return vbitclri_d(env, a, 16);}
static bool trans_xvbitclri_b(CPULoongArchState *env, arg_vbitclri_b *restrict a) {CHECK_FPE(32); return vbitclri_b(env, a, 32);}
static bool trans_xvbitclri_h(CPULoongArchState *env, arg_vbitclri_h *restrict a) {CHECK_FPE(32); return vbitclri_h(env, a, 32);}
static bool trans_xvbitclri_w(CPULoongArchState *env, arg_vbitclri_w *restrict a) {CHECK_FPE(32); return vbitclri_w(env, a, 32);}
static bool trans_xvbitclri_d(CPULoongArchState *env, arg_vbitclri_d *restrict a) {CHECK_FPE(32); return vbitclri_d(env, a, 32);}
static inline bool vbitset_b(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UB[i] = env->fpr[a->vj].vreg.UB[i] | ((1ull << (env->fpr[a->vk].vreg.UB[i] & 0x7)));}env->pc += 4;return true;}
static inline bool vbitset_h(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UH[i] = env->fpr[a->vj].vreg.UH[i] | ((1ull << (env->fpr[a->vk].vreg.UH[i] & 0xf)));}env->pc += 4;return true;}
static inline bool vbitset_w(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UW[i] = env->fpr[a->vj].vreg.UW[i] | ((1ull << (env->fpr[a->vk].vreg.UW[i] & 0x1f)));}env->pc += 4;return true;}
static inline bool vbitset_d(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UD[i] = env->fpr[a->vj].vreg.UD[i] | ((1ull << (env->fpr[a->vk].vreg.UD[i] & 0x3f)));}env->pc += 4;return true;}
static bool trans_vbitset_b(CPULoongArchState *env, arg_vbitset_b *restrict a) {CHECK_FPE(16); return vbitset_b(env, a, 16);}
static bool trans_vbitset_h(CPULoongArchState *env, arg_vbitset_h *restrict a) {CHECK_FPE(16); return vbitset_h(env, a, 16);}
static bool trans_vbitset_w(CPULoongArchState *env, arg_vbitset_w *restrict a) {CHECK_FPE(16); return vbitset_w(env, a, 16);}
static bool trans_vbitset_d(CPULoongArchState *env, arg_vbitset_d *restrict a) {CHECK_FPE(16); return vbitset_d(env, a, 16);}
static bool trans_xvbitset_b(CPULoongArchState *env, arg_vbitset_b *restrict a) {CHECK_FPE(32); return vbitset_b(env, a, 32);}
static bool trans_xvbitset_h(CPULoongArchState *env, arg_vbitset_h *restrict a) {CHECK_FPE(32); return vbitset_h(env, a, 32);}
static bool trans_xvbitset_w(CPULoongArchState *env, arg_vbitset_w *restrict a) {CHECK_FPE(32); return vbitset_w(env, a, 32);}
static bool trans_xvbitset_d(CPULoongArchState *env, arg_vbitset_d *restrict a) {CHECK_FPE(32); return vbitset_d(env, a, 32);}
static inline bool vbitseti_b(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UB[i] = env->fpr[a->vj].vreg.UB[i] | ((1ull << a->imm));}env->pc += 4;return true;}
static inline bool vbitseti_h(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UH[i] = env->fpr[a->vj].vreg.UH[i] | ((1ull << a->imm));}env->pc += 4;return true;}
static inline bool vbitseti_w(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UW[i] = env->fpr[a->vj].vreg.UW[i] | ((1ull << a->imm));}env->pc += 4;return true;}
static inline bool vbitseti_d(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UD[i] = env->fpr[a->vj].vreg.UD[i] | ((1ull << a->imm));}env->pc += 4;return true;}
static bool trans_vbitseti_b(CPULoongArchState *env, arg_vbitseti_b *restrict a) {CHECK_FPE(16); return vbitseti_b(env, a, 16);}
static bool trans_vbitseti_h(CPULoongArchState *env, arg_vbitseti_h *restrict a) {CHECK_FPE(16); return vbitseti_h(env, a, 16);}
static bool trans_vbitseti_w(CPULoongArchState *env, arg_vbitseti_w *restrict a) {CHECK_FPE(16); return vbitseti_w(env, a, 16);}
static bool trans_vbitseti_d(CPULoongArchState *env, arg_vbitseti_d *restrict a) {CHECK_FPE(16); return vbitseti_d(env, a, 16);}
static bool trans_xvbitseti_b(CPULoongArchState *env, arg_vbitseti_b *restrict a) {CHECK_FPE(32); return vbitseti_b(env, a, 32);}
static bool trans_xvbitseti_h(CPULoongArchState *env, arg_vbitseti_h *restrict a) {CHECK_FPE(32); return vbitseti_h(env, a, 32);}
static bool trans_xvbitseti_w(CPULoongArchState *env, arg_vbitseti_w *restrict a) {CHECK_FPE(32); return vbitseti_w(env, a, 32);}
static bool trans_xvbitseti_d(CPULoongArchState *env, arg_vbitseti_d *restrict a) {CHECK_FPE(32); return vbitseti_d(env, a, 32);}
static inline bool vbitrev_b(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UB[i] = env->fpr[a->vj].vreg.UB[i] ^ ((1ull << (env->fpr[a->vk].vreg.UB[i] & 0x7)));}env->pc += 4;return true;}
static inline bool vbitrev_h(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UH[i] = env->fpr[a->vj].vreg.UH[i] ^ ((1ull << (env->fpr[a->vk].vreg.UH[i] & 0xf)));}env->pc += 4;return true;}
static inline bool vbitrev_w(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UW[i] = env->fpr[a->vj].vreg.UW[i] ^ ((1ull << (env->fpr[a->vk].vreg.UW[i] & 0x1f)));}env->pc += 4;return true;}
static inline bool vbitrev_d(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UD[i] = env->fpr[a->vj].vreg.UD[i] ^ ((1ull << (env->fpr[a->vk].vreg.UD[i] & 0x3f)));}env->pc += 4;return true;}
static bool trans_vbitrev_b(CPULoongArchState *env, arg_vbitrev_b *restrict a) {CHECK_FPE(16); return vbitrev_b(env, a, 16);}
static bool trans_vbitrev_h(CPULoongArchState *env, arg_vbitrev_h *restrict a) {CHECK_FPE(16); return vbitrev_h(env, a, 16);}
static bool trans_vbitrev_w(CPULoongArchState *env, arg_vbitrev_w *restrict a) {CHECK_FPE(16); return vbitrev_w(env, a, 16);}
static bool trans_vbitrev_d(CPULoongArchState *env, arg_vbitrev_d *restrict a) {CHECK_FPE(16); return vbitrev_d(env, a, 16);}
static bool trans_xvbitrev_b(CPULoongArchState *env, arg_vbitrev_b *restrict a) {CHECK_FPE(32); return vbitrev_b(env, a, 32);}
static bool trans_xvbitrev_h(CPULoongArchState *env, arg_vbitrev_h *restrict a) {CHECK_FPE(32); return vbitrev_h(env, a, 32);}
static bool trans_xvbitrev_w(CPULoongArchState *env, arg_vbitrev_w *restrict a) {CHECK_FPE(32); return vbitrev_w(env, a, 32);}
static bool trans_xvbitrev_d(CPULoongArchState *env, arg_vbitrev_d *restrict a) {CHECK_FPE(32); return vbitrev_d(env, a, 32);}
static inline bool vbitrevi_b(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UB[i] = env->fpr[a->vj].vreg.UB[i] ^ (1 << a->imm);}env->pc += 4;return true;}
static inline bool vbitrevi_h(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UH[i] = env->fpr[a->vj].vreg.UH[i] ^ (1 << a->imm);}env->pc += 4;return true;}
static inline bool vbitrevi_w(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UW[i] = env->fpr[a->vj].vreg.UW[i] ^ (1 << a->imm);}env->pc += 4;return true;}
static inline bool vbitrevi_d(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UD[i] = env->fpr[a->vj].vreg.UD[i] ^ (1ull << a->imm);}env->pc += 4;return true;}
static bool trans_vbitrevi_b(CPULoongArchState *env, arg_vbitrevi_b *restrict a) {CHECK_FPE(16); return vbitrevi_b(env, a, 16);}
static bool trans_vbitrevi_h(CPULoongArchState *env, arg_vbitrevi_h *restrict a) {CHECK_FPE(16); return vbitrevi_h(env, a, 16);}
static bool trans_vbitrevi_w(CPULoongArchState *env, arg_vbitrevi_w *restrict a) {CHECK_FPE(16); return vbitrevi_w(env, a, 16);}
static bool trans_vbitrevi_d(CPULoongArchState *env, arg_vbitrevi_d *restrict a) {CHECK_FPE(16); return vbitrevi_d(env, a, 16);}
static bool trans_xvbitrevi_b(CPULoongArchState *env, arg_vbitrevi_b *restrict a) {CHECK_FPE(32); return vbitrevi_b(env, a, 32);}
static bool trans_xvbitrevi_h(CPULoongArchState *env, arg_vbitrevi_h *restrict a) {CHECK_FPE(32); return vbitrevi_h(env, a, 32);}
static bool trans_xvbitrevi_w(CPULoongArchState *env, arg_vbitrevi_w *restrict a) {CHECK_FPE(32); return vbitrevi_w(env, a, 32);}
static bool trans_xvbitrevi_d(CPULoongArchState *env, arg_vbitrevi_d *restrict a) {CHECK_FPE(32); return vbitrevi_d(env, a, 32);}
gen_trans_vvvd(vfrstp_b, 16, vfrstp_b)
gen_trans_vvvd(vfrstp_h, 16, vfrstp_h)
gen_trans_vvid(vfrstpi_b, 16, vfrstpi_b)
gen_trans_vvid(vfrstpi_h, 16, vfrstpi_h)

#define gen_trans_vvved(op, size, helper_name) \
static bool glue(trans_, op)(CPULoongArchState *env, arg_vvv *restrict a) {   \
    CHECK_FPE(size);                                                   \
    int oprsz = size;                                                   \
    uint32_t desc = simd_desc(oprsz, oprsz, 0);                         \
    glue(helper_, helper_name)(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], env, desc);          \
    env->pc += 4;                                                       \
    return true;                                                        \
}

gen_trans_vvved(vfadd_s, 16, vfadd_s)
gen_trans_vvved(vfadd_d, 16, vfadd_d)
gen_trans_vvved(vfsub_s, 16, vfsub_s)
gen_trans_vvved(vfsub_d, 16, vfsub_d)
gen_trans_vvved(vfmul_s, 16, vfmul_s)
gen_trans_vvved(vfmul_d, 16, vfmul_d)
gen_trans_vvved(vfdiv_s, 16, vfdiv_s)
gen_trans_vvved(vfdiv_d, 16, vfdiv_d)

gen_trans_vvved(xvfadd_s, 32, vfadd_s)
gen_trans_vvved(xvfadd_d, 32, vfadd_d)
gen_trans_vvved(xvfsub_s, 32, vfsub_s)
gen_trans_vvved(xvfsub_d, 32, vfsub_d)
gen_trans_vvved(xvfmul_s, 32, vfmul_s)
gen_trans_vvved(xvfmul_d, 32, vfmul_d)
gen_trans_vvved(xvfdiv_s, 32, vfdiv_s)
gen_trans_vvved(xvfdiv_d, 32, vfdiv_d)

#define gen_trans_vvvv(op, size, helper_name) \
static bool glue(trans_, op)(CPULoongArchState *env, arg_vvvv *restrict a) {   \
    CHECK_FPE(size);                                                   \
    int oprsz = size;                                                   \
    uint32_t desc = simd_desc(oprsz, oprsz, 0);                         \
    glue(helper_, helper_name)(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], &env->fpr[a->va], env, desc);          \
    env->pc += 4;                                                       \
    return true;                                                        \
}

gen_trans_vvvv(vfmadd_s, 16, vfmadd_s)
gen_trans_vvvv(vfmadd_d, 16, vfmadd_d)
gen_trans_vvvv(vfmsub_s, 16, vfmsub_s)
gen_trans_vvvv(vfmsub_d, 16, vfmsub_d)
gen_trans_vvvv(vfnmadd_s, 16, vfnmadd_s)
gen_trans_vvvv(vfnmadd_d, 16, vfnmadd_d)
gen_trans_vvvv(vfnmsub_s, 16, vfnmsub_s)
gen_trans_vvvv(vfnmsub_d, 16, vfnmsub_d)

gen_trans_vvved(vfmax_s, 16, vfmax_s)
gen_trans_vvved(vfmax_d, 16, vfmax_d)
gen_trans_vvved(vfmin_s, 16, vfmin_s)
gen_trans_vvved(vfmin_d, 16, vfmin_d)
gen_trans_vvved(vfmaxa_s, 16, vfmaxa_s)
gen_trans_vvved(vfmaxa_d, 16, vfmaxa_d)
gen_trans_vvved(vfmina_s, 16, vfmina_s)
gen_trans_vvved(vfmina_d, 16, vfmina_d)

gen_trans_vvvv(xvfmadd_s, 32, vfmadd_s)
gen_trans_vvvv(xvfmadd_d, 32, vfmadd_d)
gen_trans_vvvv(xvfmsub_s, 32, vfmsub_s)
gen_trans_vvvv(xvfmsub_d, 32, vfmsub_d)
gen_trans_vvvv(xvfnmadd_s, 32, vfnmadd_s)
gen_trans_vvvv(xvfnmadd_d, 32, vfnmadd_d)
gen_trans_vvvv(xvfnmsub_s, 32, vfnmsub_s)
gen_trans_vvvv(xvfnmsub_d, 32, vfnmsub_d)

gen_trans_vvved(xvfmax_s, 32, vfmax_s)
gen_trans_vvved(xvfmax_d, 32, vfmax_d)
gen_trans_vvved(xvfmin_s, 32, vfmin_s)
gen_trans_vvved(xvfmin_d, 32, vfmin_d)
gen_trans_vvved(xvfmaxa_s, 32, vfmaxa_s)
gen_trans_vvved(xvfmaxa_d, 32, vfmaxa_d)
gen_trans_vvved(xvfmina_s, 32, vfmina_s)
gen_trans_vvved(xvfmina_d, 32, vfmina_d)

#define gen_trans_vved(op, size, helper_name) \
static bool glue(trans_, op)(CPULoongArchState *env, arg_vv *restrict a) {      \
    CHECK_FPE(size);                                                   \
    int oprsz = size;                                                   \
    uint32_t desc = simd_desc(oprsz, oprsz, 0);                         \
    glue(helper_, helper_name)(&env->fpr[a->vd], &env->fpr[a->vj], env, desc);   \
    env->pc += 4;                                                       \
    return true;                                                        \
}
gen_trans_vved(vflogb_s, 16, vflogb_s)
gen_trans_vved(vflogb_d, 16, vflogb_d)
gen_trans_vved(vfclass_s, 16, vfclass_s)
gen_trans_vved(vfclass_d, 16, vfclass_d)
gen_trans_vved(vfsqrt_s, 16, vfsqrt_s)
gen_trans_vved(vfsqrt_d, 16, vfsqrt_d)
gen_trans_vved(vfrecip_s, 16, vfrecip_s)
gen_trans_vved(vfrecip_d, 16, vfrecip_d)
gen_trans_vved(vfrsqrt_s, 16, vfrsqrt_s)
gen_trans_vved(vfrsqrt_d, 16, vfrsqrt_d)

gen_trans_vved(vfcvtl_s_h, 16, vfcvtl_s_h)
gen_trans_vved(vfcvth_s_h, 16, vfcvth_s_h)
gen_trans_vved(vfcvtl_d_s, 16, vfcvtl_d_s)
gen_trans_vved(vfcvth_d_s, 16, vfcvth_d_s)

gen_trans_vvved(vfcvt_h_s, 16, vfcvt_h_s)
gen_trans_vvved(vfcvt_s_d, 16, vfcvt_s_d)

gen_trans_vved(vfrint_s, 16, vfrint_s)
gen_trans_vved(vfrint_d, 16, vfrint_d)
gen_trans_vved(vfrintrm_s, 16, vfrintrm_s)
gen_trans_vved(vfrintrm_d, 16, vfrintrm_d)
gen_trans_vved(vfrintrp_s, 16, vfrintrp_s)
gen_trans_vved(vfrintrp_d, 16, vfrintrp_d)
gen_trans_vved(vfrintrz_s, 16, vfrintrz_s)
gen_trans_vved(vfrintrz_d, 16, vfrintrz_d)
gen_trans_vved(vfrintrne_s, 16, vfrintrne_s)
gen_trans_vved(vfrintrne_d, 16, vfrintrne_d)
gen_trans_vved(vftint_w_s, 16, vftint_w_s)
gen_trans_vved(vftint_l_d, 16, vftint_l_d)
gen_trans_vved(vftintrm_w_s, 16, vftintrm_w_s)
gen_trans_vved(vftintrm_l_d, 16, vftintrm_l_d)
gen_trans_vved(vftintrp_w_s, 16, vftintrp_w_s)
gen_trans_vved(vftintrp_l_d, 16, vftintrp_l_d)
gen_trans_vved(vftintrz_w_s, 16, vftintrz_w_s)
gen_trans_vved(vftintrz_l_d, 16, vftintrz_l_d)
gen_trans_vved(vftintrne_w_s, 16, vftintrne_w_s)
gen_trans_vved(vftintrne_l_d, 16, vftintrne_l_d)
gen_trans_vved(vftint_wu_s, 16, vftint_wu_s)
gen_trans_vved(vftint_lu_d, 16, vftint_lu_d)
gen_trans_vved(vftintrz_wu_s, 16, vftintrz_wu_s)
gen_trans_vved(vftintrz_lu_d, 16, vftintrz_lu_d)
gen_trans_vvved(vftint_w_d, 16, vftint_w_d)
gen_trans_vvved(vftintrm_w_d, 16, vftintrm_w_d)
gen_trans_vvved(vftintrp_w_d, 16, vftintrp_w_d)
gen_trans_vvved(vftintrz_w_d, 16, vftintrz_w_d)
gen_trans_vvved(vftintrne_w_d, 16, vftintrne_w_d)
gen_trans_vved(vftintl_l_s, 16, vftintl_l_s)
gen_trans_vved(vftinth_l_s, 16, vftinth_l_s)
gen_trans_vved(vftintrml_l_s, 16, vftintrml_l_s)
gen_trans_vved(vftintrmh_l_s, 16, vftintrmh_l_s)
gen_trans_vved(vftintrpl_l_s, 16, vftintrpl_l_s)
gen_trans_vved(vftintrph_l_s, 16, vftintrph_l_s)
gen_trans_vved(vftintrzl_l_s, 16, vftintrzl_l_s)
gen_trans_vved(vftintrzh_l_s, 16, vftintrzh_l_s)
gen_trans_vved(vftintrnel_l_s, 16, vftintrnel_l_s)
gen_trans_vved(vftintrneh_l_s, 16, vftintrneh_l_s)

gen_trans_vved(vffint_s_w, 16, vffint_s_w)
gen_trans_vved(vffint_s_wu, 16, vffint_s_wu)
gen_trans_vved(vffint_d_l, 16, vffint_d_l)
gen_trans_vved(vffint_d_lu, 16, vffint_d_lu)
gen_trans_vved(vffintl_d_w, 16, vffintl_d_w)
gen_trans_vved(vffinth_d_w, 16, vffinth_d_w)
gen_trans_vvved(vffint_s_l, 16, vffint_s_l)


static inline bool vseq_b(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1; for (size_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.B[i] = env->fpr[a->vj].vreg.B[i] == env->fpr[a->vk].vreg.B[i] ? -1 : 0;}env->pc += 4;return true;}
static inline bool vseq_h(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2; for (size_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.H[i] = env->fpr[a->vj].vreg.H[i] == env->fpr[a->vk].vreg.H[i] ? -1 : 0;}env->pc += 4;return true;}
static inline bool vseq_w(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4; for (size_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.W[i] = env->fpr[a->vj].vreg.W[i] == env->fpr[a->vk].vreg.W[i] ? -1 : 0;}env->pc += 4;return true;}
static inline bool vseq_d(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8; for (size_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = env->fpr[a->vj].vreg.D[i] == env->fpr[a->vk].vreg.D[i] ? -1 : 0;}env->pc += 4;return true;}
static bool trans_vseq_b(CPULoongArchState *env, arg_vseq_b *restrict a) {CHECK_FPE(16); return vseq_b(env, a, 16);}
static bool trans_vseq_h(CPULoongArchState *env, arg_vseq_h *restrict a) {CHECK_FPE(16); return vseq_h(env, a, 16);}
static bool trans_vseq_w(CPULoongArchState *env, arg_vseq_w *restrict a) {CHECK_FPE(16); return vseq_w(env, a, 16);}
static bool trans_vseq_d(CPULoongArchState *env, arg_vseq_d *restrict a) {CHECK_FPE(16); return vseq_d(env, a, 16);}
static bool trans_xvseq_b(CPULoongArchState *env, arg_vseq_b *restrict a) {CHECK_FPE(32); return vseq_b(env, a, 32);}
static bool trans_xvseq_h(CPULoongArchState *env, arg_vseq_h *restrict a) {CHECK_FPE(32); return vseq_h(env, a, 32);}
static bool trans_xvseq_w(CPULoongArchState *env, arg_vseq_w *restrict a) {CHECK_FPE(32); return vseq_w(env, a, 32);}
static bool trans_xvseq_d(CPULoongArchState *env, arg_vseq_d *restrict a) {CHECK_FPE(32); return vseq_d(env, a, 32);}
static inline bool vseqi_b(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.B[i] = env->fpr[a->vj].vreg.B[i] == a->imm ? -1: 0;}env->pc += 4;return true;}
static inline bool vseqi_h(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.H[i] = env->fpr[a->vj].vreg.H[i] == a->imm ? -1: 0;}env->pc += 4;return true;}
static inline bool vseqi_w(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.W[i] = env->fpr[a->vj].vreg.W[i] == a->imm ? -1: 0;}env->pc += 4;return true;}
static inline bool vseqi_d(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = env->fpr[a->vj].vreg.D[i] == a->imm ? -1: 0;}env->pc += 4;return true;}
static bool trans_vseqi_b(CPULoongArchState *env, arg_vseqi_b *restrict a) {CHECK_FPE(16); return vseqi_b(env, a, 16);}
static bool trans_vseqi_h(CPULoongArchState *env, arg_vseqi_h *restrict a) {CHECK_FPE(16); return vseqi_h(env, a, 16);}
static bool trans_vseqi_w(CPULoongArchState *env, arg_vseqi_w *restrict a) {CHECK_FPE(16); return vseqi_w(env, a, 16);}
static bool trans_vseqi_d(CPULoongArchState *env, arg_vseqi_d *restrict a) {CHECK_FPE(16); return vseqi_d(env, a, 16);}

static bool trans_xvseqi_b(CPULoongArchState *env, arg_vseqi_b *restrict a) {CHECK_FPE(32); return vseqi_b(env, a, 32);}
static bool trans_xvseqi_h(CPULoongArchState *env, arg_vseqi_h *restrict a) {CHECK_FPE(32); return vseqi_h(env, a, 32);}
static bool trans_xvseqi_w(CPULoongArchState *env, arg_vseqi_w *restrict a) {CHECK_FPE(32); return vseqi_w(env, a, 32);}
static bool trans_xvseqi_d(CPULoongArchState *env, arg_vseqi_d *restrict a) {CHECK_FPE(32); return vseqi_d(env, a, 32);}
static inline bool vsle_b(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1; for (size_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.B[i] = env->fpr[a->vj].vreg.B[i] <= env->fpr[a->vk].vreg.B[i] ? -1 : 0;}env->pc += 4;return true;}
static inline bool vsle_h(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2; for (size_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.H[i] = env->fpr[a->vj].vreg.H[i] <= env->fpr[a->vk].vreg.H[i] ? -1 : 0;}env->pc += 4;return true;}
static inline bool vsle_w(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4; for (size_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.W[i] = env->fpr[a->vj].vreg.W[i] <= env->fpr[a->vk].vreg.W[i] ? -1 : 0;}env->pc += 4;return true;}
static inline bool vsle_d(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8; for (size_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = env->fpr[a->vj].vreg.D[i] <= env->fpr[a->vk].vreg.D[i] ? -1 : 0;}env->pc += 4;return true;}
static bool trans_vsle_b(CPULoongArchState *env, arg_vsle_b *restrict a) {CHECK_FPE(16); return vsle_b(env, a, 16);}
static bool trans_vsle_h(CPULoongArchState *env, arg_vsle_h *restrict a) {CHECK_FPE(16); return vsle_h(env, a, 16);}
static bool trans_vsle_w(CPULoongArchState *env, arg_vsle_w *restrict a) {CHECK_FPE(16); return vsle_w(env, a, 16);}
static bool trans_vsle_d(CPULoongArchState *env, arg_vsle_d *restrict a) {CHECK_FPE(16); return vsle_d(env, a, 16);}
static bool trans_xvsle_b(CPULoongArchState *env, arg_vsle_b *restrict a) {CHECK_FPE(32); return vsle_b(env, a, 32);}
static bool trans_xvsle_h(CPULoongArchState *env, arg_vsle_h *restrict a) {CHECK_FPE(32); return vsle_h(env, a, 32);}
static bool trans_xvsle_w(CPULoongArchState *env, arg_vsle_w *restrict a) {CHECK_FPE(32); return vsle_w(env, a, 32);}
static bool trans_xvsle_d(CPULoongArchState *env, arg_vsle_d *restrict a) {CHECK_FPE(32); return vsle_d(env, a, 32);}
static inline bool vslei_b(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.B[i] = env->fpr[a->vj].vreg.B[i] <= a->imm ? -1: 0;}env->pc += 4;return true;}
static inline bool vslei_h(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.H[i] = env->fpr[a->vj].vreg.H[i] <= a->imm ? -1: 0;}env->pc += 4;return true;}
static inline bool vslei_w(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.W[i] = env->fpr[a->vj].vreg.W[i] <= a->imm ? -1: 0;}env->pc += 4;return true;}
static inline bool vslei_d(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = env->fpr[a->vj].vreg.D[i] <= a->imm ? -1: 0;}env->pc += 4;return true;}
static bool trans_vslei_b(CPULoongArchState *env, arg_vslei_b *restrict a) {CHECK_FPE(16); return vslei_b(env, a, 16);}
static bool trans_vslei_h(CPULoongArchState *env, arg_vslei_h *restrict a) {CHECK_FPE(16); return vslei_h(env, a, 16);}
static bool trans_vslei_w(CPULoongArchState *env, arg_vslei_w *restrict a) {CHECK_FPE(16); return vslei_w(env, a, 16);}
static bool trans_vslei_d(CPULoongArchState *env, arg_vslei_d *restrict a) {CHECK_FPE(16); return vslei_d(env, a, 16);}
static bool trans_xvslei_b(CPULoongArchState *env, arg_vslei_b *restrict a) {CHECK_FPE(32); return vslei_b(env, a, 32);}
static bool trans_xvslei_h(CPULoongArchState *env, arg_vslei_h *restrict a) {CHECK_FPE(32); return vslei_h(env, a, 32);}
static bool trans_xvslei_w(CPULoongArchState *env, arg_vslei_w *restrict a) {CHECK_FPE(32); return vslei_w(env, a, 32);}
static bool trans_xvslei_d(CPULoongArchState *env, arg_vslei_d *restrict a) {CHECK_FPE(32); return vslei_d(env, a, 32);}
static inline bool vsle_bu(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1; for (size_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UB[i] = env->fpr[a->vj].vreg.UB[i] <= env->fpr[a->vk].vreg.UB[i] ? -1 : 0;}env->pc += 4;return true;}
static inline bool vsle_hu(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2; for (size_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UH[i] = env->fpr[a->vj].vreg.UH[i] <= env->fpr[a->vk].vreg.UH[i] ? -1 : 0;}env->pc += 4;return true;}
static inline bool vsle_wu(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4; for (size_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UW[i] = env->fpr[a->vj].vreg.UW[i] <= env->fpr[a->vk].vreg.UW[i] ? -1 : 0;}env->pc += 4;return true;}
static inline bool vsle_du(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8; for (size_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UD[i] = env->fpr[a->vj].vreg.UD[i] <= env->fpr[a->vk].vreg.UD[i] ? -1 : 0;}env->pc += 4;return true;}
static bool trans_vsle_bu(CPULoongArchState *env, arg_vsle_bu *restrict a) {CHECK_FPE(16); return vsle_bu(env, a, 16);}
static bool trans_vsle_hu(CPULoongArchState *env, arg_vsle_hu *restrict a) {CHECK_FPE(16); return vsle_hu(env, a, 16);}
static bool trans_vsle_wu(CPULoongArchState *env, arg_vsle_wu *restrict a) {CHECK_FPE(16); return vsle_wu(env, a, 16);}
static bool trans_vsle_du(CPULoongArchState *env, arg_vsle_du *restrict a) {CHECK_FPE(16); return vsle_du(env, a, 16);}
static bool trans_xvsle_bu(CPULoongArchState *env, arg_vsle_bu *restrict a) {CHECK_FPE(32); return vsle_bu(env, a, 32);}
static bool trans_xvsle_hu(CPULoongArchState *env, arg_vsle_hu *restrict a) {CHECK_FPE(32); return vsle_hu(env, a, 32);}
static bool trans_xvsle_wu(CPULoongArchState *env, arg_vsle_wu *restrict a) {CHECK_FPE(32); return vsle_wu(env, a, 32);}
static bool trans_xvsle_du(CPULoongArchState *env, arg_vsle_du *restrict a) {CHECK_FPE(32); return vsle_du(env, a, 32);}
static inline bool vslei_bu(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UB[i] = env->fpr[a->vj].vreg.UB[i] <= (uint64_t)a->imm ? -1: 0;}env->pc += 4;return true;}
static inline bool vslei_hu(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UH[i] = env->fpr[a->vj].vreg.UH[i] <= (uint64_t)a->imm ? -1: 0;}env->pc += 4;return true;}
static inline bool vslei_wu(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UW[i] = env->fpr[a->vj].vreg.UW[i] <= (uint64_t)a->imm ? -1: 0;}env->pc += 4;return true;}
static inline bool vslei_du(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UD[i] = env->fpr[a->vj].vreg.UD[i] <= (uint64_t)a->imm ? -1: 0;}env->pc += 4;return true;}
static bool trans_vslei_bu(CPULoongArchState *env, arg_vslei_bu *restrict a) {CHECK_FPE(16); return vslei_bu(env, a, 16);}
static bool trans_vslei_hu(CPULoongArchState *env, arg_vslei_hu *restrict a) {CHECK_FPE(16); return vslei_hu(env, a, 16);}
static bool trans_vslei_wu(CPULoongArchState *env, arg_vslei_wu *restrict a) {CHECK_FPE(16); return vslei_wu(env, a, 16);}
static bool trans_vslei_du(CPULoongArchState *env, arg_vslei_du *restrict a) {CHECK_FPE(16); return vslei_du(env, a, 16);}
static bool trans_xvslei_bu(CPULoongArchState *env, arg_vslei_bu *restrict a) {CHECK_FPE(32); return vslei_bu(env, a, 32);}
static bool trans_xvslei_hu(CPULoongArchState *env, arg_vslei_hu *restrict a) {CHECK_FPE(32); return vslei_hu(env, a, 32);}
static bool trans_xvslei_wu(CPULoongArchState *env, arg_vslei_wu *restrict a) {CHECK_FPE(32); return vslei_wu(env, a, 32);}
static bool trans_xvslei_du(CPULoongArchState *env, arg_vslei_du *restrict a) {CHECK_FPE(32); return vslei_du(env, a, 32);}
static inline bool vslt_b(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1; for (size_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.B[i] = env->fpr[a->vj].vreg.B[i] < env->fpr[a->vk].vreg.B[i] ? -1 : 0;}env->pc += 4;return true;}
static inline bool vslt_h(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2; for (size_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.H[i] = env->fpr[a->vj].vreg.H[i] < env->fpr[a->vk].vreg.H[i] ? -1 : 0;}env->pc += 4;return true;}
static inline bool vslt_w(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4; for (size_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.W[i] = env->fpr[a->vj].vreg.W[i] < env->fpr[a->vk].vreg.W[i] ? -1 : 0;}env->pc += 4;return true;}
static inline bool vslt_d(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8; for (size_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = env->fpr[a->vj].vreg.D[i] < env->fpr[a->vk].vreg.D[i] ? -1 : 0;}env->pc += 4;return true;}
static bool trans_vslt_b(CPULoongArchState *env, arg_vslt_b *restrict a) {CHECK_FPE(16); return vslt_b(env, a, 16);}
static bool trans_vslt_h(CPULoongArchState *env, arg_vslt_h *restrict a) {CHECK_FPE(16); return vslt_h(env, a, 16);}
static bool trans_vslt_w(CPULoongArchState *env, arg_vslt_w *restrict a) {CHECK_FPE(16); return vslt_w(env, a, 16);}
static bool trans_vslt_d(CPULoongArchState *env, arg_vslt_d *restrict a) {CHECK_FPE(16); return vslt_d(env, a, 16);}
static bool trans_xvslt_b(CPULoongArchState *env, arg_vslt_b *restrict a) {CHECK_FPE(32); return vslt_b(env, a, 32);}
static bool trans_xvslt_h(CPULoongArchState *env, arg_vslt_h *restrict a) {CHECK_FPE(32); return vslt_h(env, a, 32);}
static bool trans_xvslt_w(CPULoongArchState *env, arg_vslt_w *restrict a) {CHECK_FPE(32); return vslt_w(env, a, 32);}
static bool trans_xvslt_d(CPULoongArchState *env, arg_vslt_d *restrict a) {CHECK_FPE(32); return vslt_d(env, a, 32);}
static inline bool vslti_b(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.B[i] = env->fpr[a->vj].vreg.B[i] < a->imm ? -1: 0;}env->pc += 4;return true;}
static inline bool vslti_h(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.H[i] = env->fpr[a->vj].vreg.H[i] < a->imm ? -1: 0;}env->pc += 4;return true;}
static inline bool vslti_w(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.W[i] = env->fpr[a->vj].vreg.W[i] < a->imm ? -1: 0;}env->pc += 4;return true;}
static inline bool vslti_d(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.D[i] = env->fpr[a->vj].vreg.D[i] < a->imm ? -1: 0;}env->pc += 4;return true;}
static bool trans_vslti_b(CPULoongArchState *env, arg_vslti_b *restrict a) {CHECK_FPE(16); return vslti_b(env, a, 16);}
static bool trans_vslti_h(CPULoongArchState *env, arg_vslti_h *restrict a) {CHECK_FPE(16); return vslti_h(env, a, 16);}
static bool trans_vslti_w(CPULoongArchState *env, arg_vslti_w *restrict a) {CHECK_FPE(16); return vslti_w(env, a, 16);}
static bool trans_vslti_d(CPULoongArchState *env, arg_vslti_d *restrict a) {CHECK_FPE(16); return vslti_d(env, a, 16);}
static bool trans_xvslti_b(CPULoongArchState *env, arg_vslti_b *restrict a) {CHECK_FPE(32); return vslti_b(env, a, 32);}
static bool trans_xvslti_h(CPULoongArchState *env, arg_vslti_h *restrict a) {CHECK_FPE(32); return vslti_h(env, a, 32);}
static bool trans_xvslti_w(CPULoongArchState *env, arg_vslti_w *restrict a) {CHECK_FPE(32); return vslti_w(env, a, 32);}
static bool trans_xvslti_d(CPULoongArchState *env, arg_vslti_d *restrict a) {CHECK_FPE(32); return vslti_d(env, a, 32);}
static inline bool vslt_bu(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1; for (size_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UB[i] = env->fpr[a->vj].vreg.UB[i] < env->fpr[a->vk].vreg.UB[i] ? -1 : 0;}env->pc += 4;return true;}
static inline bool vslt_hu(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2; for (size_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UH[i] = env->fpr[a->vj].vreg.UH[i] < env->fpr[a->vk].vreg.UH[i] ? -1 : 0;}env->pc += 4;return true;}
static inline bool vslt_wu(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4; for (size_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UW[i] = env->fpr[a->vj].vreg.UW[i] < env->fpr[a->vk].vreg.UW[i] ? -1 : 0;}env->pc += 4;return true;}
static inline bool vslt_du(CPULoongArchState *env, arg_vvv *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8; for (size_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UD[i] = env->fpr[a->vj].vreg.UD[i] < env->fpr[a->vk].vreg.UD[i] ? -1 : 0;}env->pc += 4;return true;}
static bool trans_vslt_bu(CPULoongArchState *env, arg_vslt_bu *restrict a) {CHECK_FPE(16); return vslt_bu(env, a, 16);}
static bool trans_vslt_hu(CPULoongArchState *env, arg_vslt_hu *restrict a) {CHECK_FPE(16); return vslt_hu(env, a, 16);}
static bool trans_vslt_wu(CPULoongArchState *env, arg_vslt_wu *restrict a) {CHECK_FPE(16); return vslt_wu(env, a, 16);}
static bool trans_vslt_du(CPULoongArchState *env, arg_vslt_du *restrict a) {CHECK_FPE(16); return vslt_du(env, a, 16);}
static bool trans_xvslt_bu(CPULoongArchState *env, arg_vslt_bu *restrict a) {CHECK_FPE(32); return vslt_bu(env, a, 32);}
static bool trans_xvslt_hu(CPULoongArchState *env, arg_vslt_hu *restrict a) {CHECK_FPE(32); return vslt_hu(env, a, 32);}
static bool trans_xvslt_wu(CPULoongArchState *env, arg_vslt_wu *restrict a) {CHECK_FPE(32); return vslt_wu(env, a, 32);}
static bool trans_xvslt_du(CPULoongArchState *env, arg_vslt_du *restrict a) {CHECK_FPE(32); return vslt_du(env, a, 32);}
static inline bool vslti_bu(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 1;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UB[i] = env->fpr[a->vj].vreg.UB[i] < (uint64_t)a->imm ? -1: 0;}env->pc += 4;return true;}
static inline bool vslti_hu(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 2;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UH[i] = env->fpr[a->vj].vreg.UH[i] < (uint64_t)a->imm ? -1: 0;}env->pc += 4;return true;}
static inline bool vslti_wu(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 4;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UW[i] = env->fpr[a->vj].vreg.UW[i] < (uint64_t)a->imm ? -1: 0;}env->pc += 4;return true;}
static inline bool vslti_du(CPULoongArchState *env, arg_vv_i *restrict a, uint32_t vlen) {uint32_t ele_cnt = vlen / 8;for (uint32_t i = 0; i < ele_cnt; i++) {env->fpr[a->vd].vreg.UD[i] = env->fpr[a->vj].vreg.UD[i] < (uint64_t)a->imm ? -1: 0;}env->pc += 4;return true;}
static bool trans_vslti_bu(CPULoongArchState *env, arg_vslti_bu *restrict a) {CHECK_FPE(16); return vslti_bu(env, a, 16);}
static bool trans_vslti_hu(CPULoongArchState *env, arg_vslti_hu *restrict a) {CHECK_FPE(16); return vslti_hu(env, a, 16);}
static bool trans_vslti_wu(CPULoongArchState *env, arg_vslti_wu *restrict a) {CHECK_FPE(16); return vslti_wu(env, a, 16);}
static bool trans_vslti_du(CPULoongArchState *env, arg_vslti_du *restrict a) {CHECK_FPE(16); return vslti_du(env, a, 16);}
static bool trans_xvslti_bu(CPULoongArchState *env, arg_vslti_bu *restrict a) {CHECK_FPE(32); return vslti_bu(env, a, 32);}
static bool trans_xvslti_hu(CPULoongArchState *env, arg_vslti_hu *restrict a) {CHECK_FPE(32); return vslti_hu(env, a, 32);}
static bool trans_xvslti_wu(CPULoongArchState *env, arg_vslti_wu *restrict a) {CHECK_FPE(32); return vslti_wu(env, a, 32);}
static bool trans_xvslti_du(CPULoongArchState *env, arg_vslti_du *restrict a) {CHECK_FPE(32); return vslti_du(env, a, 32);}
static bool trans_vfcmp_cond_s(CPULoongArchState *env, arg_vfcmp_cond_s *restrict a) {
    CHECK_FPE(16);
    uint32_t flags = get_fcmp_flags(a->fcond >> 1);
    (a->fcond & 1) ? helper_vfcmp_s_s(env, 16, a->vd, a->vj, a->vk, flags) : helper_vfcmp_c_s(env, 16, a->vd, a->vj, a->vk, flags);
    env->pc += 4;
    return true;
}
static bool trans_vfcmp_cond_d(CPULoongArchState *env, arg_vfcmp_cond_d *restrict a) {
    CHECK_FPE(16);
    uint32_t flags = get_fcmp_flags(a->fcond >> 1);
    (a->fcond & 1) ? helper_vfcmp_s_d(env, 16, a->vd, a->vj, a->vk, flags) : helper_vfcmp_c_d(env, 16, a->vd, a->vj, a->vk, flags);
    env->pc += 4;
    return true;
}
static bool trans_vbitsel_v(CPULoongArchState *env, arg_vbitsel_v *restrict a) {
    CHECK_FPE(16);
    for (size_t i = 0; i < 2; i++) {
        env->fpr[a->vd].vreg.D[i] = ((~env->fpr[a->va].vreg.D[i]) & env->fpr[a->vj].vreg.D[i]) | (env->fpr[a->va].vreg.D[i] & env->fpr[a->vk].vreg.D[i]);
    }
    env->pc += 4;
    return true;
}
static bool trans_vbitseli_b(CPULoongArchState *env, arg_vbitseli_b *restrict a) {
    CHECK_FPE(16);
    for (size_t i = 0; i < 16; i++) {
        env->fpr[a->vd].vreg.B[i] = ((~env->fpr[a->vd].vreg.B[i]) & env->fpr[a->vj].vreg.B[i]) | (env->fpr[a->vd].vreg.B[i] & a->imm);
    }
    env->pc += 4;
    return true;
}
static bool trans_vseteqz_v(CPULoongArchState *env, arg_vseteqz_v *restrict a) {
    CHECK_FPE(16);
    uint32_t ele_cnt = 16 / 8;
    bool r = 1;
    for (uint32_t i = 0; i < ele_cnt; i++) {
        r &= (env->fpr[a->vj].vreg.D[i] == 0);
    }
    env->cf[a->cd] = r;
    env->pc += 4;
    return true;
}
static bool trans_vsetnez_v(CPULoongArchState *env, arg_vsetnez_v *restrict a) {
    CHECK_FPE(16);
    uint32_t ele_cnt = 16 / 8;
    bool r = 0;
    for (uint32_t i = 0; i < ele_cnt; i++) {
        r |= (env->fpr[a->vj].vreg.D[i] != 0);
    }
    env->cf[a->cd] = r;
    env->pc += 4;
    return true;
}
static inline bool vsetanyeqz_b(CPULoongArchState *env, arg_cv *restrict a, uint32_t vlen) { uint32_t ele_cnt = vlen / 1; bool r = 0; for (uint32_t i = 0; i < ele_cnt; i++) { r |= (env->fpr[a->vj].vreg.B[i]==0); } env->cf[a->cd] = r; env->pc += 4; return true;}
static inline bool vsetanyeqz_h(CPULoongArchState *env, arg_cv *restrict a, uint32_t vlen) { uint32_t ele_cnt = vlen / 2; bool r = 0; for (uint32_t i = 0; i < ele_cnt; i++) { r |= (env->fpr[a->vj].vreg.H[i]==0); } env->cf[a->cd] = r; env->pc += 4; return true;}
static inline bool vsetanyeqz_w(CPULoongArchState *env, arg_cv *restrict a, uint32_t vlen) { uint32_t ele_cnt = vlen / 4; bool r = 0; for (uint32_t i = 0; i < ele_cnt; i++) { r |= (env->fpr[a->vj].vreg.W[i]==0); } env->cf[a->cd] = r; env->pc += 4; return true;}
static inline bool vsetanyeqz_d(CPULoongArchState *env, arg_cv *restrict a, uint32_t vlen) { uint32_t ele_cnt = vlen / 8; bool r = 0; for (uint32_t i = 0; i < ele_cnt; i++) { r |= (env->fpr[a->vj].vreg.D[i]==0); } env->cf[a->cd] = r; env->pc += 4; return true;}
static bool trans_vsetanyeqz_b(CPULoongArchState *env, arg_vsetanyeqz_b *restrict a) {CHECK_FPE(16); return vsetanyeqz_b(env, a, 16);}
static bool trans_vsetanyeqz_h(CPULoongArchState *env, arg_vsetanyeqz_h *restrict a) {CHECK_FPE(16); return vsetanyeqz_h(env, a, 16);}
static bool trans_vsetanyeqz_w(CPULoongArchState *env, arg_vsetanyeqz_w *restrict a) {CHECK_FPE(16); return vsetanyeqz_w(env, a, 16);}
static bool trans_vsetanyeqz_d(CPULoongArchState *env, arg_vsetanyeqz_d *restrict a) {CHECK_FPE(16); return vsetanyeqz_d(env, a, 16);}
static bool trans_xvsetanyeqz_b(CPULoongArchState *env, arg_vsetanyeqz_b *restrict a) {CHECK_FPE(32); return vsetanyeqz_b(env, a, 32);}
static bool trans_xvsetanyeqz_h(CPULoongArchState *env, arg_vsetanyeqz_h *restrict a) {CHECK_FPE(32); return vsetanyeqz_h(env, a, 32);}
static bool trans_xvsetanyeqz_w(CPULoongArchState *env, arg_vsetanyeqz_w *restrict a) {CHECK_FPE(32); return vsetanyeqz_w(env, a, 32);}
static bool trans_xvsetanyeqz_d(CPULoongArchState *env, arg_vsetanyeqz_d *restrict a) {CHECK_FPE(32); return vsetanyeqz_d(env, a, 32);}
static inline bool vsetallnez_b(CPULoongArchState *env, arg_cv *restrict a, uint32_t vlen) { uint32_t ele_cnt = vlen / 1; bool r = 1; for (uint32_t i = 0; i < ele_cnt; i++) { r &= (env->fpr[a->vj].vreg.B[i]!=0); } env->cf[a->cd] = r; env->pc += 4; return true;}
static inline bool vsetallnez_h(CPULoongArchState *env, arg_cv *restrict a, uint32_t vlen) { uint32_t ele_cnt = vlen / 2; bool r = 1; for (uint32_t i = 0; i < ele_cnt; i++) { r &= (env->fpr[a->vj].vreg.H[i]!=0); } env->cf[a->cd] = r; env->pc += 4; return true;}
static inline bool vsetallnez_w(CPULoongArchState *env, arg_cv *restrict a, uint32_t vlen) { uint32_t ele_cnt = vlen / 4; bool r = 1; for (uint32_t i = 0; i < ele_cnt; i++) { r &= (env->fpr[a->vj].vreg.W[i]!=0); } env->cf[a->cd] = r; env->pc += 4; return true;}
static inline bool vsetallnez_d(CPULoongArchState *env, arg_cv *restrict a, uint32_t vlen) { uint32_t ele_cnt = vlen / 8; bool r = 1; for (uint32_t i = 0; i < ele_cnt; i++) { r &= (env->fpr[a->vj].vreg.D[i]!=0); } env->cf[a->cd] = r; env->pc += 4; return true;}
static bool trans_vsetallnez_b(CPULoongArchState *env, arg_vsetallnez_b *restrict a) {CHECK_FPE(16); return vsetallnez_b(env, a, 16);}
static bool trans_vsetallnez_h(CPULoongArchState *env, arg_vsetallnez_h *restrict a) {CHECK_FPE(16); return vsetallnez_h(env, a, 16);}
static bool trans_vsetallnez_w(CPULoongArchState *env, arg_vsetallnez_w *restrict a) {CHECK_FPE(16); return vsetallnez_w(env, a, 16);}
static bool trans_vsetallnez_d(CPULoongArchState *env, arg_vsetallnez_d *restrict a) {CHECK_FPE(16); return vsetallnez_d(env, a, 16);}
static bool trans_xvsetallnez_b(CPULoongArchState *env, arg_vsetallnez_b *restrict a) {CHECK_FPE(32); return vsetallnez_b(env, a, 32);}
static bool trans_xvsetallnez_h(CPULoongArchState *env, arg_vsetallnez_h *restrict a) {CHECK_FPE(32); return vsetallnez_h(env, a, 32);}
static bool trans_xvsetallnez_w(CPULoongArchState *env, arg_vsetallnez_w *restrict a) {CHECK_FPE(32); return vsetallnez_w(env, a, 32);}
static bool trans_xvsetallnez_d(CPULoongArchState *env, arg_vsetallnez_d *restrict a) {CHECK_FPE(32); return vsetallnez_d(env, a, 32);}
static bool trans_vinsgr2vr_b(CPULoongArchState *env, arg_vinsgr2vr_b *restrict a) {CHECK_FPE(16); env->fpr[a->vd].vreg.UB[a->imm] = env->gpr[a->rj]; env->pc += 4; return true;}
static bool trans_vinsgr2vr_h(CPULoongArchState *env, arg_vinsgr2vr_h *restrict a) {CHECK_FPE(16); env->fpr[a->vd].vreg.UH[a->imm] = env->gpr[a->rj]; env->pc += 4; return true;}
static bool trans_vinsgr2vr_w(CPULoongArchState *env, arg_vinsgr2vr_w *restrict a) {CHECK_FPE(16); env->fpr[a->vd].vreg.UW[a->imm] = env->gpr[a->rj]; env->pc += 4; return true;}
static bool trans_vinsgr2vr_d(CPULoongArchState *env, arg_vinsgr2vr_d *restrict a) {CHECK_FPE(16); env->fpr[a->vd].vreg.UD[a->imm] = env->gpr[a->rj]; env->pc += 4; return true;}
static bool trans_vpickve2gr_b(CPULoongArchState *env, arg_vpickve2gr_b *restrict a) {CHECK_FPE(16); env->gpr[a->rd] = (int64_t)env->fpr[a->vj].vreg.B[a->imm]; env->pc += 4; return true;}
static bool trans_vpickve2gr_h(CPULoongArchState *env, arg_vpickve2gr_h *restrict a) {CHECK_FPE(16); env->gpr[a->rd] = (int64_t)env->fpr[a->vj].vreg.H[a->imm]; env->pc += 4; return true;}
static bool trans_vpickve2gr_w(CPULoongArchState *env, arg_vpickve2gr_w *restrict a) {CHECK_FPE(16); env->gpr[a->rd] = (int64_t)env->fpr[a->vj].vreg.W[a->imm]; env->pc += 4; return true;}
static bool trans_vpickve2gr_d(CPULoongArchState *env, arg_vpickve2gr_d *restrict a) {CHECK_FPE(16); env->gpr[a->rd] = (int64_t)env->fpr[a->vj].vreg.D[a->imm]; env->pc += 4; return true;}
static bool trans_vpickve2gr_bu(CPULoongArchState *env, arg_vpickve2gr_bu *restrict a) {CHECK_FPE(16); env->gpr[a->rd] = (uint64_t)env->fpr[a->vj].vreg.UB[a->imm]; env->pc += 4; return true;}
static bool trans_vpickve2gr_hu(CPULoongArchState *env, arg_vpickve2gr_hu *restrict a) {CHECK_FPE(16); env->gpr[a->rd] = (uint64_t)env->fpr[a->vj].vreg.UH[a->imm]; env->pc += 4; return true;}
static bool trans_vpickve2gr_wu(CPULoongArchState *env, arg_vpickve2gr_wu *restrict a) {CHECK_FPE(16); env->gpr[a->rd] = (uint64_t)env->fpr[a->vj].vreg.UW[a->imm]; env->pc += 4; return true;}
static bool trans_vpickve2gr_du(CPULoongArchState *env, arg_vpickve2gr_du *restrict a) {CHECK_FPE(16); env->gpr[a->rd] = (uint64_t)env->fpr[a->vj].vreg.UD[a->imm]; env->pc += 4; return true;}

static bool trans_xvinsgr2vr_w(CPULoongArchState *env, arg_vinsgr2vr_w *restrict a) {CHECK_FPE(32); env->fpr[a->vd].vreg.UW[a->imm] = env->gpr[a->rj]; env->pc += 4; return true;}
static bool trans_xvinsgr2vr_d(CPULoongArchState *env, arg_vinsgr2vr_d *restrict a) {CHECK_FPE(32); env->fpr[a->vd].vreg.UD[a->imm] = env->gpr[a->rj]; env->pc += 4; return true;}
static bool trans_xvpickve2gr_w(CPULoongArchState *env, arg_vpickve2gr_w *restrict a) {CHECK_FPE(32); env->gpr[a->rd] = (int64_t)env->fpr[a->vj].vreg.W[a->imm]; env->pc += 4; return true;}
static bool trans_xvpickve2gr_d(CPULoongArchState *env, arg_vpickve2gr_d *restrict a) {CHECK_FPE(32); env->gpr[a->rd] = (int64_t)env->fpr[a->vj].vreg.D[a->imm]; env->pc += 4; return true;}
static bool trans_xvpickve2gr_wu(CPULoongArchState *env, arg_vpickve2gr_wu *restrict a) {CHECK_FPE(32); env->gpr[a->rd] = (uint64_t)env->fpr[a->vj].vreg.UW[a->imm]; env->pc += 4; return true;}
static bool trans_xvpickve2gr_du(CPULoongArchState *env, arg_vpickve2gr_du *restrict a) {CHECK_FPE(32); env->gpr[a->rd] = (uint64_t)env->fpr[a->vj].vreg.UD[a->imm]; env->pc += 4; return true;}
static bool vreplgr2vr_b(CPULoongArchState *env, arg_vr *restrict a, uint32_t vlen) { uint32_t ele_cnt = vlen / 1; for (int i = 0; i < ele_cnt; i ++) { env->fpr[a->vd].vreg.UB[i] = env->gpr[a->rj]; } env->pc += 4;return true;}
static bool vreplgr2vr_h(CPULoongArchState *env, arg_vr *restrict a, uint32_t vlen) { uint32_t ele_cnt = vlen / 2; for (int i = 0; i < ele_cnt; i ++) { env->fpr[a->vd].vreg.UH[i] = env->gpr[a->rj]; } env->pc += 4;return true;}
static bool vreplgr2vr_w(CPULoongArchState *env, arg_vr *restrict a, uint32_t vlen) { uint32_t ele_cnt = vlen / 4; for (int i = 0; i < ele_cnt; i ++) { env->fpr[a->vd].vreg.UW[i] = env->gpr[a->rj]; } env->pc += 4;return true;}
static bool vreplgr2vr_d(CPULoongArchState *env, arg_vr *restrict a, uint32_t vlen) { uint32_t ele_cnt = vlen / 8; for (int i = 0; i < ele_cnt; i ++) { env->fpr[a->vd].vreg.UD[i] = env->gpr[a->rj]; } env->pc += 4;return true;}
static bool trans_vreplgr2vr_b(CPULoongArchState *env, arg_vreplgr2vr_b *restrict a) {CHECK_FPE(16); return vreplgr2vr_b(env, a, 16);}
static bool trans_vreplgr2vr_h(CPULoongArchState *env, arg_vreplgr2vr_h *restrict a) {CHECK_FPE(16); return vreplgr2vr_h(env, a, 16);}
static bool trans_vreplgr2vr_w(CPULoongArchState *env, arg_vreplgr2vr_w *restrict a) {CHECK_FPE(16); return vreplgr2vr_w(env, a, 16);}
static bool trans_vreplgr2vr_d(CPULoongArchState *env, arg_vreplgr2vr_d *restrict a) {CHECK_FPE(16); return vreplgr2vr_d(env, a, 16);}
static bool trans_xvreplgr2vr_b(CPULoongArchState *env, arg_vreplgr2vr_b *restrict a) {CHECK_FPE(32); return vreplgr2vr_b(env, a, 32);}
static bool trans_xvreplgr2vr_h(CPULoongArchState *env, arg_vreplgr2vr_h *restrict a) {CHECK_FPE(32); return vreplgr2vr_h(env, a, 32);}
static bool trans_xvreplgr2vr_w(CPULoongArchState *env, arg_vreplgr2vr_w *restrict a) {CHECK_FPE(32); return vreplgr2vr_w(env, a, 32);}
static bool trans_xvreplgr2vr_d(CPULoongArchState *env, arg_vreplgr2vr_d *restrict a) {CHECK_FPE(32); return vreplgr2vr_d(env, a, 32);}
static bool trans_vreplve_b(CPULoongArchState *env, arg_vreplve_b *restrict a) {CHECK_FPE(16); int32_t ele_cnt = 16 / 1; for (int i = 0; i < ele_cnt; i ++) { env->fpr[a->vd].vreg.B[i] = env->fpr[a->vj].vreg.B[env->gpr[a->rk] & (ele_cnt - 1)]; } env->pc += 4; return true;}
static bool trans_vreplve_h(CPULoongArchState *env, arg_vreplve_h *restrict a) {CHECK_FPE(16); int32_t ele_cnt = 16 / 2; for (int i = 0; i < ele_cnt; i ++) { env->fpr[a->vd].vreg.H[i] = env->fpr[a->vj].vreg.H[env->gpr[a->rk] & (ele_cnt - 1)]; } env->pc += 4; return true;}
static bool trans_vreplve_w(CPULoongArchState *env, arg_vreplve_w *restrict a) {CHECK_FPE(16); int32_t ele_cnt = 16 / 4; for (int i = 0; i < ele_cnt; i ++) { env->fpr[a->vd].vreg.W[i] = env->fpr[a->vj].vreg.W[env->gpr[a->rk] & (ele_cnt - 1)]; } env->pc += 4; return true;}
static bool trans_vreplve_d(CPULoongArchState *env, arg_vreplve_d *restrict a) {CHECK_FPE(16); int32_t ele_cnt = 16 / 8; for (int i = 0; i < ele_cnt; i ++) { env->fpr[a->vd].vreg.D[i] = env->fpr[a->vj].vreg.D[env->gpr[a->rk] & (ele_cnt - 1)]; } env->pc += 4; return true;}
static bool trans_vreplvei_b(CPULoongArchState *env, arg_vreplvei_b *restrict a) {CHECK_FPE(16); uint32_t ele_cnt = 16 / 1; for (int i = 0; i < ele_cnt; i ++) { env->fpr[a->vd].vreg.B[i] = env->fpr[a->vj].vreg.B[a->imm]; } env->pc += 4; return true;}
static bool trans_vreplvei_h(CPULoongArchState *env, arg_vreplvei_h *restrict a) {CHECK_FPE(16); uint32_t ele_cnt = 16 / 2; for (int i = 0; i < ele_cnt; i ++) { env->fpr[a->vd].vreg.H[i] = env->fpr[a->vj].vreg.H[a->imm]; } env->pc += 4; return true;}
static bool trans_vreplvei_w(CPULoongArchState *env, arg_vreplvei_w *restrict a) {CHECK_FPE(16); uint32_t ele_cnt = 16 / 4; for (int i = 0; i < ele_cnt; i ++) { env->fpr[a->vd].vreg.W[i] = env->fpr[a->vj].vreg.W[a->imm]; } env->pc += 4; return true;}
static bool trans_vreplvei_d(CPULoongArchState *env, arg_vreplvei_d *restrict a) {CHECK_FPE(16); uint32_t ele_cnt = 16 / 8; for (int i = 0; i < ele_cnt; i ++) { env->fpr[a->vd].vreg.D[i] = env->fpr[a->vj].vreg.D[a->imm]; } env->pc += 4; return true;}
static bool trans_vbsll_v(CPULoongArchState *env, arg_vbsll_v *restrict a) {
    CHECK_FPE(16);
    int imm = a->imm & 0xf;
    for (int i = 0; i < (16 - imm); i ++) {
        env->fpr[a->vd].vreg.B[15 - i] = env->fpr[a->vj].vreg.B[15 - (i + imm)];
    }
    for (int i = 0; i < imm; i ++) {
        env->fpr[a->vd].vreg.B[i] = 0;
    }
    env->pc += 4;
    return true;
}
static bool trans_vbsrl_v(CPULoongArchState *env, arg_vbsrl_v *restrict a) {
    CHECK_FPE(16);
    int imm = a->imm & 0xf;
    for (int i = 0; i < (16 - imm); i ++) {
        env->fpr[a->vd].vreg.B[i] = env->fpr[a->vj].vreg.B[i + imm];
    }
    for (int i = (16 - imm); i < 16; i ++) {
        env->fpr[a->vd].vreg.B[i] = 0;
    }
    env->pc += 4;
    return true;
}
gen_trans_vvvd(vpackev_b, 16, vpackev_b)
gen_trans_vvvd(vpackev_h, 16, vpackev_h)
gen_trans_vvvd(vpackev_w, 16, vpackev_w)
gen_trans_vvvd(vpackev_d, 16, vpackev_d)
gen_trans_vvvd(vpackod_b, 16, vpackod_b)
gen_trans_vvvd(vpackod_h, 16, vpackod_h)
gen_trans_vvvd(vpackod_w, 16, vpackod_w)
gen_trans_vvvd(vpackod_d, 16, vpackod_d)
static bool trans_vpickev_b(CPULoongArchState *env, arg_vpickev_b *restrict a) {CHECK_FPE(16); int oprsz = 16; uint32_t desc = simd_desc(oprsz, oprsz, 0); helper_vpickev_b(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], desc); env->pc += 4; return true;}
static bool trans_vpickev_h(CPULoongArchState *env, arg_vpickev_h *restrict a) {CHECK_FPE(16); int oprsz = 16; uint32_t desc = simd_desc(oprsz, oprsz, 0); helper_vpickev_h(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], desc); env->pc += 4; return true;}
static bool trans_vpickev_w(CPULoongArchState *env, arg_vpickev_w *restrict a) {CHECK_FPE(16); int oprsz = 16; uint32_t desc = simd_desc(oprsz, oprsz, 0); helper_vpickev_w(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], desc); env->pc += 4; return true;}
static bool trans_vpickev_d(CPULoongArchState *env, arg_vpickev_d *restrict a) {CHECK_FPE(16); int oprsz = 16; uint32_t desc = simd_desc(oprsz, oprsz, 0); helper_vpickev_d(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], desc); env->pc += 4; return true;}
static bool trans_vpickod_b(CPULoongArchState *env, arg_vpickod_b *restrict a) {CHECK_FPE(16); int oprsz = 16; uint32_t desc = simd_desc(oprsz, oprsz, 0); helper_vpickod_b(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], desc); env->pc += 4; return true;}
static bool trans_vpickod_h(CPULoongArchState *env, arg_vpickod_h *restrict a) {CHECK_FPE(16); int oprsz = 16; uint32_t desc = simd_desc(oprsz, oprsz, 0); helper_vpickod_h(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], desc); env->pc += 4; return true;}
static bool trans_vpickod_w(CPULoongArchState *env, arg_vpickod_w *restrict a) {CHECK_FPE(16); int oprsz = 16; uint32_t desc = simd_desc(oprsz, oprsz, 0); helper_vpickod_w(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], desc); env->pc += 4; return true;}
static bool trans_vpickod_d(CPULoongArchState *env, arg_vpickod_d *restrict a) {CHECK_FPE(16); int oprsz = 16; uint32_t desc = simd_desc(oprsz, oprsz, 0); helper_vpickod_d(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], desc); env->pc += 4; return true;}
static bool trans_xvpickev_b(CPULoongArchState *env, arg_vpickev_b *restrict a) {CHECK_FPE(32); int oprsz = 32; uint32_t desc = simd_desc(oprsz, oprsz, 0); helper_vpickev_b(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], desc); env->pc += 4; return true;}
static bool trans_xvpickev_h(CPULoongArchState *env, arg_vpickev_h *restrict a) {CHECK_FPE(32); int oprsz = 32; uint32_t desc = simd_desc(oprsz, oprsz, 0); helper_vpickev_h(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], desc); env->pc += 4; return true;}
static bool trans_xvpickev_w(CPULoongArchState *env, arg_vpickev_w *restrict a) {CHECK_FPE(32); int oprsz = 32; uint32_t desc = simd_desc(oprsz, oprsz, 0); helper_vpickev_w(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], desc); env->pc += 4; return true;}
static bool trans_xvpickev_d(CPULoongArchState *env, arg_vpickev_d *restrict a) {CHECK_FPE(32); int oprsz = 32; uint32_t desc = simd_desc(oprsz, oprsz, 0); helper_vpickev_d(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], desc); env->pc += 4; return true;}
static bool trans_xvpickod_b(CPULoongArchState *env, arg_vpickod_b *restrict a) {CHECK_FPE(32); int oprsz = 32; uint32_t desc = simd_desc(oprsz, oprsz, 0); helper_vpickod_b(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], desc); env->pc += 4; return true;}
static bool trans_xvpickod_h(CPULoongArchState *env, arg_vpickod_h *restrict a) {CHECK_FPE(32); int oprsz = 32; uint32_t desc = simd_desc(oprsz, oprsz, 0); helper_vpickod_h(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], desc); env->pc += 4; return true;}
static bool trans_xvpickod_w(CPULoongArchState *env, arg_vpickod_w *restrict a) {CHECK_FPE(32); int oprsz = 32; uint32_t desc = simd_desc(oprsz, oprsz, 0); helper_vpickod_w(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], desc); env->pc += 4; return true;}
static bool trans_xvpickod_d(CPULoongArchState *env, arg_vpickod_d *restrict a) {CHECK_FPE(32); int oprsz = 32; uint32_t desc = simd_desc(oprsz, oprsz, 0); helper_vpickod_d(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], desc); env->pc += 4; return true;}
static bool trans_vilvl_b(CPULoongArchState *env, arg_vilvl_b *restrict a) {
    CHECK_FPE(16);
    int oprsz = 16;
    uint32_t desc = simd_desc(oprsz, oprsz, 0);
    helper_vilvl_b(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], desc);
    env->pc += 4;
    return true;
}
static bool trans_vilvl_h(CPULoongArchState *env, arg_vilvl_h *restrict a) {
    CHECK_FPE(16);
    int oprsz = 16;
    uint32_t desc = simd_desc(oprsz, oprsz, 0);
    helper_vilvl_h(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], desc);
    env->pc += 4;
    return true;
}
static bool trans_vilvl_w(CPULoongArchState *env, arg_vilvl_w *restrict a) {
    CHECK_FPE(16);
    int oprsz = 16;
    uint32_t desc = simd_desc(oprsz, oprsz, 0);
    helper_vilvl_w(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], desc);
    env->pc += 4;
    return true;
}
static bool trans_vilvl_d(CPULoongArchState *env, arg_vilvl_d *restrict a) {
    CHECK_FPE(16);
    int oprsz = 16;
    uint32_t desc = simd_desc(oprsz, oprsz, 0);
    helper_vilvl_d(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], desc);
    env->pc += 4;
    return true;
}
static bool trans_vilvh_b(CPULoongArchState *env, arg_vilvh_b *restrict a) {
    CHECK_FPE(16);
    int oprsz = 16;
    uint32_t desc = simd_desc(oprsz, oprsz, 0);
    helper_vilvh_b(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], desc);
    env->pc += 4;
    return true;
}
static bool trans_vilvh_h(CPULoongArchState *env, arg_vilvh_h *restrict a) {
    CHECK_FPE(16);
    int oprsz = 16;
    uint32_t desc = simd_desc(oprsz, oprsz, 0);
    helper_vilvh_h(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], desc);
    env->pc += 4;
    return true;
}
static bool trans_vilvh_w(CPULoongArchState *env, arg_vilvh_w *restrict a) {
    CHECK_FPE(16);
    int oprsz = 16;
    uint32_t desc = simd_desc(oprsz, oprsz, 0);
    helper_vilvh_w(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], desc);
    env->pc += 4;
    return true;
}
static bool trans_vilvh_d(CPULoongArchState *env, arg_vilvh_d *restrict a) {
    CHECK_FPE(16);
    int oprsz = 16;
    uint32_t desc = simd_desc(oprsz, oprsz, 0);
    helper_vilvh_d(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], desc);
    env->pc += 4;
    return true;
}
static bool trans_vshuf_b(CPULoongArchState *env, arg_vshuf_b *restrict a) {
    CHECK_FPE(16);
    int oprsz = 16;
    uint32_t desc = simd_desc(oprsz, oprsz, 0);
    helper_vshuf_b(&env->fpr[a->vd], &env->fpr[a->vj], &env->fpr[a->vk], &env->fpr[a->va], desc);
    env->pc += 4;
    return true;
}
static bool trans_vshuf_h(CPULoongArchState *env, arg_vshuf_h *restrict a) {CHECK_FPE(16); const uint32_t ele_cnt = 16 / 2;int16_t vv[ele_cnt * 2];for (size_t i = 0; i < ele_cnt; i++) {vv[i] = env->fpr[a->vk].vreg.H[i];vv[ele_cnt + i] = env->fpr[a->vj].vreg.H[i];}for (size_t i = 0; i < ele_cnt; i++) { env->fpr[a->vd].vreg.H[i] = vv[env->fpr[a->vd].vreg.H[i] & (ele_cnt * 2 - 1)];}env->pc += 4;return true;}
static bool trans_vshuf_w(CPULoongArchState *env, arg_vshuf_w *restrict a) {CHECK_FPE(16); const uint32_t ele_cnt = 16 / 4;int32_t vv[ele_cnt * 2];for (size_t i = 0; i < ele_cnt; i++) {vv[i] = env->fpr[a->vk].vreg.W[i];vv[ele_cnt + i] = env->fpr[a->vj].vreg.W[i];}for (size_t i = 0; i < ele_cnt; i++) { env->fpr[a->vd].vreg.W[i] = vv[env->fpr[a->vd].vreg.W[i] & (ele_cnt * 2 - 1)];}env->pc += 4;return true;}
static bool trans_vshuf_d(CPULoongArchState *env, arg_vshuf_d *restrict a) {CHECK_FPE(16); const uint32_t ele_cnt = 16 / 8;int64_t vv[ele_cnt * 2];for (size_t i = 0; i < ele_cnt; i++) {vv[i] = env->fpr[a->vk].vreg.D[i];vv[ele_cnt + i] = env->fpr[a->vj].vreg.D[i];}for (size_t i = 0; i < ele_cnt; i++) { env->fpr[a->vd].vreg.D[i] = vv[env->fpr[a->vd].vreg.D[i] & (ele_cnt * 2 - 1)];}env->pc += 4;return true;}
gen_trans_vvid(vshuf4i_b, 16, vshuf4i_b)
gen_trans_vvid(vshuf4i_h, 16, vshuf4i_h)
gen_trans_vvid(vshuf4i_w, 16, vshuf4i_w)
gen_trans_vvid(vshuf4i_d, 16, vshuf4i_d)
gen_trans_vvid(vpermi_w, 16, vpermi_w)

gen_trans_vvid(vextrins_d, 16, vextrins_d)
gen_trans_vvid(vextrins_w, 16, vextrins_w)
gen_trans_vvid(vextrins_h, 16, vextrins_h)
gen_trans_vvid(vextrins_b, 16, vextrins_b)
static bool trans_vld(CPULoongArchState *env, arg_vld *restrict a) {
    CHECK_FPE(16);
    uint64_t va = add_addr(env->gpr[a->rj], a->imm);
    lsassert(!is_io(load_pa(env, va)));
    env->fpr[a->vd].vreg.D[0] = ld_d(env, va);
    env->fpr[a->vd].vreg.D[1] = ld_d(env, va + 8);
    env->pc += 4;
    return true;
}
static bool trans_vst(CPULoongArchState *env, arg_vst *restrict a) {
    CHECK_FPE(16);
    uint64_t va = add_addr(env->gpr[a->rj], a->imm);
    lsassert(!is_io(store_pa(env, va)));
    st_d(env, va, env->fpr[a->vd].vreg.D[0]);
    st_d(env, va + 8, env->fpr[a->vd].vreg.D[1]);
    env->pc += 4;
    return true;
}
static bool trans_vldx(CPULoongArchState *env, arg_vldx *restrict a) {
    CHECK_FPE(16);
    uint64_t va = add_addr(env->gpr[a->rj], env->gpr[a->rk]);
    lsassert(!is_io(load_pa(env, va)));
    env->fpr[a->vd].vreg.D[0] = ld_d(env, va);
    env->fpr[a->vd].vreg.D[1] = ld_d(env, va + 8);
    env->pc += 4;
    return true;
}
static bool trans_vstx(CPULoongArchState *env, arg_vstx *restrict a) {
    CHECK_FPE(16);
    uint64_t va = add_addr(env->gpr[a->rj], env->gpr[a->rk]);
    lsassert(!is_io(store_pa(env, va)));
    st_d(env, va, env->fpr[a->vd].vreg.D[0]);
    st_d(env, va + 8, env->fpr[a->vd].vreg.D[1]);
    env->pc += 4;
    return true;
}
static bool trans_vldrepl_b(CPULoongArchState *env, arg_vldrepl_b *restrict a) {CHECK_FPE(16); int8_t data = ld_b(env, add_addr(env->gpr[a->rj], a->imm));for (size_t i = 0; i < 16; i++){env->fpr[a->vd].vreg.B[i] = data;}env->pc += 4;return true;}
static bool trans_vldrepl_h(CPULoongArchState *env, arg_vldrepl_h *restrict a) {CHECK_FPE(16); int16_t data = ld_h(env, add_addr(env->gpr[a->rj], a->imm));for (size_t i = 0; i < 8; i++){env->fpr[a->vd].vreg.H[i] = data;}env->pc += 4;return true;}
static bool trans_vldrepl_w(CPULoongArchState *env, arg_vldrepl_w *restrict a) {CHECK_FPE(16); int32_t data = ld_w(env, add_addr(env->gpr[a->rj], a->imm));for (size_t i = 0; i < 4; i++){env->fpr[a->vd].vreg.W[i] = data;}env->pc += 4;return true;}
static bool trans_vldrepl_d(CPULoongArchState *env, arg_vldrepl_d *restrict a) {CHECK_FPE(16); int64_t data = ld_d(env, add_addr(env->gpr[a->rj], a->imm));for (size_t i = 0; i < 2; i++){env->fpr[a->vd].vreg.D[i] = data;}env->pc += 4;return true;}
static bool trans_vstelm_b(CPULoongArchState *env, arg_vstelm_b *restrict a) {CHECK_FPE(16); st_b(env, add_addr(env->gpr[a->rj], a->imm), env->fpr[a->vd].vreg.B[a->imm2]);env->pc += 4;return true;}
static bool trans_vstelm_h(CPULoongArchState *env, arg_vstelm_h *restrict a) {CHECK_FPE(16); st_h(env, add_addr(env->gpr[a->rj], a->imm), env->fpr[a->vd].vreg.H[a->imm2]);env->pc += 4;return true;}
static bool trans_vstelm_w(CPULoongArchState *env, arg_vstelm_w *restrict a) {CHECK_FPE(16); st_w(env, add_addr(env->gpr[a->rj], a->imm), env->fpr[a->vd].vreg.W[a->imm2]);env->pc += 4;return true;}
static bool trans_vstelm_d(CPULoongArchState *env, arg_vstelm_d *restrict a) {CHECK_FPE(16); st_d(env, add_addr(env->gpr[a->rj], a->imm), env->fpr[a->vd].vreg.D[a->imm2]);env->pc += 4;return true;}


gen_trans_vvd(vext2xv_d_b, 32, vext2xv_d_b)
gen_trans_vvd(vext2xv_d_h, 32, vext2xv_d_h)
gen_trans_vvd(vext2xv_du_bu, 32, vext2xv_du_bu)
gen_trans_vvd(vext2xv_du_hu, 32, vext2xv_du_hu)
gen_trans_vvd(vext2xv_du_wu, 32, vext2xv_du_wu)
gen_trans_vvd(vext2xv_d_w, 32, vext2xv_d_w)
gen_trans_vvd(vext2xv_h_b, 32, vext2xv_h_b)
gen_trans_vvd(vext2xv_hu_bu, 32, vext2xv_hu_bu)
gen_trans_vvd(vext2xv_w_b, 32, vext2xv_w_b)
gen_trans_vvd(vext2xv_w_h, 32, vext2xv_w_h)
gen_trans_vvd(vext2xv_wu_bu, 32, vext2xv_wu_bu)
gen_trans_vvd(vext2xv_wu_hu, 32, vext2xv_wu_hu)

static bool trans_xvadd_q(CPULoongArchState *env, arg_xvadd_q *restrict a) {
    CHECK_FPE(32);
    env->fpr[a->vd].vreg.Q[0] = env->fpr[a->vj].vreg.Q[0] + env->fpr[a->vk].vreg.Q[0];
    env->fpr[a->vd].vreg.Q[1] = env->fpr[a->vj].vreg.Q[1] + env->fpr[a->vk].vreg.Q[1];
    env->pc += 4;
    return true;
}
gen_trans_vvvd(xvaddwev_d_w, 32, vaddwev_d_w)
gen_trans_vvvd(xvaddwev_d_wu, 32, vaddwev_d_wu)
gen_trans_vvvd(xvaddwev_d_wu_w, 32, vaddwev_d_wu_w)
gen_trans_vvvd(xvaddwev_h_b, 32, vaddwev_h_b)
gen_trans_vvvd(xvaddwev_h_bu_b, 32, vaddwev_h_bu_b)
gen_trans_vvvd(xvaddwev_h_bu, 32, vaddwev_h_bu)
gen_trans_vvvd(xvaddwev_q_d, 32, vaddwev_q_d)
gen_trans_vvvd(xvaddwev_q_du_d, 32, vaddwev_q_du_d)
gen_trans_vvvd(xvaddwev_q_du, 32, vaddwev_q_du)
gen_trans_vvvd(xvaddwev_w_h, 32, vaddwev_w_h)
gen_trans_vvvd(xvaddwev_w_hu, 32, vaddwev_w_hu)
gen_trans_vvvd(xvaddwev_w_hu_h, 32, vaddwev_w_hu_h)
gen_trans_vvvd(xvaddwod_d_w, 32, vaddwod_d_w)
gen_trans_vvvd(xvaddwod_d_wu, 32, vaddwod_d_wu)
gen_trans_vvvd(xvaddwod_d_wu_w, 32, vaddwod_d_wu_w)
gen_trans_vvvd(xvaddwod_h_b, 32, vaddwod_h_b)
gen_trans_vvvd(xvaddwod_h_bu_b, 32, vaddwod_h_bu_b)
gen_trans_vvvd(xvaddwod_h_bu, 32, vaddwod_h_bu)
gen_trans_vvvd(xvaddwod_q_d, 32, vaddwod_q_d)
gen_trans_vvvd(xvaddwod_q_du_d, 32, vaddwod_q_du_d)
gen_trans_vvvd(xvaddwod_q_du, 32, vaddwod_q_du)
gen_trans_vvvd(xvaddwod_w_h, 32, vaddwod_w_h)
gen_trans_vvvd(xvaddwod_w_hu, 32, vaddwod_w_hu)
gen_trans_vvvd(xvaddwod_w_hu_h, 32, vaddwod_w_hu_h)
static bool trans_xvbitseli_b(CPULoongArchState *env, arg_xvbitseli_b *restrict a) {
    CHECK_FPE(32);
    for (size_t i = 0; i < 32; i++) {
        env->fpr[a->vd].vreg.B[i] = ((~env->fpr[a->vd].vreg.B[i]) & env->fpr[a->vj].vreg.B[i]) | (env->fpr[a->vd].vreg.B[i] & a->imm);
    }
    env->pc += 4;
    return true;
}
static bool trans_xvbitsel_v(CPULoongArchState *env, arg_xvbitsel_v *restrict a) {
    CHECK_FPE(32);
    for (size_t i = 0; i < 4; i++) {
        env->fpr[a->vd].vreg.D[i] = ((~env->fpr[a->va].vreg.D[i]) & env->fpr[a->vj].vreg.D[i]) | (env->fpr[a->va].vreg.D[i] & env->fpr[a->vk].vreg.D[i]);
    }
    env->pc += 4;
    return true;
}
static bool trans_xvbsll_v(CPULoongArchState *env, arg_xvbsll_v *restrict a) {
    CHECK_FPE(32);
    int vlen = 16;
    int imm = a->imm & (vlen - 1);
    for (int i = 0; i < (vlen - imm); i ++) {
        env->fpr[a->vd].vreg.B[vlen - 1 - i] = env->fpr[a->vj].vreg.B[vlen - 1 - (i + imm)];
        env->fpr[a->vd].vreg.B[vlen + vlen - 1 - i] = env->fpr[a->vj].vreg.B[vlen + vlen - 1 - (i + imm)];
    }
    for (int i = 0; i < imm; i ++) {
        env->fpr[a->vd].vreg.B[i] = 0;
        env->fpr[a->vd].vreg.B[i + vlen] = 0;
    }
    env->pc += 4;
    return true;
}
static bool trans_xvbsrl_v(CPULoongArchState *env, arg_xvbsrl_v *restrict a) {
    CHECK_FPE(32);
    int vlen = 16;
    int imm = a->imm & (vlen - 1);
    for (int i = 0; i < (vlen - imm); i ++) {
        env->fpr[a->vd].vreg.B[i] = env->fpr[a->vj].vreg.B[i + imm];
        env->fpr[a->vd].vreg.B[i + vlen] = env->fpr[a->vj].vreg.B[i + imm + vlen];    }
    for (int i = (vlen - imm); i < vlen; i ++) {
        env->fpr[a->vd].vreg.B[i] = 0;
        env->fpr[a->vd].vreg.B[i + vlen] = 0;
    }
    env->pc += 4;
    return true;
}
gen_trans_vvd(xvclo_b, 32, vclo_b)
gen_trans_vvd(xvclo_h, 32, vclo_h)
gen_trans_vvd(xvclo_w, 32, vclo_w)
gen_trans_vvd(xvclo_d, 32, vclo_d)
gen_trans_vvd(xvclz_b, 32, vclz_b)
gen_trans_vvd(xvclz_h, 32, vclz_h)
gen_trans_vvd(xvclz_w, 32, vclz_w)
gen_trans_vvd(xvclz_d, 32, vclz_d)
gen_trans_vvd(xvpcnt_b, 32, vpcnt_b)
gen_trans_vvd(xvpcnt_h, 32, vpcnt_h)
gen_trans_vvd(xvpcnt_w, 32, vpcnt_w)
gen_trans_vvd(xvpcnt_d, 32, vpcnt_d)

gen_trans_vvd(xvexth_du_wu, 32, vexth_du_wu)
gen_trans_vvd(xvexth_d_w, 32, vexth_d_w)
gen_trans_vvd(xvexth_h_b, 32, vexth_h_b)
gen_trans_vvd(xvexth_hu_bu, 32, vexth_hu_bu)
gen_trans_vvd(xvexth_q_d, 32, vexth_q_d)
gen_trans_vvd(xvexth_qu_du, 32, vexth_qu_du)
gen_trans_vvd(xvexth_w_h, 32, vexth_w_h)
gen_trans_vvd(xvexth_wu_hu, 32, vexth_wu_hu)
gen_trans_vvd(xvextl_q_d, 32, vextl_q_d)
gen_trans_vvd(xvextl_qu_du, 32, vextl_qu_du)
gen_trans_vvid(xvextrins_d, 32, vextrins_d)
gen_trans_vvid(xvextrins_w, 32, vextrins_w)
gen_trans_vvid(xvextrins_h, 32, vextrins_h)
gen_trans_vvid(xvextrins_b, 32, vextrins_b)
gen_trans_vved(xvfclass_d, 32, vfclass_d)
gen_trans_vved(xvfclass_s, 32, vfclass_s)
static bool trans_xvfcmp_cond_d(CPULoongArchState *env, arg_xvfcmp_cond_d *restrict a) {
    CHECK_FPE(32);
    uint32_t flags = get_fcmp_flags(a->fcond >> 1);
    (a->fcond & 1) ? helper_vfcmp_s_d(env, 32, a->vd, a->vj, a->vk, flags) : helper_vfcmp_c_d(env, 32, a->vd, a->vj, a->vk, flags);
    env->pc += 4;
    return true;
}
static bool trans_xvfcmp_cond_s(CPULoongArchState *env, arg_xvfcmp_cond_s *restrict a) {
    CHECK_FPE(32);
    uint32_t flags = get_fcmp_flags(a->fcond >> 1);
    (a->fcond & 1) ? helper_vfcmp_s_s(env, 32, a->vd, a->vj, a->vk, flags) : helper_vfcmp_c_s(env, 32, a->vd, a->vj, a->vk, flags);
    env->pc += 4;
    return true;
}
gen_trans_vved(xvfcvtl_s_h, 32, vfcvtl_s_h)
gen_trans_vved(xvfcvth_s_h, 32, vfcvth_s_h)
gen_trans_vved(xvfcvtl_d_s, 32, vfcvtl_d_s)
gen_trans_vved(xvfcvth_d_s, 32, vfcvth_d_s)
gen_trans_vvved(xvfcvt_h_s, 32, vfcvt_h_s)
gen_trans_vvved(xvfcvt_s_d, 32, vfcvt_s_d)
gen_trans_vved(xvffint_s_w, 32, vffint_s_w)
gen_trans_vved(xvffint_s_wu, 32, vffint_s_wu)
gen_trans_vved(xvffint_d_l, 32, vffint_d_l)
gen_trans_vved(xvffint_d_lu, 32, vffint_d_lu)
gen_trans_vved(xvffintl_d_w, 32, vffintl_d_w)
gen_trans_vved(xvffinth_d_w, 32, vffinth_d_w)
gen_trans_vvved(xvffint_s_l, 32, vffint_s_l)
gen_trans_vved(xvflogb_d, 32, vflogb_d)
gen_trans_vved(xvflogb_s, 32, vflogb_s)
gen_trans_vved(xvfrecip_d, 32, vfrecip_d)
gen_trans_vved(xvfrecip_s, 32, vfrecip_s)
gen_trans_vved(xvfrsqrt_d, 32, vfrsqrt_d)
gen_trans_vved(xvfrsqrt_s, 32, vfrsqrt_s)
gen_trans_vvvd(xvfrstp_b, 32, vfrstp_b)
gen_trans_vvvd(xvfrstp_h, 32, vfrstp_h)
gen_trans_vvid(xvfrstpi_b, 32, vfrstpi_b)
gen_trans_vvid(xvfrstpi_h, 32, vfrstpi_h)
gen_trans_vved(xvfsqrt_d, 32, vfsqrt_d)
gen_trans_vved(xvfsqrt_s, 32, vfsqrt_s)
gen_trans_vved(xvfrint_s, 32, vfrint_s)
gen_trans_vved(xvfrint_d, 32, vfrint_d)
gen_trans_vved(xvfrintrm_s, 32, vfrintrm_s)
gen_trans_vved(xvfrintrm_d, 32, vfrintrm_d)
gen_trans_vved(xvfrintrp_s, 32, vfrintrp_s)
gen_trans_vved(xvfrintrp_d, 32, vfrintrp_d)
gen_trans_vved(xvfrintrz_s, 32, vfrintrz_s)
gen_trans_vved(xvfrintrz_d, 32, vfrintrz_d)
gen_trans_vved(xvfrintrne_s, 32, vfrintrne_s)
gen_trans_vved(xvfrintrne_d, 32, vfrintrne_d)
gen_trans_vved(xvftint_w_s, 32, vftint_w_s)
gen_trans_vved(xvftint_l_d, 32, vftint_l_d)
gen_trans_vved(xvftintrm_w_s, 32, vftintrm_w_s)
gen_trans_vved(xvftintrm_l_d, 32, vftintrm_l_d)
gen_trans_vved(xvftintrp_w_s, 32, vftintrp_w_s)
gen_trans_vved(xvftintrp_l_d, 32, vftintrp_l_d)
gen_trans_vved(xvftintrz_w_s, 32, vftintrz_w_s)
gen_trans_vved(xvftintrz_l_d, 32, vftintrz_l_d)
gen_trans_vved(xvftintrne_w_s, 32, vftintrne_w_s)
gen_trans_vved(xvftintrne_l_d, 32, vftintrne_l_d)
gen_trans_vved(xvftint_wu_s, 32, vftint_wu_s)
gen_trans_vved(xvftint_lu_d, 32, vftint_lu_d)
gen_trans_vved(xvftintrz_wu_s, 32, vftintrz_wu_s)
gen_trans_vved(xvftintrz_lu_d, 32, vftintrz_lu_d)
gen_trans_vvved(xvftint_w_d, 32, vftint_w_d)
gen_trans_vvved(xvftintrm_w_d, 32, vftintrm_w_d)
gen_trans_vvved(xvftintrp_w_d, 32, vftintrp_w_d)
gen_trans_vvved(xvftintrz_w_d, 32, vftintrz_w_d)
gen_trans_vvved(xvftintrne_w_d, 32, vftintrne_w_d)
gen_trans_vved(xvftintl_l_s, 32, vftintl_l_s)
gen_trans_vved(xvftinth_l_s, 32, vftinth_l_s)
gen_trans_vved(xvftintrml_l_s, 32, vftintrml_l_s)
gen_trans_vved(xvftintrmh_l_s, 32, vftintrmh_l_s)
gen_trans_vved(xvftintrpl_l_s, 32, vftintrpl_l_s)
gen_trans_vved(xvftintrph_l_s, 32, vftintrph_l_s)
gen_trans_vved(xvftintrzl_l_s, 32, vftintrzl_l_s)
gen_trans_vved(xvftintrzh_l_s, 32, vftintrzh_l_s)
gen_trans_vved(xvftintrnel_l_s, 32, vftintrnel_l_s)
gen_trans_vved(xvftintrneh_l_s, 32, vftintrneh_l_s)
gen_trans_vvvd(xvhaddw_du_wu, 32, vhaddw_du_wu)
gen_trans_vvvd(xvhaddw_d_w, 32, vhaddw_d_w)
gen_trans_vvvd(xvhaddw_h_b, 32, vhaddw_h_b)
gen_trans_vvvd(xvhaddw_hu_bu, 32, vhaddw_hu_bu)
gen_trans_vvvd(xvhaddw_q_d, 32, vhaddw_q_d)
gen_trans_vvvd(xvhaddw_qu_du, 32, vhaddw_qu_du)
gen_trans_vvvd(xvhaddw_w_h, 32, vhaddw_w_h)
gen_trans_vvvd(xvhaddw_wu_hu, 32, vhaddw_wu_hu)
gen_trans_vvvd(xvhsubw_du_wu, 32, vhsubw_du_wu)
gen_trans_vvvd(xvhsubw_d_w, 32, vhsubw_d_w)
gen_trans_vvvd(xvhsubw_h_b, 32, vhsubw_h_b)
gen_trans_vvvd(xvhsubw_hu_bu, 32, vhsubw_hu_bu)
gen_trans_vvvd(xvhsubw_q_d, 32, vhsubw_q_d)
gen_trans_vvvd(xvhsubw_qu_du, 32, vhsubw_qu_du)
gen_trans_vvvd(xvhsubw_w_h, 32, vhsubw_w_h)
gen_trans_vvvd(xvhsubw_wu_hu, 32, vhsubw_wu_hu)
gen_trans_vvvd(xvilvh_b, 32, vilvh_b)
gen_trans_vvvd(xvilvh_d, 32, vilvh_d)
gen_trans_vvvd(xvilvh_h, 32, vilvh_h)
gen_trans_vvvd(xvilvh_w, 32, vilvh_w)
gen_trans_vvvd(xvilvl_b, 32, vilvl_b)
gen_trans_vvvd(xvilvl_d, 32, vilvl_d)
gen_trans_vvvd(xvilvl_h, 32, vilvl_h)
gen_trans_vvvd(xvilvl_w, 32, vilvl_w)
static bool trans_xvinsve0_d(CPULoongArchState *env, arg_xvinsve0_d *restrict a) {
    CHECK_FPE(32);
    env->fpr[a->vd].vreg.D[a->imm] = env->fpr[a->vj].vreg.D[0];
    env->pc += 4;
    return true;
}
static bool trans_xvinsve0_w(CPULoongArchState *env, arg_xvinsve0_w *restrict a) {
    CHECK_FPE(32);
    env->fpr[a->vd].vreg.W[a->imm] = env->fpr[a->vj].vreg.W[0];
    env->pc += 4;
    return true;
}
static bool trans_xvld(CPULoongArchState *env, arg_xvld *restrict a) {
    CHECK_FPE(32);
    int32_t ele_cnt = 32 / 8;
    for (int32_t i = 0; i < ele_cnt; i++) {
        env->fpr[a->vd].vreg.D[i] = ld_d(env, add_addr(env->gpr[a->rj], a->imm + (i * 8)));
    }
    env->pc += 4;
    return true;
}
static bool trans_xvldrepl_b(CPULoongArchState *env, arg_xvldrepl_b *restrict a) {CHECK_FPE(32); int8_t data = ld_b(env, add_addr(env->gpr[a->rj], a->imm));for (size_t i = 0; i < 32; i++){env->fpr[a->vd].vreg.B[i] = data;}env->pc += 4;return true;}
static bool trans_xvldrepl_h(CPULoongArchState *env, arg_xvldrepl_h *restrict a) {CHECK_FPE(32); int16_t data = ld_h(env, add_addr(env->gpr[a->rj], a->imm));for (size_t i = 0; i < 16; i++){env->fpr[a->vd].vreg.H[i] = data;}env->pc += 4;return true;}
static bool trans_xvldrepl_w(CPULoongArchState *env, arg_xvldrepl_w *restrict a) {CHECK_FPE(32); int32_t data = ld_w(env, add_addr(env->gpr[a->rj], a->imm));for (size_t i = 0; i < 8; i++){env->fpr[a->vd].vreg.W[i] = data;}env->pc += 4;return true;}
static bool trans_xvldrepl_d(CPULoongArchState *env, arg_xvldrepl_d *restrict a) {CHECK_FPE(32); int64_t data = ld_d(env, add_addr(env->gpr[a->rj], a->imm));for (size_t i = 0; i < 4; i++){env->fpr[a->vd].vreg.D[i] = data;}env->pc += 4;return true;}
static bool trans_xvldx(CPULoongArchState *env, arg_xvldx *restrict a) {
    CHECK_FPE(32);
    int32_t ele_cnt = 32 / 8;
    for (int32_t i = 0; i < ele_cnt; i++) {
        env->fpr[a->vd].vreg.D[i] = ld_d(env, add_addr(env->gpr[a->rj], env->gpr[a->rk] + (i * 8)));
    }
    env->pc += 4;
    return true;
}
gen_trans_vvvd(xvmaddwev_h_b, 32, vmaddwev_h_b)
gen_trans_vvvd(xvmaddwev_w_h, 32, vmaddwev_w_h)
gen_trans_vvvd(xvmaddwev_d_w, 32, vmaddwev_d_w)
gen_trans_vvvd(xvmaddwod_h_b, 32, vmaddwod_h_b)
gen_trans_vvvd(xvmaddwod_w_h, 32, vmaddwod_w_h)
gen_trans_vvvd(xvmaddwod_d_w, 32, vmaddwod_d_w)
gen_trans_vvvd(xvmaddwev_h_bu, 32, vmaddwev_h_bu)
gen_trans_vvvd(xvmaddwev_w_hu, 32, vmaddwev_w_hu)
gen_trans_vvvd(xvmaddwev_d_wu, 32, vmaddwev_d_wu)
gen_trans_vvvd(xvmaddwod_h_bu, 32, vmaddwod_h_bu)
gen_trans_vvvd(xvmaddwod_w_hu, 32, vmaddwod_w_hu)
gen_trans_vvvd(xvmaddwod_d_wu, 32, vmaddwod_d_wu)
gen_trans_vvvd(xvmaddwev_h_bu_b, 32, vmaddwev_h_bu_b)
gen_trans_vvvd(xvmaddwev_w_hu_h, 32, vmaddwev_w_hu_h)
gen_trans_vvvd(xvmaddwev_d_wu_w, 32, vmaddwev_d_wu_w)
gen_trans_vvvd(xvmaddwod_h_bu_b, 32, vmaddwod_h_bu_b)
gen_trans_vvvd(xvmaddwod_w_hu_h, 32, vmaddwod_w_hu_h)
gen_trans_vvvd(xvmaddwod_d_wu_w, 32, vmaddwod_d_wu_w)
static bool trans_xvmaddwev_q_d(CPULoongArchState *env, arg_xvmaddwev_q_d *restrict a) {
    int size = 32;
    int index = 0;
    CHECK_FPE(size);
    for (int i = 0; i < (size / 16); i++) {
        env->fpr[a->vd].vreg.Q[i] += (__int128_t)env->fpr[a->vj].vreg.D[i * 2 + index] * (__int128_t)env->fpr[a->vk].vreg.D[i *2 + index];
    }
    env->pc += 4;
    return true;
}
static bool trans_xvmaddwod_q_d(CPULoongArchState *env, arg_xvmaddwod_q_d *restrict a) {
    int size = 32;
    int index = 1;
    CHECK_FPE(size);
    for (int i = 0; i < (size / 16); i++) {
        env->fpr[a->vd].vreg.Q[i] += (__int128_t)env->fpr[a->vj].vreg.D[i * 2 + index] * (__int128_t)env->fpr[a->vk].vreg.D[i *2 + index];
    }
    env->pc += 4;
    return true;
}
static bool trans_xvmaddwev_q_du(CPULoongArchState *env, arg_xvmaddwev_q_du *restrict a) {
    int size = 32;
    int index = 0;
    CHECK_FPE(size);
    for (int i = 0; i < (size / 16); i++) {
        env->fpr[a->vd].vreg.Q[i] += (__uint128_t)env->fpr[a->vj].vreg.UD[i * 2 + index] * (__uint128_t)env->fpr[a->vk].vreg.UD[i *2 + index];
    }
    env->pc += 4;
    return true;
}
static bool trans_xvmaddwod_q_du(CPULoongArchState *env, arg_xvmaddwod_q_du *restrict a) {
    int size = 32;
    int index = 1;
    CHECK_FPE(size);
    for (int i = 0; i < (size / 16); i++) {
        env->fpr[a->vd].vreg.Q[i] += (__uint128_t)env->fpr[a->vj].vreg.UD[i * 2 + index] * (__uint128_t)env->fpr[a->vk].vreg.UD[i *2 + index];
    }
    env->pc += 4;
    return true;
}
static bool trans_xvmaddwev_q_du_d(CPULoongArchState *env, arg_xvmaddwev_q_du_d *restrict a) {
    int size = 32;
    int index = 0;
    CHECK_FPE(size);
    for (int i = 0; i < (size / 16); i++) {
        env->fpr[a->vd].vreg.Q[i] += (__uint128_t)env->fpr[a->vj].vreg.UD[i * 2 + index] * (__int128_t)env->fpr[a->vk].vreg.D[i *2 + index];
    }
    env->pc += 4;
    return true;
}
static bool trans_xvmaddwod_q_du_d(CPULoongArchState *env, arg_xvmaddwod_q_du_d *restrict a) {
    int size = 32;
    int index = 1;
    CHECK_FPE(size);
    for (int i = 0; i < (size / 16); i++) {
        env->fpr[a->vd].vreg.Q[i] += (__uint128_t)env->fpr[a->vj].vreg.UD[i * 2 + index] * (__int128_t)env->fpr[a->vk].vreg.D[i *2 + index];
    }
    env->pc += 4;
    return true;
}
static bool trans_xvmskgez_b(CPULoongArchState *env, arg_xvmskgez_b *restrict a) {
    CHECK_FPE(32);
    int oprsz = 32;
    uint32_t desc = simd_desc(oprsz, oprsz, 0);
    helper_vmskgez_b(&env->fpr[a->vd], &env->fpr[a->vj], desc);
    env->pc += 4;
    return true;
}
gen_trans_vvd(xvmskltz_b, 32, vmskltz_b)
gen_trans_vvd(xvmskltz_d, 32, vmskltz_d)
gen_trans_vvd(xvmskltz_h, 32, vmskltz_h)
gen_trans_vvd(xvmskltz_w, 32, vmskltz_w)
static bool trans_xvmsknz_b(CPULoongArchState *env, arg_xvmsknz_b *restrict a) {
    CHECK_FPE(32);
    int oprsz = 32;
    uint32_t desc = simd_desc(oprsz, oprsz, 0);
    helper_vmsknz_b(&env->fpr[a->vd], &env->fpr[a->vj], desc);
    env->pc += 4;
    return true;
}
gen_trans_vvvd(xvmuh_b, 32, vmuh_b)
gen_trans_vvvd(xvmuh_bu, 32, vmuh_bu)
gen_trans_vvvd(xvmuh_d, 32, vmuh_d)
gen_trans_vvvd(xvmuh_du, 32, vmuh_du)
gen_trans_vvvd(xvmuh_h, 32, vmuh_h)
gen_trans_vvvd(xvmuh_hu, 32, vmuh_hu)
gen_trans_vvvd(xvmuh_w, 32, vmuh_w)
gen_trans_vvvd(xvmuh_wu, 32, vmuh_wu)
gen_trans_vvvd(xvmulwev_d_w, 32, vmulwev_d_w)
gen_trans_vvvd(xvmulwev_d_wu, 32, vmulwev_d_wu)
gen_trans_vvvd(xvmulwev_d_wu_w, 32, vmulwev_d_wu_w)
gen_trans_vvvd(xvmulwev_h_b, 32, vmulwev_h_b)
gen_trans_vvvd(xvmulwev_h_bu_b, 32, vmulwev_h_bu_b)
gen_trans_vvvd(xvmulwev_h_bu, 32, vmulwev_h_bu)
gen_trans_vvvd(xvmulwev_w_h, 32, vmulwev_w_h)
gen_trans_vvvd(xvmulwev_w_hu, 32, vmulwev_w_hu)
gen_trans_vvvd(xvmulwev_w_hu_h, 32, vmulwev_w_hu_h)
gen_trans_vvvd(xvmulwod_d_w, 32, vmulwod_d_w)
gen_trans_vvvd(xvmulwod_d_wu, 32, vmulwod_d_wu)
gen_trans_vvvd(xvmulwod_d_wu_w, 32, vmulwod_d_wu_w)
gen_trans_vvvd(xvmulwod_h_b, 32, vmulwod_h_b)
gen_trans_vvvd(xvmulwod_h_bu_b, 32, vmulwod_h_bu_b)
gen_trans_vvvd(xvmulwod_h_bu, 32, vmulwod_h_bu)
gen_trans_vvvd(xvmulwod_w_h, 32, vmulwod_w_h)
gen_trans_vvvd(xvmulwod_w_hu, 32, vmulwod_w_hu)
gen_trans_vvvd(xvmulwod_w_hu_h, 32, vmulwod_w_hu_h)
static bool trans_xvmulwev_q_d(CPULoongArchState *env, arg_xvmulwev_q_d *restrict a) {
    int size = 32;
    int index = 0;
    CHECK_FPE(size);
    for (int i = 0; i < (size / 16); i++) {
        env->fpr[a->vd].vreg.Q[i] = (__int128_t)env->fpr[a->vj].vreg.D[i * 2 + index] * (__int128_t)env->fpr[a->vk].vreg.D[i *2 + index];
    }
    env->pc += 4;
    return true;
}
static bool trans_xvmulwod_q_d(CPULoongArchState *env, arg_xvmulwod_q_d *restrict a) {
    int size = 32;
    int index = 1;
    CHECK_FPE(size);
    for (int i = 0; i < (size / 16); i++) {
        env->fpr[a->vd].vreg.Q[i] = (__int128_t)env->fpr[a->vj].vreg.D[i * 2 + index] * (__int128_t)env->fpr[a->vk].vreg.D[i *2 + index];
    }
    env->pc += 4;
    return true;
}
static bool trans_xvmulwev_q_du(CPULoongArchState *env, arg_xvmulwev_q_du *restrict a) {
    int size = 32;
    int index = 0;
    CHECK_FPE(size);
    for (int i = 0; i < (size / 16); i++) {
        env->fpr[a->vd].vreg.Q[i] = (__uint128_t)env->fpr[a->vj].vreg.UD[i * 2 + index] * (__uint128_t)env->fpr[a->vk].vreg.UD[i *2 + index];
    }
    env->pc += 4;
    return true;
}
static bool trans_xvmulwod_q_du(CPULoongArchState *env, arg_xvmulwod_q_du *restrict a) {
    int size = 32;
    int index = 1;
    CHECK_FPE(size);
    for (int i = 0; i < (size / 16); i++) {
        env->fpr[a->vd].vreg.Q[i] = (__uint128_t)env->fpr[a->vj].vreg.UD[i * 2 + index] * (__uint128_t)env->fpr[a->vk].vreg.UD[i *2 + index];
    }
    env->pc += 4;
    return true;
}
static bool trans_xvmulwev_q_du_d(CPULoongArchState *env, arg_xvmulwev_q_du_d *restrict a) {
    int size = 32;
    int index = 0;
    CHECK_FPE(size);
    for (int i = 0; i < (size / 16); i++) {
        env->fpr[a->vd].vreg.Q[i] = (__uint128_t)env->fpr[a->vj].vreg.UD[i * 2 + index] * (__int128_t)env->fpr[a->vk].vreg.D[i *2 + index];
    }
    env->pc += 4;
    return true;
}
static bool trans_xvmulwod_q_du_d(CPULoongArchState *env, arg_xvmulwod_q_du_d *restrict a) {
    int size = 32;
    int index = 1;
    CHECK_FPE(size);
    for (int i = 0; i < (size / 16); i++) {
        env->fpr[a->vd].vreg.Q[i] = (__uint128_t)env->fpr[a->vj].vreg.UD[i * 2 + index] * (__int128_t)env->fpr[a->vk].vreg.D[i *2 + index];
    }
    env->pc += 4;
    return true;
}
gen_trans_vvvd(xvpackev_b, 32, vpackev_b)
gen_trans_vvvd(xvpackev_h, 32, vpackev_h)
gen_trans_vvvd(xvpackev_w, 32, vpackev_w)
gen_trans_vvvd(xvpackev_d, 32, vpackev_d)
gen_trans_vvvd(xvpackod_b, 32, vpackod_b)
gen_trans_vvvd(xvpackod_h, 32, vpackod_h)
gen_trans_vvvd(xvpackod_w, 32, vpackod_w)
gen_trans_vvvd(xvpackod_d, 32, vpackod_d)
gen_trans_vvid(xvpermi_d, 32, vpermi_d)
gen_trans_vvid(xvpermi_w, 32, vpermi_w)
gen_trans_vvid(xvpermi_q, 32, vpermi_q)
gen_trans_vvvd(xvperm_w, 32, vperm_w)
gen_trans_vvid(xvpickve_d, 32, xvpickve_d)
gen_trans_vvid(xvpickve_w, 32, xvpickve_w)
static bool trans_xvrepl128vei_b(CPULoongArchState *env, arg_xvrepl128vei_b *restrict a) {CHECK_FPE(32); uint32_t ele_cnt = 16 / 1; for (int i = 0; i < ele_cnt; i ++) { env->fpr[a->vd].vreg.B[i] = env->fpr[a->vj].vreg.B[a->imm]; env->fpr[a->vd].vreg.B[i + ele_cnt] = env->fpr[a->vj].vreg.B[a->imm + ele_cnt]; } env->pc += 4; return true;}
static bool trans_xvrepl128vei_h(CPULoongArchState *env, arg_xvrepl128vei_h *restrict a) {CHECK_FPE(32); uint32_t ele_cnt = 16 / 2; for (int i = 0; i < ele_cnt; i ++) { env->fpr[a->vd].vreg.H[i] = env->fpr[a->vj].vreg.H[a->imm]; env->fpr[a->vd].vreg.H[i + ele_cnt] = env->fpr[a->vj].vreg.H[a->imm + ele_cnt]; } env->pc += 4; return true;}
static bool trans_xvrepl128vei_w(CPULoongArchState *env, arg_xvrepl128vei_w *restrict a) {CHECK_FPE(32); uint32_t ele_cnt = 16 / 4; for (int i = 0; i < ele_cnt; i ++) { env->fpr[a->vd].vreg.W[i] = env->fpr[a->vj].vreg.W[a->imm]; env->fpr[a->vd].vreg.W[i + ele_cnt] = env->fpr[a->vj].vreg.W[a->imm + ele_cnt]; } env->pc += 4; return true;}
static bool trans_xvrepl128vei_d(CPULoongArchState *env, arg_xvrepl128vei_d *restrict a) {CHECK_FPE(32); uint32_t ele_cnt = 16 / 8; for (int i = 0; i < ele_cnt; i ++) { env->fpr[a->vd].vreg.D[i] = env->fpr[a->vj].vreg.D[a->imm]; env->fpr[a->vd].vreg.D[i + ele_cnt] = env->fpr[a->vj].vreg.D[a->imm + ele_cnt]; } env->pc += 4; return true;}
static bool trans_xvreplve0_b(CPULoongArchState *env, arg_xvreplve0_b *restrict a) {CHECK_FPE(32); int32_t ele_cnt = 32 / 1; for (int32_t i = 0; i < ele_cnt; i++) { env->fpr[a->vd].vreg.B[i] = env->fpr[a->vj].vreg.B[0]; } env->pc += 4; return true;}
static bool trans_xvreplve0_h(CPULoongArchState *env, arg_xvreplve0_h *restrict a) {CHECK_FPE(32); int32_t ele_cnt = 32 / 2; for (int32_t i = 0; i < ele_cnt; i++) { env->fpr[a->vd].vreg.H[i] = env->fpr[a->vj].vreg.H[0]; } env->pc += 4; return true;}
static bool trans_xvreplve0_w(CPULoongArchState *env, arg_xvreplve0_w *restrict a) {CHECK_FPE(32); int32_t ele_cnt = 32 / 4; for (int32_t i = 0; i < ele_cnt; i++) { env->fpr[a->vd].vreg.W[i] = env->fpr[a->vj].vreg.W[0]; } env->pc += 4; return true;}
static bool trans_xvreplve0_d(CPULoongArchState *env, arg_xvreplve0_d *restrict a) {CHECK_FPE(32); int32_t ele_cnt = 32 / 8; for (int32_t i = 0; i < ele_cnt; i++) { env->fpr[a->vd].vreg.D[i] = env->fpr[a->vj].vreg.D[0]; } env->pc += 4; return true;}
static bool trans_xvreplve0_q(CPULoongArchState *env, arg_xvreplve0_q *restrict a) {
    CHECK_FPE(32);
    env->fpr[a->vd].vreg.D[0] = env->fpr[a->vj].vreg.D[0];
    env->fpr[a->vd].vreg.D[1] = env->fpr[a->vj].vreg.D[1];
    env->fpr[a->vd].vreg.D[2] = env->fpr[a->vj].vreg.D[0];
    env->fpr[a->vd].vreg.D[3] = env->fpr[a->vj].vreg.D[1];
    env->pc += 4;
    return true;
}
static bool trans_xvreplve_b(CPULoongArchState *env, arg_xvreplve_b *restrict a) {CHECK_FPE(32); int32_t ele_cnt = 16 / 1; for (int i = 0; i < ele_cnt; i ++) { env->fpr[a->vd].vreg.B[i] = env->fpr[a->vj].vreg.B[env->gpr[a->rk] & (ele_cnt - 1)]; env->fpr[a->vd].vreg.B[i + ele_cnt] = env->fpr[a->vj].vreg.B[(env->gpr[a->rk] & (ele_cnt - 1)) + ele_cnt]; } env->pc += 4; return true;}
static bool trans_xvreplve_h(CPULoongArchState *env, arg_xvreplve_h *restrict a) {CHECK_FPE(32); int32_t ele_cnt = 16 / 2; for (int i = 0; i < ele_cnt; i ++) { env->fpr[a->vd].vreg.H[i] = env->fpr[a->vj].vreg.H[env->gpr[a->rk] & (ele_cnt - 1)]; env->fpr[a->vd].vreg.H[i + ele_cnt] = env->fpr[a->vj].vreg.H[(env->gpr[a->rk] & (ele_cnt - 1)) + ele_cnt]; } env->pc += 4; return true;}
static bool trans_xvreplve_w(CPULoongArchState *env, arg_xvreplve_w *restrict a) {CHECK_FPE(32); int32_t ele_cnt = 16 / 4; for (int i = 0; i < ele_cnt; i ++) { env->fpr[a->vd].vreg.W[i] = env->fpr[a->vj].vreg.W[env->gpr[a->rk] & (ele_cnt - 1)]; env->fpr[a->vd].vreg.W[i + ele_cnt] = env->fpr[a->vj].vreg.W[(env->gpr[a->rk] & (ele_cnt - 1)) + ele_cnt]; } env->pc += 4; return true;}
static bool trans_xvreplve_d(CPULoongArchState *env, arg_xvreplve_d *restrict a) {CHECK_FPE(32); int32_t ele_cnt = 16 / 8; for (int i = 0; i < ele_cnt; i ++) { env->fpr[a->vd].vreg.D[i] = env->fpr[a->vj].vreg.D[env->gpr[a->rk] & (ele_cnt - 1)]; env->fpr[a->vd].vreg.D[i + ele_cnt] = env->fpr[a->vj].vreg.D[(env->gpr[a->rk] & (ele_cnt - 1)) + ele_cnt]; } env->pc += 4; return true;}
gen_trans_vvvd(xvrotr_b, 32, gvec_rotr8v)
gen_trans_vvvd(xvrotr_h, 32, gvec_rotr16v)
gen_trans_vvvd(xvrotr_w, 32, gvec_rotr32v)
gen_trans_vvvd(xvrotr_d, 32, gvec_rotr64v)
static bool trans_xvrotri_b(CPULoongArchState *env, arg_xvrotri_b *restrict a) {
    int size = 32;
    CHECK_FPE(size);
    int oprsz = size;
    uint32_t desc = simd_desc(oprsz, oprsz, 8 - a->imm);
    helper_gvec_rotl8i(&env->fpr[a->vd], &env->fpr[a->vj], desc);
    env->pc += 4;
    return true;
}
static bool trans_xvrotri_h(CPULoongArchState *env, arg_xvrotri_h *restrict a) {
    int size = 32;
    CHECK_FPE(size);
    int oprsz = size;
    uint32_t desc = simd_desc(oprsz, oprsz, 16 - a->imm);
    helper_gvec_rotl16i(&env->fpr[a->vd], &env->fpr[a->vj], desc);
    env->pc += 4;
    return true;
}
static bool trans_xvrotri_w(CPULoongArchState *env, arg_xvrotri_w *restrict a) {
    int size = 32;
    CHECK_FPE(size);
    int oprsz = size;
    uint32_t desc = simd_desc(oprsz, oprsz, 32 - a->imm);
    helper_gvec_rotl32i(&env->fpr[a->vd], &env->fpr[a->vj], desc);
    env->pc += 4;
    return true;
}
static bool trans_xvrotri_d(CPULoongArchState *env, arg_xvrotri_d *restrict a) {
    int size = 32;
    CHECK_FPE(size);
    int oprsz = size;
    uint32_t desc = simd_desc(oprsz, oprsz, 64 - a->imm);
    helper_gvec_rotl64i(&env->fpr[a->vd], &env->fpr[a->vj], desc);
    env->pc += 4;
    return true;
}
gen_trans_vvvd(xvsadd_b, 32, gvec_ssadd8)
gen_trans_vvvd(xvsadd_h, 32, gvec_ssadd16)
gen_trans_vvvd(xvsadd_w, 32, gvec_ssadd32)
gen_trans_vvvd(xvsadd_d, 32, gvec_ssadd64)
gen_trans_vvvd(xvsadd_bu, 32, gvec_usadd8)
gen_trans_vvvd(xvsadd_hu, 32, gvec_usadd16)
gen_trans_vvvd(xvsadd_wu, 32, gvec_usadd32)
gen_trans_vvvd(xvsadd_du, 32, gvec_usadd64)
gen_trans_vvid_sat(xvsat_b, 32, vsat_b)
gen_trans_vvid_sat(xvsat_h, 32, vsat_h)
gen_trans_vvid_sat(xvsat_w, 32, vsat_w)
gen_trans_vvid_sat(xvsat_d, 32, vsat_d)
gen_trans_vvid_satu(xvsat_bu, 32, vsat_bu)
gen_trans_vvid_satu(xvsat_hu, 32, vsat_hu)
gen_trans_vvid_satu(xvsat_wu, 32, vsat_wu)
gen_trans_vvid_satu(xvsat_du, 32, vsat_du)
static bool trans_xvseteqz_v(CPULoongArchState *env, arg_xvseteqz_v *restrict a) {
    CHECK_FPE(32);
    uint32_t ele_cnt = 32 / 8;
    bool r = 1;
    for (uint32_t i = 0; i < ele_cnt; i++) {
        r &= (env->fpr[a->vj].vreg.D[i] == 0);
    }
    env->cf[a->cd] = r;
    env->pc += 4;
    return true;
}
static bool trans_xvsetnez_v(CPULoongArchState *env, arg_xvsetnez_v *restrict a) {
    CHECK_FPE(32);
    uint32_t ele_cnt = 32 / 8;
    bool r = 0;
    for (uint32_t i = 0; i < ele_cnt; i++) {
        r |= (env->fpr[a->vj].vreg.D[i] != 0);
    }
    env->cf[a->cd] = r;
    env->pc += 4;
    return true;
}
gen_trans_vvid(xvshuf4i_b, 32, vshuf4i_b)
gen_trans_vvid(xvshuf4i_d, 32, vshuf4i_d)
gen_trans_vvid(xvshuf4i_h, 32, vshuf4i_h)
gen_trans_vvid(xvshuf4i_w, 32, vshuf4i_w)
gen_trans_vvvvd(xvshuf_b, 32, vshuf_b)
gen_trans_vvvd(xvshuf_h, 32, vshuf_h)
gen_trans_vvvd(xvshuf_w, 32, vshuf_w)
gen_trans_vvvd(xvshuf_d, 32, vshuf_d)
gen_trans_vvvd(xvsigncov_b, 32, vsigncov_b)
gen_trans_vvvd(xvsigncov_h, 32, vsigncov_h)
gen_trans_vvvd(xvsigncov_w, 32, vsigncov_w)
gen_trans_vvvd(xvsigncov_d, 32, vsigncov_d)
gen_trans_vvid(xvsllwil_du_wu, 32, vsllwil_du_wu)
gen_trans_vvid(xvsllwil_d_w, 32, vsllwil_d_w)
gen_trans_vvid(xvsllwil_h_b, 32, vsllwil_h_b)
gen_trans_vvid(xvsllwil_hu_bu, 32, vsllwil_hu_bu)
gen_trans_vvid(xvsllwil_w_h, 32, vsllwil_w_h)
gen_trans_vvid(xvsllwil_wu_hu, 32, vsllwil_wu_hu)
gen_trans_vvvd(xvsran_b_h, 32, vsran_b_h)
gen_trans_vvvd(xvsran_h_w, 32, vsran_h_w)
gen_trans_vvid(xvsrani_b_h, 32, vsrani_b_h)
gen_trans_vvid(xvsrani_d_q, 32, vsrani_d_q)
gen_trans_vvid(xvsrani_h_w, 32, vsrani_h_w)
gen_trans_vvid(xvsrani_w_d, 32, vsrani_w_d)
gen_trans_vvvd(xvsran_w_d, 32, vsran_w_d)
gen_trans_vvvd(xvsrar_b, 32, vsrar_b)
gen_trans_vvvd(xvsrar_d, 32, vsrar_d)
gen_trans_vvvd(xvsrar_h, 32, vsrar_h)
gen_trans_vvid(xvsrari_b, 32, vsrari_b)
gen_trans_vvid(xvsrari_d, 32, vsrari_d)
gen_trans_vvid(xvsrari_h, 32, vsrari_h)
gen_trans_vvid(xvsrari_w, 32, vsrari_w)
gen_trans_vvvd(xvsrarn_b_h, 32, vsrarn_b_h)
gen_trans_vvvd(xvsrarn_h_w, 32, vsrarn_h_w)
gen_trans_vvid(xvsrarni_b_h, 32, vsrarni_b_h)
gen_trans_vvid(xvsrarni_d_q, 32, vsrarni_d_q)
gen_trans_vvid(xvsrarni_h_w, 32, vsrarni_h_w)
gen_trans_vvid(xvsrarni_w_d, 32, vsrarni_w_d)
gen_trans_vvvd(xvsrarn_w_d, 32, vsrarn_w_d)
gen_trans_vvvd(xvsrar_w, 32, vsrar_w)
gen_trans_vvvd(xvsrln_b_h, 32, vsrln_b_h)
gen_trans_vvvd(xvsrln_h_w, 32, vsrln_h_w)
gen_trans_vvid(xvsrlni_b_h, 32, vsrlni_b_h)
gen_trans_vvid(xvsrlni_d_q, 32, vsrlni_d_q)
gen_trans_vvid(xvsrlni_h_w, 32, vsrlni_h_w)
gen_trans_vvid(xvsrlni_w_d, 32, vsrlni_w_d)
gen_trans_vvvd(xvsrln_w_d, 32, vsrln_w_d)
gen_trans_vvvd(xvsrlr_b, 32, vsrlr_b)
gen_trans_vvvd(xvsrlr_d, 32, vsrlr_d)
gen_trans_vvvd(xvsrlr_h, 32, vsrlr_h)
gen_trans_vvid(xvsrlri_b, 32, vsrlri_b)
gen_trans_vvid(xvsrlri_d, 32, vsrlri_d)
gen_trans_vvid(xvsrlri_h, 32, vsrlri_h)
gen_trans_vvid(xvsrlri_w, 32, vsrlri_w)
gen_trans_vvvd(xvsrlrn_b_h, 32, vsrlrn_b_h)
gen_trans_vvvd(xvsrlrn_h_w, 32, vsrlrn_h_w)
gen_trans_vvid(xvsrlrni_b_h, 32, vsrlrni_b_h)
gen_trans_vvid(xvsrlrni_d_q, 32, vsrlrni_d_q)
gen_trans_vvid(xvsrlrni_h_w, 32, vsrlrni_h_w)
gen_trans_vvid(xvsrlrni_w_d, 32, vsrlrni_w_d)
gen_trans_vvvd(xvsrlrn_w_d, 32, vsrlrn_w_d)
gen_trans_vvvd(xvsrlr_w, 32, vsrlr_w)
gen_trans_vvvd(xvssran_b_h, 32, vssran_b_h)
gen_trans_vvvd(xvssran_bu_h, 32, vssran_bu_h)
gen_trans_vvvd(xvssran_hu_w, 32, vssran_hu_w)
gen_trans_vvvd(xvssran_h_w, 32, vssran_h_w)
gen_trans_vvid(xvssrani_b_h, 32, vssrani_b_h)
gen_trans_vvid(xvssrani_bu_h, 32, vssrani_bu_h)
gen_trans_vvid(xvssrani_d_q, 32, vssrani_d_q)
gen_trans_vvid(xvssrani_du_q, 32, vssrani_du_q)
gen_trans_vvid(xvssrani_hu_w, 32, vssrani_hu_w)
gen_trans_vvid(xvssrani_h_w, 32, vssrani_h_w)
gen_trans_vvid(xvssrani_w_d, 32, vssrani_w_d)
gen_trans_vvid(xvssrani_wu_d, 32, vssrani_wu_d)
gen_trans_vvvd(xvssran_w_d, 32, vssran_w_d)
gen_trans_vvvd(xvssran_wu_d, 32, vssran_wu_d)
gen_trans_vvvd(xvssrarn_b_h, 32, vssrarn_b_h)
gen_trans_vvvd(xvssrarn_bu_h, 32, vssrarn_bu_h)
gen_trans_vvvd(xvssrarn_hu_w, 32, vssrarn_hu_w)
gen_trans_vvvd(xvssrarn_h_w, 32, vssrarn_h_w)
gen_trans_vvid(xvssrarni_b_h, 32, vssrarni_b_h)
gen_trans_vvid(xvssrarni_bu_h, 32, vssrarni_bu_h)
gen_trans_vvid(xvssrarni_d_q, 32, vssrarni_d_q)
gen_trans_vvid(xvssrarni_du_q, 32, vssrarni_du_q)
gen_trans_vvid(xvssrarni_hu_w, 32, vssrarni_hu_w)
gen_trans_vvid(xvssrarni_h_w, 32, vssrarni_h_w)
gen_trans_vvid(xvssrarni_w_d, 32, vssrarni_w_d)
gen_trans_vvid(xvssrarni_wu_d, 32, vssrarni_wu_d)
gen_trans_vvvd(xvssrarn_w_d, 32, vssrarn_w_d)
gen_trans_vvvd(xvssrarn_wu_d, 32, vssrarn_wu_d)
gen_trans_vvvd(xvssrln_b_h, 32, vssrln_b_h)
gen_trans_vvvd(xvssrln_bu_h, 32, vssrln_bu_h)
gen_trans_vvvd(xvssrln_hu_w, 32, vssrln_hu_w)
gen_trans_vvvd(xvssrln_h_w, 32, vssrln_h_w)
gen_trans_vvid(xvssrlni_b_h, 32, vssrlni_b_h)
gen_trans_vvid(xvssrlni_bu_h, 32, vssrlni_bu_h)
gen_trans_vvid(xvssrlni_d_q, 32, vssrlni_d_q)
gen_trans_vvid(xvssrlni_du_q, 32, vssrlni_du_q)
gen_trans_vvid(xvssrlni_hu_w, 32, vssrlni_hu_w)
gen_trans_vvid(xvssrlni_h_w, 32, vssrlni_h_w)
gen_trans_vvid(xvssrlni_w_d, 32, vssrlni_w_d)
gen_trans_vvid(xvssrlni_wu_d, 32, vssrlni_wu_d)
gen_trans_vvvd(xvssrln_w_d, 32, vssrln_w_d)
gen_trans_vvvd(xvssrln_wu_d, 32, vssrln_wu_d)
gen_trans_vvvd(xvssrlrn_b_h, 32, vssrlrn_b_h)
gen_trans_vvvd(xvssrlrn_bu_h, 32, vssrlrn_bu_h)
gen_trans_vvvd(xvssrlrn_hu_w, 32, vssrlrn_hu_w)
gen_trans_vvvd(xvssrlrn_h_w, 32, vssrlrn_h_w)
gen_trans_vvid(xvssrlrni_b_h, 32, vssrlrni_b_h)
gen_trans_vvid(xvssrlrni_bu_h, 32, vssrlrni_bu_h)
gen_trans_vvid(xvssrlrni_d_q, 32, vssrlrni_d_q)
gen_trans_vvid(xvssrlrni_du_q, 32, vssrlrni_du_q)
gen_trans_vvid(xvssrlrni_hu_w, 32, vssrlrni_hu_w)
gen_trans_vvid(xvssrlrni_h_w, 32, vssrlrni_h_w)
gen_trans_vvid(xvssrlrni_w_d, 32, vssrlrni_w_d)
gen_trans_vvid(xvssrlrni_wu_d, 32, vssrlrni_wu_d)
gen_trans_vvvd(xvssrlrn_w_d, 32, vssrlrn_w_d)
gen_trans_vvvd(xvssrlrn_wu_d, 32, vssrlrn_wu_d)
gen_trans_vvvd(xvssub_b, 32, gvec_sssub8)
gen_trans_vvvd(xvssub_h, 32, gvec_sssub16)
gen_trans_vvvd(xvssub_w, 32, gvec_sssub32)
gen_trans_vvvd(xvssub_d, 32, gvec_sssub64)
gen_trans_vvvd(xvssub_bu, 32, gvec_ussub8)
gen_trans_vvvd(xvssub_hu, 32, gvec_ussub16)
gen_trans_vvvd(xvssub_wu, 32, gvec_ussub32)
gen_trans_vvvd(xvssub_du, 32, gvec_ussub64)
static bool trans_xvst(CPULoongArchState *env, arg_xvst *restrict a) {
    CHECK_FPE(32);
    int32_t ele_cnt = 32 / 8;
    for (int32_t i = 0; i < ele_cnt; i++) {
        st_d(env, add_addr(env->gpr[a->rj], a->imm + (i * 8)), env->fpr[a->vd].vreg.D[i]);
    }
    env->pc += 4;
    return true;
}
static bool trans_xvstelm_b(CPULoongArchState *env, arg_xvstelm_b *restrict a) {CHECK_FPE(32); st_b(env, add_addr(env->gpr[a->rj], a->imm), env->fpr[a->vd].vreg.B[a->imm2]);env->pc += 4;return true;}
static bool trans_xvstelm_h(CPULoongArchState *env, arg_xvstelm_h *restrict a) {CHECK_FPE(32); st_h(env, add_addr(env->gpr[a->rj], a->imm), env->fpr[a->vd].vreg.H[a->imm2]);env->pc += 4;return true;}
static bool trans_xvstelm_w(CPULoongArchState *env, arg_xvstelm_w *restrict a) {CHECK_FPE(32); st_w(env, add_addr(env->gpr[a->rj], a->imm), env->fpr[a->vd].vreg.W[a->imm2]);env->pc += 4;return true;}
static bool trans_xvstelm_d(CPULoongArchState *env, arg_xvstelm_d *restrict a) {CHECK_FPE(32); st_d(env, add_addr(env->gpr[a->rj], a->imm), env->fpr[a->vd].vreg.D[a->imm2]);env->pc += 4;return true;}
static bool trans_xvstx(CPULoongArchState *env, arg_xvstx *restrict a) {
    CHECK_FPE(32);
    int32_t ele_cnt = 32 / 8;
    for (int32_t i = 0; i < ele_cnt; i++) {
        st_d(env, add_addr(env->gpr[a->rj], env->gpr[a->rk] + (i * 8)), env->fpr[a->vd].vreg.D[i]);
    }
    env->pc += 4;
    return true;
}
static bool trans_xvsub_q(CPULoongArchState *env, arg_xvsub_q *restrict a) {
    CHECK_FPE(32);
    env->fpr[a->vd].vreg.Q[0] = env->fpr[a->vj].vreg.Q[0] - env->fpr[a->vk].vreg.Q[0];
    env->fpr[a->vd].vreg.Q[1] = env->fpr[a->vj].vreg.Q[1] - env->fpr[a->vk].vreg.Q[1];
    env->pc += 4;
    return true;
}

gen_trans_vvvd(xvsubwev_d_w, 32, vsubwev_d_w)
gen_trans_vvvd(xvsubwev_d_wu, 32, vsubwev_d_wu)
gen_trans_vvvd(xvsubwev_h_b, 32, vsubwev_h_b)
gen_trans_vvvd(xvsubwev_h_bu, 32, vsubwev_h_bu)
gen_trans_vvvd(xvsubwev_q_d, 32, vsubwev_q_d)
gen_trans_vvvd(xvsubwev_q_du, 32, vsubwev_q_du)
gen_trans_vvvd(xvsubwev_w_h, 32, vsubwev_w_h)
gen_trans_vvvd(xvsubwev_w_hu, 32, vsubwev_w_hu)
gen_trans_vvvd(xvsubwod_d_w, 32, vsubwod_d_w)
gen_trans_vvvd(xvsubwod_d_wu, 32, vsubwod_d_wu)
gen_trans_vvvd(xvsubwod_h_b, 32, vsubwod_h_b)
gen_trans_vvvd(xvsubwod_h_bu, 32, vsubwod_h_bu)
gen_trans_vvvd(xvsubwod_q_d, 32, vsubwod_q_d)
gen_trans_vvvd(xvsubwod_q_du, 32, vsubwod_q_du)
gen_trans_vvvd(xvsubwod_w_h, 32, vsubwod_w_h)
gen_trans_vvvd(xvsubwod_w_hu, 32, vsubwod_w_hu)


bool interpreter(CPULoongArchState *env, uint32_t insn, INSCache* ic) {
    if (ic) {
        ic->trans_func(env, ic->arg);
        env->gpr[0] = 0;
        return true;
    }
    if (decode(env, insn)) {
        env->gpr[0] = 0;
        return true;
    } else {
        return false;
    }
}
