#ifndef __UTIL_H__
#define __UTIL_H__

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <unistd.h>

#if defined(__x86_64__)
#include <x86intrin.h>

//12700k
#define TSC_HZ 3600000000
static inline long long get_tsc(void) {
    unsigned int temp;
    return __rdtscp(&temp);
}

static inline long long
__attribute__((always_inline))
get_cycle(void) {
    return (long long)(get_tsc() * 1.38888888888888888888);
}

#elif defined(__loongarch__)
#include <larchintrin.h>
// 3a5000
#define TSC_HZ 100000000
static inline long long get_tsc(void) {
    long long r;
    asm volatile(
    "rdtime.d %0, $r0 \n\t"
    :"=r"(r)
    :
    :"memory" 
    );
    return r;
}

static inline long long
__attribute__((always_inline))
get_cycle(void) {

    return get_tsc() * 25;
}

#elif defined(__riscv)
// d1 c906
#define TSC_HZ 24000000
static inline long long get_tsc(void) {
    long long r;
    asm volatile(
    "rdtime %0 \n\t"
    :"=r"(r)
    :
    :"memory" 
    );
    return r;
}

static inline long long
__attribute__((always_inline))
get_cycle(void) {
    long long r;
    asm volatile(
    "rdcycle %0 \n\t"
    :"=r"(r)
    :
    :"memory" 
    );
    return r;
}

static inline long long 
__attribute__((always_inline))
get_instret(void) {
    long long r;
    asm volatile(
    "rdinstret %0 \n\t"
    :"=r"(r)
    :
    :"memory" 
    );
    return r;
}

#else
    #warning "no intrin.h support"
#endif


static inline long long nano_second(void){
    struct timespec _t;
    clock_gettime(CLOCK_REALTIME, &_t);
    return _t.tv_sec * 1000000000ll + _t.tv_nsec;
}

static inline double second(void){
    struct timespec _t;
    clock_gettime(CLOCK_REALTIME, &_t);
    return _t.tv_sec + _t.tv_nsec/1.0e9;
}


#define __4G (4 * 1024 * 1024 * 1024ULL)
#define __4K (4 * 1024ULL)

extern FILE* logfile;
static inline void qemu_log(const char *fmt, ...)
{
    FILE *f = logfile;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
}

#define CPU_LOG_TB_OUT_ASM (1 << 0)
#define CPU_LOG_TB_IN_ASM  (1 << 1)
#define CPU_LOG_TB_OP      (1 << 2)
#define CPU_LOG_TB_OP_OPT  (1 << 3)
#define CPU_LOG_INT        (1 << 4)
#define CPU_LOG_EXEC       (1 << 5)
#define CPU_LOG_PCALL      (1 << 6)
#define CPU_LOG_TB_CPU     (1 << 8)
#define CPU_LOG_RESET      (1 << 9)
#define LOG_UNIMP          (1 << 10)
#define LOG_GUEST_ERROR    (1 << 11)
#define CPU_LOG_MMU        (1 << 12)
#define CPU_LOG_TB_NOCHAIN (1 << 13)
#define CPU_LOG_PAGE       (1 << 14)
#define LOG_TRACE          (1 << 15)
#define CPU_LOG_TB_OP_IND  (1 << 16)
#define CPU_LOG_TB_FPU     (1 << 17)
#define CPU_LOG_PLUGIN     (1 << 18)
/* LOG_STRACE is used for user-mode strace logging. */
#define LOG_STRACE         (1 << 19)
#define LOG_PER_THREAD     (1 << 20)
#define CPU_LOG_TB_VPU     (1 << 21)
#define CPU_LOG_TIMER      (1 << 22)

extern int qemu_loglevel;
static inline bool qemu_loglevel_mask(int mask)
{
    return (qemu_loglevel & mask) != 0;
}

#define qemu_log_mask(MASK, FMT, ...)                   \
    do {                                                \
        if (unlikely(qemu_loglevel_mask(MASK))) {       \
            qemu_log(FMT, ## __VA_ARGS__);              \
        }                                               \
    } while (0)

# define G_NORETURN __attribute__ ((__noreturn__))

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

#endif
