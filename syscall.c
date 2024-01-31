#include "user.h"

#define TARGET_NR_write 64
#define TARGET_NR_exit 93
#define TARGET_NR_exit_group 94
#define TARGET_NR_clock_gettime 113

static abi_long do_syscall1(CPUArchState *cpu_env, int num, abi_long arg1,
                            abi_long arg2, abi_long arg3, abi_long arg4,
                            abi_long arg5, abi_long arg6, abi_long arg7,
                            abi_long arg8)
{
    // fprintf(stderr, "syscall %d %lx %lx %lx %lx %lx\n", num, arg1, arg2, arg3, arg4, arg5);
    abi_long ret;
    switch(num) {
        case TARGET_NR_write:
            ret = write(arg1, (void*)arg2, arg3);
            return ret;
        case TARGET_NR_exit:
        case TARGET_NR_exit_group:
            printf("icount:%ld ic_hit_count:%ld ecount:%ld\n", cpu_env->icount, cpu_env->ic_hit_count, cpu_env->ecount);
            exit(arg1);
            lsassert(0);
        case TARGET_NR_clock_gettime:
            ret = clock_gettime(arg1, (void*)arg2);
            return ret;
        default:
            lsassertm(0, "unimplement syscall %d\n", num);
    }
    return -1;
}

abi_long do_syscall(CPUArchState *cpu_env, int num, abi_long arg1, abi_long arg2, abi_long arg3, abi_long arg4, abi_long arg5, abi_long arg6, abi_long arg7, abi_long arg8) {
    return do_syscall1(cpu_env, num, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8);
}