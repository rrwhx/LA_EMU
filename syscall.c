#include "user.h"
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>

#define TARGET_NR_fstatfs 44
#define TARGET_NR_write 64
#define TARGET_NR_readlinkat 78
#define TARGET_NR_fstatat64 79
#define TARGET_NR_fstat64 80
#define TARGET_NR_exit 93
#define TARGET_NR_exit_group 94
#define TARGET_NR_clock_gettime 113
#define TARGET_NR_getuid 174
#define TARGET_NR_geteuid 175
#define TARGET_NR_getgid 176
#define TARGET_NR_getegid 177
#define TARGET_NR_gettid 178
#define TARGET_NR_brk 214

static abi_ulong target_brk, initial_target_brk;
void target_set_brk(abi_ulong new_brk)
{
    target_brk = TARGET_PAGE_ALIGN(new_brk);
    initial_target_brk = target_brk;
    fprintf(stderr, "initial_target_brk:%lx\n", initial_target_brk);
}

/* do_brk() must return target values and target errnos. */
abi_long do_brk(abi_ulong brk_val)
{
    abi_long mapped_addr;
    abi_ulong new_brk;
    abi_ulong old_brk;

    /* brk pointers are always untagged */

    /* do not allow to shrink below initial brk value */
    if (brk_val < initial_target_brk) {
        return target_brk;
    }

    new_brk = TARGET_PAGE_ALIGN(brk_val);
    old_brk = TARGET_PAGE_ALIGN(target_brk);

    /* new and old target_brk might be on the same page */
    if (new_brk == old_brk) {
        target_brk = brk_val;
        return target_brk;
    }

    /* Release heap if necessary */
    if (new_brk < old_brk) {
        munmap((void*)new_brk, old_brk - new_brk);

        target_brk = brk_val;
        return target_brk;
    }

    mapped_addr = (size_t)mmap((void*)old_brk, new_brk - old_brk,
                              PROT_READ | PROT_WRITE,
                              MAP_FIXED_NOREPLACE | MAP_ANON | MAP_PRIVATE,
                              -1, 0);

    if (mapped_addr == old_brk) {
        target_brk = brk_val;
        return target_brk;
    }

#if defined(TARGET_ALPHA)
    /* We (partially) emulate OSF/1 on Alpha, which requires we
       return a proper errno, not an unchanged brk value.  */
    return -TARGET_ENOMEM;
#endif
    /* For everything else, return the previous break. */
    return target_brk;
}

static int is_proc_myself(const char *filename, const char *entry)
{
    if (!strncmp(filename, "/proc/", strlen("/proc/"))) {
        filename += strlen("/proc/");
        if (!strncmp(filename, "self/", strlen("self/"))) {
            filename += strlen("self/");
        } else if (*filename >= '1' && *filename <= '9') {
            char myself[80];
            snprintf(myself, sizeof(myself), "%d/", getpid());
            if (!strncmp(filename, myself, strlen(myself))) {
                filename += strlen(myself);
            } else {
                return 0;
            }
        } else {
            return 0;
        }
        if (!strcmp(filename, entry)) {
            return 1;
        }
    }
    return 0;
}
#define VERIFY_NONE  0
#define VERIFY_READ  PAGE_READ
#define VERIFY_WRITE (PAGE_READ | PAGE_WRITE)
void *lock_user(int type, abi_ulong guest_addr, ssize_t len, bool copy)
{
    return (void*)guest_addr;
}

void *lock_user_string(abi_ulong guest_addr)
{
    return (void*)guest_addr;
}

static abi_long do_syscall1(CPUArchState *cpu_env, int num, abi_long arg1,
                            abi_long arg2, abi_long arg3, abi_long arg4,
                            abi_long arg5, abi_long arg6, abi_long arg7,
                            abi_long arg8)
{
    fprintf(stderr, "syscall %d %lx %lx %lx %lx %lx\n", num, arg1, arg2, arg3, arg4, arg5);
    struct stat st;
    abi_long ret;
    void *p;
    switch(num) {
        case TARGET_NR_write:
            ret = write(arg1, (void*)arg2, arg3);
            return ret;
        case TARGET_NR_readlinkat:
            {
                void *p2;
                p  = lock_user_string(arg2);
                p2 = lock_user(VERIFY_WRITE, arg3, arg4, 0);
                if (!p || !p2) {
                    ret = -TARGET_EFAULT;
                } else if (!arg4) {
                    /* Short circuit this for the magic exe check. */
                    ret = -TARGET_EINVAL;
                } else if (is_proc_myself((const char *)p, "exe")) {
                    /*
                    * Don't worry about sign mismatch as earlier mapping
                    * logic would have thrown a bad address error.
                    */
                    ret = MIN(strlen(exec_path), arg4);
                    /* We cannot NUL terminate the string. */
                    memcpy(p2, exec_path, ret);
                } else {
                    ret = readlinkat(arg1, p, p2, arg4);
                }
            }

            return ret;
        case TARGET_NR_fstat64:
            {
                ret = fstat(arg1, &st);
            }
            return ret;
        case TARGET_NR_exit:
        case TARGET_NR_exit_group:
            printf("icount:%ld ic_hit_count:%ld ecount:%ld\n", cpu_env->icount, cpu_env->ic_hit_count, cpu_env->ecount);
            exit(arg1);
            lsassert(0);
        case TARGET_NR_clock_gettime:
            ret = clock_gettime(arg1, (void*)arg2);
            return ret;
        case TARGET_NR_geteuid:
            return geteuid();
        case TARGET_NR_getuid:
            return getuid();
        case TARGET_NR_getgid:
            return getgid();
        case TARGET_NR_getegid:
            return getegid();
        case TARGET_NR_gettid:
            return syscall(178);
        case TARGET_NR_brk:
            return do_brk(arg1);
        default:
            lsassertm(0, "unimplement syscall %d\n", num);
    }
    return -1;
}

abi_long do_syscall(CPUArchState *cpu_env, int num, abi_long arg1, abi_long arg2, abi_long arg3, abi_long arg4, abi_long arg5, abi_long arg6, abi_long arg7, abi_long arg8) {
    abi_long r = do_syscall1(cpu_env, num, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8);
    fprintf(stderr, "syscall ret:%lx\n", r);
    return r;
}