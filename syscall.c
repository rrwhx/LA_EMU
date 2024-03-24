#define _GNU_SOURCE
#include "user.h"
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <asm/unistd.h>
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <sys/uio.h>
#include <sys/times.h>
#include <sys/resource.h>
#include <sys/ioctl.h>

#define TARGET_NR_getcwd 17
#define TARGET_NR_fcntl 25
#define TARGET_NR_ioctl 29
#define TARGET_NR_unlinkat 35
#define TARGET_NR_fstatfs 44
#define TARGET_NR_ftruncate 46
#define TARGET_NR_faccessat 48
#define TARGET_NR_chdir 49
#define TARGET_NR_openat 56
#define TARGET_NR_close 57
#define TARGET_NR_pipe2 59
#define TARGET_NR_getdents64 61
#define TARGET_NR_lseek 62
#define TARGET_NR_read 63
#define TARGET_NR_write 64
#define TARGET_NR_writev 66
#define TARGET_NR_readlinkat 78
#define TARGET_NR_fstatat64 79
#define TARGET_NR_fstat64 80
#define TARGET_NR_exit 93
#define TARGET_NR_exit_group 94
#define TARGET_NR_clock_gettime 113
#define TARGET_NR_rt_sigaction 134
#define TARGET_NR_times 153
#define TARGET_NR_getrusage 165
#define TARGET_NR_umask 166
#define TARGET_NR_gettimeofday 169
#define TARGET_NR_getpid 172
#define TARGET_NR_getppid 173
#define TARGET_NR_getuid 174
#define TARGET_NR_geteuid 175
#define TARGET_NR_getgid 176
#define TARGET_NR_getegid 177
#define TARGET_NR_gettid 178
#define TARGET_NR_sysinfo 179
#define TARGET_NR_brk 214
#define TARGET_NR_munmap 215
#define TARGET_NR_mremap 216
#define TARGET_NR_mmap 222
#define TARGET_NR_prlimit64 261
#define TARGET_NR_renameat2 276


static abi_ulong target_brk, initial_target_brk;
void target_set_brk(abi_ulong new_brk)
{
    target_brk = TARGET_PAGE_ALIGN(new_brk);
    initial_target_brk = target_brk;
    qemu_log_mask(CPU_LOG_PAGE, "initial_target_brk:%lx\n", initial_target_brk);
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

static inline int host_to_target_errno(int host_errno)
{
    switch (host_errno) {
#define E(X)  case X: return TARGET_##X;
        E(EADDRINUSE)
        E(EADDRNOTAVAIL)
        E(EADV)
        E(EAFNOSUPPORT)
        E(EAGAIN)
        E(EALREADY)
        E(EBADE)
        E(EBADFD)
        E(EBADMSG)
        E(EBADR)
        E(EBADRQC)
        E(EBADSLT)
        E(EBFONT)
        E(ECANCELED)
        E(ECHRNG)
        E(ECOMM)
        E(ECONNABORTED)
        E(ECONNREFUSED)
        E(ECONNRESET)
        E(EDEADLK)
        E(EDESTADDRREQ)
        E(EDOTDOT)
        E(EDQUOT)
        E(EHOSTDOWN)
        E(EHOSTUNREACH)
        #ifdef EHWPOISON
        E(EHWPOISON)
        #endif
        E(EIDRM)
        E(EILSEQ)
        E(EINPROGRESS)
        E(EISCONN)
        E(EISNAM)
        #ifdef EKEYEXPIRED
        E(EKEYEXPIRED)
        #endif
        #ifdef EKEYREJECTED
        E(EKEYREJECTED)
        #endif
        #ifdef EKEYREVOKED
        E(EKEYREVOKED)
        #endif
        E(EL2HLT)
        E(EL2NSYNC)
        E(EL3HLT)
        E(EL3RST)
        E(ELIBACC)
        E(ELIBBAD)
        E(ELIBEXEC)
        E(ELIBMAX)
        E(ELIBSCN)
        E(ELNRNG)
        E(ELOOP)
        E(EMEDIUMTYPE)
        E(EMSGSIZE)
        E(EMULTIHOP)
        E(ENAMETOOLONG)
        E(ENAVAIL)
        E(ENETDOWN)
        E(ENETRESET)
        E(ENETUNREACH)
        E(ENOANO)
        E(ENOBUFS)
        E(ENOCSI)
        E(ENODATA)
        #ifdef ENOKEY
        E(ENOKEY)
        #endif
        E(ENOLCK)
        E(ENOLINK)
        E(ENOMEDIUM)
        #ifdef ENOMSG
        E(ENOMSG)
        #endif
        E(ENONET)
        E(ENOPKG)
        E(ENOPROTOOPT)
        E(ENOSR)
        E(ENOSTR)
        E(ENOSYS)
        E(ENOTCONN)
        E(ENOTEMPTY)
        E(ENOTNAM)
        #ifdef ENOTRECOVERABLE
        E(ENOTRECOVERABLE)
        #endif
        E(ENOTSOCK)
        E(ENOTUNIQ)
        E(EOPNOTSUPP)
        E(EOVERFLOW)
        #ifdef EOWNERDEAD
        E(EOWNERDEAD)
        #endif
        E(EPFNOSUPPORT)
        E(EPROTO)
        E(EPROTONOSUPPORT)
        E(EPROTOTYPE)
        E(EREMCHG)
        E(EREMOTE)
        E(EREMOTEIO)
        E(ERESTART)
        #ifdef ERFKILL
        E(ERFKILL)
        #endif
        E(ESHUTDOWN)
        E(ESOCKTNOSUPPORT)
        E(ESRMNT)
        E(ESTALE)
        E(ESTRPIPE)
        E(ETIME)
        E(ETIMEDOUT)
        E(ETOOMANYREFS)
        E(EUCLEAN)
        E(EUNATCH)
        E(EUSERS)
        E(EXFULL)
#undef E
    default:
        return host_errno;
    }
}

abi_long get_errno(abi_long ret)
{
    if (ret == -1)
        return -host_to_target_errno(errno);
    else
        return ret;
}

static inline int is_error(abi_long ret)
{
    return (abi_ulong)ret >= (abi_ulong)(-4096);
}

static abi_long do_mmap(abi_ulong addr, abi_ulong len, int prot,
                        int target_flags, int fd, off_t offset)
{
    return get_errno((abi_long)mmap((void*)addr, len, prot, target_flags, fd, offset));
}

struct target_stat {
    abi_ulong st_dev;
    abi_ulong st_ino;
    unsigned int st_mode;
    unsigned int st_nlink;
    unsigned int st_uid;
    unsigned int st_gid;
    abi_ulong st_rdev;
    abi_ulong __pad1;
    abi_long st_size;
    int st_blksize;
    int __pad2;
    abi_long st_blocks;
    abi_long target_st_atime;
    abi_ulong target_st_atime_nsec;
    abi_long target_st_mtime;
    abi_ulong target_st_mtime_nsec;
    abi_long target_st_ctime;
    abi_ulong target_st_ctime_nsec;
    unsigned int __unused4;
    unsigned int __unused5;
};

static inline abi_long host_to_target_stat64(CPUArchState *cpu_env,
                                             abi_ulong target_addr,
                                             struct stat *host_st)
{
    struct target_stat *p = (void*)target_addr;
    memset(p, 0, sizeof(*p));
    p->st_dev = host_st->st_dev;
    p->st_ino = host_st->st_ino;
    p->st_mode = host_st->st_mode;
    p->st_nlink = host_st->st_nlink;
    p->st_uid = host_st->st_uid;
    p->st_gid = host_st->st_gid;
    p->st_rdev = host_st->st_rdev;
    p->st_size = host_st->st_size;
    p->st_blksize = host_st->st_blksize;
    p->st_blocks = host_st->st_blocks;
    p->target_st_atime = host_st->st_atime;
    p->target_st_mtime = host_st->st_mtime;
    p->target_st_ctime = host_st->st_ctime;
    p->target_st_atime_nsec = host_st->st_atim.tv_nsec;
    p->target_st_mtime_nsec = host_st->st_mtim.tv_nsec;
    p->target_st_ctime_nsec = host_st->st_ctim.tv_nsec;
    return 0;
}

static abi_long do_syscall1(CPUArchState *cpu_env, int num, abi_long arg1,
                            abi_long arg2, abi_long arg3, abi_long arg4,
                            abi_long arg5, abi_long arg6, abi_long arg7,
                            abi_long arg8)
{
    struct stat st;
    abi_long ret;
    void *p;
    switch(num) {
        case TARGET_NR_getcwd:
            {
                char* p = getcwd((void*)arg1, arg2);
                if (!p) {
                    ret = -TARGET_ERANGE;
                } else {
                    ret = strlen(p);
                }
            }
            return ret;
        case TARGET_NR_fcntl:
            return get_errno(fcntl(arg1, arg2, arg3));
        case TARGET_NR_ioctl:
            return get_errno(ioctl(arg1, arg2, arg3));
        case TARGET_NR_faccessat:
            return get_errno(faccessat(arg1, (void*)arg2, arg3, 0));
        case TARGET_NR_chdir:
            return get_errno(chdir((void*)arg1));
        case TARGET_NR_ftruncate:
            return get_errno(ftruncate(arg1, arg2));
        case TARGET_NR_close:
            return get_errno(close(arg1));
        case TARGET_NR_pipe2:
            return get_errno(pipe2((void*)arg1, arg2));
        case TARGET_NR_getdents64:
            fprintf(stderr, "lxy: %s:%d %s warning TARGET_NR_getdents64\n", __FILE__,__LINE__,__func__);
            return get_errno(getdents64(arg1, (void*)arg2, arg3));
        case TARGET_NR_lseek:
            return get_errno(lseek(arg1, arg2, arg3));
        case TARGET_NR_read:
            return get_errno(read(arg1, (void*)arg2, arg3));
        case TARGET_NR_write:
            return get_errno(write(arg1, (void*)arg2, arg3));
        case TARGET_NR_writev:
            return get_errno(writev(arg1, (void*)arg2, arg3));
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
                    ret = get_errno(readlinkat(arg1, p, p2, arg4));
                }
            }
            return ret;
        case TARGET_NR_rt_sigaction:
            fprintf(stderr, "unimp rt_sigaction\n");
            return 0;
        case TARGET_NR_unlinkat:
            ret = get_errno(unlinkat(arg1, (void*)arg2, arg3));
            return ret;
        case TARGET_NR_fstatat64:
            ret = get_errno(fstatat(arg1, (void*)arg2, &st, arg4));
            if (!is_error(ret))
                ret = host_to_target_stat64(cpu_env, arg3, &st);
            return ret;
        case TARGET_NR_fstat64:
            ret = get_errno(fstat(arg1, &st));
            if (!is_error(ret))
                ret = host_to_target_stat64(cpu_env, arg2, &st);
            return ret;
        case TARGET_NR_openat:
            ret = get_errno(openat(arg1, (char*)arg2, arg3, arg4));
            return ret;
        case TARGET_NR_exit:
        case TARGET_NR_exit_group:
            fprintf(stderr, "icount:%ld ic_hit_count:%ld syscall_count:%ld ecount:%ld\n", cpu_env->icount, cpu_env->ic_hit_count, cpu_env->syscall_count, cpu_env->ecount);
            exit(arg1);
            lsassert(0);
        case TARGET_NR_clock_gettime:
            ret = get_errno(clock_gettime(arg1, (void*)arg2));
            return ret;
        case TARGET_NR_times:
            return get_errno(times((void*)arg1));
        case TARGET_NR_getrusage:
            return get_errno(getrusage(arg1, (void*)arg2));
        case TARGET_NR_umask:
            return get_errno(umask(arg1));
        case TARGET_NR_gettimeofday:
            ret = get_errno(gettimeofday((void*)arg1, (void*)arg2));
            return ret;
        case TARGET_NR_getpid:
            return get_errno(getpid());
        case TARGET_NR_getppid:
            return get_errno(getppid());
        case TARGET_NR_geteuid:
            return get_errno(geteuid());
        case TARGET_NR_getuid:
            return get_errno(getuid());
        case TARGET_NR_getgid:
            return get_errno(getgid());
        case TARGET_NR_getegid:
            return get_errno(getegid());
        case TARGET_NR_gettid:
            return get_errno(syscall(__NR_gettid));
        case TARGET_NR_sysinfo:
            return get_errno(sysinfo((void*)arg1));
        case TARGET_NR_brk:
            return do_brk(arg1);
        case TARGET_NR_munmap:
            return get_errno(munmap((void*)arg1, arg2));
        case TARGET_NR_mremap:
            return get_errno((abi_long)mremap((void*)arg1, arg2, arg3, arg4, (void*)arg5));
        case TARGET_NR_mmap:
            return do_mmap(arg1, arg2, arg3, arg4, arg5, arg6);
        case TARGET_NR_prlimit64:
            return get_errno(prlimit(arg1, arg2, (void*)arg3, (void*)arg4));
        case TARGET_NR_renameat2:
            return get_errno(renameat2(arg1, (void*)arg2, arg3, (void*)4, arg5));
        default:
            lsassertm(0, "unimplement syscall %d\n", num);
    }
    return -1;
}

abi_long do_syscall(CPUArchState *cpu_env, int num, abi_long arg1, abi_long arg2, abi_long arg3, abi_long arg4, abi_long arg5, abi_long arg6, abi_long arg7, abi_long arg8) {
    qemu_log_mask(LOG_STRACE, "syscall %d %lx %lx %lx %lx %lx\n", num, arg1, arg2, arg3, arg4, arg5);
    abi_long r = do_syscall1(cpu_env, num, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8);
    qemu_log_mask(LOG_STRACE, "syscall ret:%lx\n", r);
    return r;
}