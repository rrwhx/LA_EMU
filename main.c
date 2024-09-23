#include <stdint.h>
#include <stdio.h>
#define _GNU_SOURCE

#include "qemu/osdep.h"
#include "util.h"

#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/mman.h>
#include <getopt.h>

#include <elf.h>

#include "sizes.h"
#include "cpu.h"
#include "internals.h"

#if defined(CONFIG_GDB)
#include "gdbserver.h"
#endif
#if defined(CONFIG_USER_ONLY)
#include "user.h"
#else
#include "irq.h"
#include "serial.h"
#include "serial_plus.h"
#endif
#if defined(CONFIG_PLUGIN)
#include <dlfcn.h>
#endif

#if defined(CONFIG_PLUGIN)
la_emu_plugin_ops* plugin_ops;
char plugin_name[PATH_MAX];
char plugin_arg[PATH_MAX];
#endif
bool new_abi;
bool determined;
bool hw_ptw;
bool ptw_hw_setVD = true;
bool serial_plus;
#if !defined(CONFIG_USER_ONLY)
SerialState *ss;
timer_t serial_timerid;
volatile sig_atomic_t serial_timer_int;
#endif
__thread CPULoongArchState *current_env;

int gdbserver = 0;
extern int check_signal;
extern int64_t singlestep;

extern void handle_debug_cli(CPULoongArchState *env);
extern void show_register(CPULoongArchState *env);
extern void show_register_fpr(CPULoongArchState *env);
extern void set_fetch_breakpoint(int idx, target_long pc);
extern void restore_checkpoint(CPULoongArchState *env, char* image_dir);
extern void restore_checkpoint_qemu_format(CPULoongArchState *env, char* mem_path, char* cpu_path);

// # define ELF_CLASS  ELFCLASS64

#ifdef CONFIG_CLI
static void sigaction_entry_int(int signal, siginfo_t *si, void *arg) {
    // ucontext_t* c = (ucontext_t*)arg;
    // printf("signal:%d, at address %p\n", signal, si->si_addr);
    if (check_signal > 3) {
        fprintf(stderr, "exit");
        laemu_exit(EXIT_SUCCESS);
    }
    check_signal++;
    return;
}
#endif

#if !defined (CONFIG_USER_ONLY) && !defined (CONFIG_DIFF)
static void sigaction_entry_timer(int signal, siginfo_t *si, void *arg) {
    timer_t id = *((timer_t*)si->si_value.sival_ptr);
    // printf("Caught signal %d,id=%lx -- ", signal,(long)id);
    if (id==current_env->timerid) {
        qemu_log_mask(CPU_LOG_TIMER, "TIMER alarmed, icount:%ld\n", current_env->icount);
        current_env->timer_int = true;
    } else {
        fprintf(stderr, "TIMER, it's somebody else!\n");
    }
}

static void sigaction_entry_serial_timer(int signal, siginfo_t *si, void *arg) {
    timer_t id = *((timer_t*)si->si_value.sival_ptr);
    // printf("Caught signal %d,id=%lx -- ", signal,(long)id);
    if (id==serial_timerid) {
        qemu_log_mask(CPU_LOG_TIMER, "SERIAL TIMER alarmed, icount:%ld\n", current_env->icount);
        serial_timer_int = true;
    } else {
        fprintf(stderr, "TIMER, it's somebody else!\n");
    }
}
static void kernel_setup_signal(void) {
    struct sigaction sa;

    memset(&sa, 0, sizeof(struct sigaction));
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = sigaction_entry_timer;
    sa.sa_flags     = SA_SIGINFO;
    lsassert (sigaction(SIGRTMIN, &sa, NULL) == 0);
}
#endif

#ifdef CONFIG_CLI
static void cli_setup_signal(void) {
    struct sigaction sa;

    memset(&sa, 0, sizeof(struct sigaction));
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = sigaction_entry_int;
    sa.sa_flags     = SA_SIGINFO;
    lsassert (sigaction(SIGINT, &sa, NULL) == 0);
}
#endif

#if !defined (CONFIG_DIFF)
static void setup_signal(void) {
#ifndef CONFIG_USER_ONLY
    kernel_setup_signal();
#endif
#ifdef CONFIG_CLI
    cli_setup_signal();
#endif
}
#endif

static const char * const excp_names[] = {
    [EXCCODE_INT] = "Interrupt",
    [EXCCODE_PIL] = "Page invalid exception for load",
    [EXCCODE_PIS] = "Page invalid exception for store",
    [EXCCODE_PIF] = "Page invalid exception for fetch",
    [EXCCODE_PME] = "Page modified exception",
    [EXCCODE_PNR] = "Page Not Readable exception",
    [EXCCODE_PNX] = "Page Not Executable exception",
    [EXCCODE_PPI] = "Page Privilege error",
    [EXCCODE_ADEF] = "Address error for instruction fetch",
    [EXCCODE_ADEM] = "Address error for Memory access",
    [EXCCODE_SYS] = "Syscall",
    [EXCCODE_BRK] = "Break",
    [EXCCODE_INE] = "Instruction Non-Existent",
    [EXCCODE_IPE] = "Instruction privilege error",
    [EXCCODE_FPD] = "Floating Point Disabled",
    [EXCCODE_FPE] = "Floating Point Exception",
    [EXCCODE_DBP] = "Debug breakpoint",
    [EXCCODE_BCE] = "Bound Check Exception",
    [EXCCODE_SXD] = "128 bit vector instructions Disable exception",
    [EXCCODE_ASXD] = "256 bit vector instructions Disable exception",
};

const char *loongarch_exception_name(int32_t exception)
{
    assert(excp_names[exception]);
    return excp_names[exception];
}

#ifndef CONFIG_USER_ONLY
char* ram;
#endif
uint64_t ram_size = SZ_4G;
char* kernel_filename;
// for checkpoint restore
char* ckpt_mem_filename;
char* ckpt_cpu_filename;

void usage(void) {
#ifndef CONFIG_USER_ONLY
    fprintf(stderr, "la_emu_kernel -m n[G] -k kernel\n");
    fprintf(stderr, "-m Memory size(kernel mode)\n");
    fprintf(stderr, "-k Kernel vmlinux or checkpoint directory(kernel mode)\n");
#else
    fprintf(stderr, "usage: la_emu_user [-d exec,cpu,page,strace,unimp] [-D logfile] program [arguments...]\n");
#endif
    fprintf(stderr, "-d Log info, support: exec,cpu,fpu,int\n");
    fprintf(stderr, "-D Log file\n");
    fprintf(stderr, "-c Check item, support: tlb_mhit\n");
    fprintf(stderr, "-z Determined events\n");
    fprintf(stderr, "-g Enable gdbserver\n");
    fprintf(stderr, "-w Force enable hardware page table walker\n");
    laemu_exit(EXIT_SUCCESS);
}

#if defined(CONFIG_USER_ONLY)
static target_ulong user_setup_stack() {
    void* dst = mmap(NULL, SZ_4G, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    lsassert(dst != MAP_FAILED);
    return (target_ulong)(dst + SZ_4G - 64);
}
#endif

#define elfhdr Elf64_Ehdr
#define elf_shdr Elf64_Shdr
#define elf_phdr Elf64_Phdr
#if !defined (CONFIG_USER_ONLY) && !defined (CONFIG_DIFF)
static char* alloc_ram(uint64_t ram_size) {
    void* start = mmap(NULL, ram_size + SZ_2G, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    lsassert(start != MAP_FAILED);
    void* part1 = mmap(start, SZ_256M, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    lsassert(part1 != MAP_FAILED);
    void* part2 = mmap(start + SZ_2G + SZ_256M, ram_size - SZ_256M, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    lsassert(part2 != MAP_FAILED);
    void* part3 = mmap(start + 0x1c000000, SZ_32M, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    lsassert(part3 != MAP_FAILED);
    return part1;
}

bool addr_in_ram(hwaddr pa) {
    return
        (pa < SZ_256M) ||
        (pa >= SZ_2G + SZ_256M && pa < ram_size + SZ_2G) ||
        (pa >= 0x1c000000 && pa < 0x1c000000 + SZ_32M);
}

bool load_elf(const char* filename, uint64_t* entry_addr) {
    int size, i;
    uint64_t mem_size, file_size;
    uint8_t e_ident[EI_NIDENT];
    uint8_t *data = NULL;
    int ret = 1;
    elfhdr ehdr;
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
                memcpy(ram + (ph->p_paddr & 0xffffffffffff), data, file_size);
                qemu_log_mask(CPU_LOG_PAGE, "%lx, file_size:%lx mem_size:%lx, \n", ph->p_paddr, file_size, mem_size);
            }
            free((void*)data);
        }
    }

fail:
    if (phdr) {
        free(phdr);
    }
    close(fd);
    return ret;
}
#endif

#if defined(CONFIG_USER_ONLY)
char *exec_path;
char real_exec_path[PATH_MAX];
struct image_info info;
abi_ulong e_phoff;
abi_ulong e_phnum;
target_ulong TARGET_PAGE_BITS;
target_ulong TARGET_PAGE_SIZE;
target_ulong TARGET_PAGE_MASK;

bool load_elf_user(const char* filename, uint64_t* entry_addr) {
    int size, i;
    uint8_t e_ident[EI_NIDENT];
    int ret = 1;
    elfhdr ehdr;
    elf_phdr *phdr = NULL, *ph;
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror(filename);
        laemu_exit(EXIT_FAILURE);
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
    e_phoff = ehdr.e_phoff;
    e_phnum = ehdr.e_phnum;
    if (lseek(fd, ehdr.e_phoff, SEEK_SET) != ehdr.e_phoff) {
        goto fail;
    }
    phdr = (elf_phdr *)malloc(size);
    if (!phdr)
        goto fail;
    if (read(fd, phdr, size) != size)
        goto fail;

    uint64_t loaddr = -1, hiaddr = 0;
    for(i = 0; i < ehdr.e_phnum; i++) {
        ph = &phdr[i];
        if (ph->p_type == PT_LOAD) {
            abi_ulong a = ph->p_vaddr & TARGET_PAGE_MASK;
            if (a < loaddr) {
                loaddr = a;
            }
            a = ph->p_vaddr + ph->p_memsz - 1;
            if (a > hiaddr) {
                hiaddr = a;
            }
        } else if (ph->p_type == PT_INTERP) {
            qemu_log("unsupported dynamic elf\n");
            laemu_exit(0);
        }
    }

    abi_ulong load_addr = loaddr;
    if (ehdr.e_type != ET_EXEC) {
        load_addr += 0x4000000;
    }
    #ifndef MAP_FIXED_NOREPLACE
    #define MAP_FIXED_NOREPLACE MAP_FIXED
    #endif
    load_addr = (abi_ulong)mmap((void*)load_addr, (size_t)hiaddr - loaddr + 1, PROT_NONE,
                            MAP_PRIVATE | MAP_ANON | MAP_NORESERVE |
                            (ehdr.e_type == ET_EXEC ? MAP_FIXED_NOREPLACE : 0),
                            -1, 0);
    if ((void*)load_addr == MAP_FAILED) {
        lsassert(0);
    }

    abi_ulong load_bias = load_addr - loaddr;
    info.load_bias = load_bias;
    info.code_offset = load_bias;
    info.data_offset = load_bias;
    info.load_addr = load_addr;
    info.entry = ehdr.e_entry + load_bias;
    info.start_code = -1;
    info.end_code = 0;
    info.start_data = -1;
    info.end_data = 0;
    /* Usual start for brk is after all sections of the main executable. */
    info.brk = TARGET_PAGE_ALIGN(hiaddr + load_bias);
    info.elf_flags = ehdr.e_flags;
    for(i = 0; i < ehdr.e_phnum; i++) {
        ph = &phdr[i];
        if (ph->p_type == PT_LOAD) {
            abi_ulong vaddr, vaddr_po, vaddr_ps, vaddr_ef, vaddr_em;
            int elf_prot = 0;

            if (ph->p_flags & PF_R) {
                elf_prot |= PROT_READ;
            }
            if (ph->p_flags & PF_W) {
                elf_prot |= PROT_WRITE;
            }
            if (ph->p_flags & PF_X) {
                elf_prot |= PROT_EXEC;
            }

            vaddr = load_bias + ph->p_vaddr;
            if (ph->p_align <= TARGET_PAGE_SIZE) {
                vaddr_po = vaddr & ~TARGET_PAGE_MASK;
                vaddr_ps = vaddr & TARGET_PAGE_MASK;
            } else {
                vaddr_po = vaddr & (ph->p_align - 1);
                vaddr_ps = vaddr & ~(ph->p_align - 1);
            }


            vaddr_ef = vaddr + ph->p_filesz;
            vaddr_em = vaddr + ph->p_memsz;
            qemu_log_mask(CPU_LOG_PAGE, "vaddr_po:%lx vaddr_ps:%lx vaddr_ef:%lx vaddr_em:%lx\n", vaddr_po, vaddr_ps, vaddr_ef, vaddr_em);
            if (ph->p_filesz != 0) {
                void* r = mmap((void*)vaddr_ps, ph->p_filesz + vaddr_po,
                                    elf_prot, MAP_PRIVATE | MAP_FIXED,
                                    fd, ph->p_offset - vaddr_po);
                lsassert(r != MAP_FAILED);
            }
            if (vaddr_ef < vaddr_em) {
                uint64_t end = vaddr_ps + ph->p_filesz + vaddr_po;
                memset((void*)end, 0, ROUND_UP(end, TARGET_PAGE_SIZE) - end);
                abi_ulong align_bss = TARGET_PAGE_ALIGN(vaddr_ef);
                abi_ulong end_bss = TARGET_PAGE_ALIGN(vaddr_em);
                if (align_bss != end_bss) {
                    void* r = mmap((void*)align_bss, end_bss - align_bss,
                            elf_prot, MAP_FIXED | MAP_PRIVATE | MAP_ANON, -1, 0);
                    lsassert(r != MAP_FAILED);
                }
            }
            /* Find the full program boundaries.  */
            if (elf_prot & PROT_EXEC) {
                if (vaddr < info.start_code) {
                    info.start_code = vaddr;
                }
                if (vaddr_ef > info.end_code) {
                    info.end_code = vaddr_ef;
                }
            }
            if (elf_prot & PROT_WRITE) {
                if (vaddr < info.start_data) {
                    info.start_data = vaddr;
                }
                if (vaddr_ef > info.end_data) {
                    info.end_data = vaddr_ef;
                }
            }
        }
    }

    if (info.end_data == 0) {
        info.start_data = info.end_code;
        info.end_data = info.end_code;
    }

fail:
    if (phdr) {
        free(phdr);
    }
    close(fd);
    return ret;
}

#endif

void cpu_reset(CPUState* cs) {
    CPULoongArchState *env = cpu_env(cs);
    env->fcsr0_mask = FCSR0_M1 | FCSR0_M2 | FCSR0_M3;
    env->fcsr0 = 0x0;

    int n;
    /* Set csr registers value after reset */
    env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, PLV, 0);
    env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, IE, 0);
    env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, DA, 1);
    env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, PG, 0);
    env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, DATF, 0);
    env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, DATM, 0);

    env->CSR_EUEN = FIELD_DP64(env->CSR_EUEN, CSR_EUEN, FPE, 0);
    env->CSR_EUEN = FIELD_DP64(env->CSR_EUEN, CSR_EUEN, SXE, 0);
    env->CSR_EUEN = FIELD_DP64(env->CSR_EUEN, CSR_EUEN, ASXE, 0);
    env->CSR_EUEN = FIELD_DP64(env->CSR_EUEN, CSR_EUEN, BTE, 0);

    env->CSR_MISC = 0;

    env->CSR_ECFG = FIELD_DP64(env->CSR_ECFG, CSR_ECFG, VS, 0);
    env->CSR_ECFG = FIELD_DP64(env->CSR_ECFG, CSR_ECFG, LIE, 0);

    env->CSR_ESTAT = env->CSR_ESTAT & (~MAKE_64BIT_MASK(0, 2));
    env->CSR_RVACFG = FIELD_DP64(env->CSR_RVACFG, CSR_RVACFG, RBITS, 0);
    env->CSR_CPUID = cs->cpu_index;
    env->CSR_TCFG = FIELD_DP64(env->CSR_TCFG, CSR_TCFG, EN, 0);
    env->CSR_LLBCTL = FIELD_DP64(env->CSR_LLBCTL, CSR_LLBCTL, KLO, 0);
    env->CSR_TLBRERA = FIELD_DP64(env->CSR_TLBRERA, CSR_TLBRERA, ISTLBR, 0);
    env->CSR_MERRCTL = FIELD_DP64(env->CSR_MERRCTL, CSR_MERRCTL, ISMERR, 0);
    env->CSR_TID = cs->cpu_index;

    for (n = 0; n < 4; n++) {
        env->CSR_DMW[n] = FIELD_DP64(env->CSR_DMW[n], CSR_DMW, PLV0, 0);
        env->CSR_DMW[n] = FIELD_DP64(env->CSR_DMW[n], CSR_DMW, PLV1, 0);
        env->CSR_DMW[n] = FIELD_DP64(env->CSR_DMW[n], CSR_DMW, PLV2, 0);
        env->CSR_DMW[n] = FIELD_DP64(env->CSR_DMW[n], CSR_DMW, PLV3, 0);
    }

#ifndef CONFIG_USER_ONLY
    env->pc = 0x1c000000;
    memset(env->tlb, 0, sizeof(env->tlb));
    // if (kvm_enabled()) {
    //     kvm_arch_reset_vcpu(env);
    // }
#endif

#ifdef CONFIG_TCG
    restore_fp_status(env);
#endif
    cs->exception_index = -1;
}

static void loongarch_cpu_do_interrupt(CPUState *cs)
{
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;
    bool update_badinstr = 1;
    int cause = -1;
    bool tlbfill = FIELD_EX64(env->CSR_TLBRERA, CSR_TLBRERA, ISTLBR);
    uint32_t vec_size = FIELD_EX64(env->CSR_ECFG, CSR_ECFG, VS);

    if (cs->exception_index != EXCCODE_INT) {
        qemu_log_mask(CPU_LOG_INT,
                     "%s enter: pc " TARGET_FMT_lx " ERA " TARGET_FMT_lx
                     " TLBRERA " TARGET_FMT_lx " exception: %d (%s)\n",
                     __func__, env->pc, env->CSR_ERA, env->CSR_TLBRERA,
                     cs->exception_index,
                     loongarch_exception_name(cs->exception_index));
    }

    switch (cs->exception_index) {
    case EXCCODE_DBP:
        env->CSR_DBG = FIELD_DP64(env->CSR_DBG, CSR_DBG, DCL, 1);
        env->CSR_DBG = FIELD_DP64(env->CSR_DBG, CSR_DBG, ECODE, 0xC);
        goto set_DERA;
    set_DERA:
        env->CSR_DERA = env->pc;
        env->CSR_DBG = FIELD_DP64(env->CSR_DBG, CSR_DBG, DST, 1);
        set_pc(env, env->CSR_EENTRY + 0x480);
        break;
    case EXCCODE_INT:
        if (FIELD_EX64(env->CSR_DBG, CSR_DBG, DST)) {
            env->CSR_DBG = FIELD_DP64(env->CSR_DBG, CSR_DBG, DEI, 1);
            goto set_DERA;
        }
        QEMU_FALLTHROUGH;
    case EXCCODE_PIF:
    case EXCCODE_ADEF:
        cause = cs->exception_index;
        update_badinstr = 0;
        break;
    case EXCCODE_SYS:
    case EXCCODE_BRK:
    case EXCCODE_INE:
    case EXCCODE_IPE:
    case EXCCODE_FPD:
    case EXCCODE_FPE:
    case EXCCODE_SXD:
    case EXCCODE_ASXD:
    case EXCCODE_BCE:
    case EXCCODE_ADEM:
    case EXCCODE_PIL:
    case EXCCODE_PIS:
    case EXCCODE_PME:
    case EXCCODE_PNR:
    case EXCCODE_PNX:
    case EXCCODE_PPI:
        cause = cs->exception_index;
        break;
    default:
        qemu_log("Error: exception(%d) has not been supported\n",
                 cs->exception_index);
        abort();
    }

    if (update_badinstr) {
        env->CSR_BADI = cpu_ldl_code(env, env->pc);
    }

    /* Save PLV and IE */
    if (tlbfill) {
        env->CSR_TLBRPRMD = FIELD_DP64(env->CSR_TLBRPRMD, CSR_TLBRPRMD, PPLV,
                                       FIELD_EX64(env->CSR_CRMD,
                                       CSR_CRMD, PLV));
        env->CSR_TLBRPRMD = FIELD_DP64(env->CSR_TLBRPRMD, CSR_TLBRPRMD, PIE,
                                       FIELD_EX64(env->CSR_CRMD, CSR_CRMD, IE));
        /* set the DA mode */
        env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, DA, 1);
        env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, PG, 0);
        env->CSR_TLBRERA = FIELD_DP64(env->CSR_TLBRERA, CSR_TLBRERA,
                                      PC, (env->pc >> 2));
    } else {
        if (cause != EXCCODE_INT || (cause == EXCCODE_INT && FIELD_EX64(env->CSR_ECFG, CSR_ECFG, VS) == 0)) {
            env->CSR_ESTAT = FIELD_DP64(env->CSR_ESTAT, CSR_ESTAT, ECODE,
                                    EXCODE_MCODE(cause));
            env->CSR_ESTAT = FIELD_DP64(env->CSR_ESTAT, CSR_ESTAT, ESUBCODE,
                                    EXCODE_SUBCODE(cause));
        }
        env->CSR_PRMD = FIELD_DP64(env->CSR_PRMD, CSR_PRMD, PPLV,
                                   FIELD_EX64(env->CSR_CRMD, CSR_CRMD, PLV));
        env->CSR_PRMD = FIELD_DP64(env->CSR_PRMD, CSR_PRMD, PIE,
                                   FIELD_EX64(env->CSR_CRMD, CSR_CRMD, IE));
        env->CSR_ERA = env->pc;
    }

    env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, PLV, 0);
    env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, IE, 0);

    if (vec_size) {
        vec_size = (1 << vec_size) * 4;
    }

    if  (cs->exception_index == EXCCODE_INT) {
        env->irq_count ++;
        /* Interrupt */
        uint32_t vector = 0;
        uint32_t pending = FIELD_EX64(env->CSR_ESTAT, CSR_ESTAT, IS);
        pending &= FIELD_EX64(env->CSR_ECFG, CSR_ECFG, LIE);

        /* Find the highest-priority interrupt. */
        vector = 31 - clz32(pending);
        set_pc(env, env->CSR_EENTRY + \
               (EXCCODE_EXTERNAL_INT + vector) * vec_size);
        qemu_log_mask(CPU_LOG_INT,
                      "%s: PC " TARGET_FMT_lx " ERA " TARGET_FMT_lx
                      " cause %d\n" "    A " TARGET_FMT_lx " D "
                      TARGET_FMT_lx " vector = %d ExC " TARGET_FMT_lx "ExS"
                      TARGET_FMT_lx "\n",
                      __func__, env->pc, env->CSR_ERA,
                      cause, env->CSR_BADV, env->CSR_DERA, vector,
                      env->CSR_ECFG, env->CSR_ESTAT);
    } else {
        if (tlbfill) {
            env->tlbr_count ++;
            set_pc(env, env->CSR_TLBRENTRY);
        } else {
            env->ecounter[cs->exception_index] ++;
            set_pc(env, env->CSR_EENTRY + EXCODE_MCODE(cause) * vec_size);
        }
        qemu_log_mask(CPU_LOG_INT,
                      "%s: PC " TARGET_FMT_lx " ERA " TARGET_FMT_lx
                      " cause %d%s\n, ESTAT " TARGET_FMT_lx
                      " EXCFG " TARGET_FMT_lx " BADVA " TARGET_FMT_lx
                      "BADI " TARGET_FMT_lx " SYS_NUM " TARGET_FMT_lu
                      " cpu %d asid " TARGET_FMT_lx "\n", __func__, env->pc,
                      tlbfill ? env->CSR_TLBRERA : env->CSR_ERA,
                      cause, tlbfill ? "(refill)" : "", env->CSR_ESTAT,
                      env->CSR_ECFG,
                      tlbfill ? env->CSR_TLBRBADV : env->CSR_BADV,
                      env->CSR_BADI, env->gpr[11], cs->cpu_index,
                      env->CSR_ASID);
    }
    cs->exception_index = -1;
}


void loongarch_cpu_set_irq(void *opaque, int irq, int level)
{
    CPULoongArchState *env = opaque;

    if (irq < 0 || irq >= N_IRQS) {
        lsassert(0);
        return;
    }

    env->CSR_ESTAT = deposit64(env->CSR_ESTAT, irq, 1, level != 0);
}

static uint32_t fetch(CPULoongArchState *env, INSCache** ic) {
#if defined(CONFIG_USER_ONLY)
        uint32_t insn = ram_lduw(env->pc);
        *ic = cpu_get_ic(env, insn);
        return insn;

#else
    int insn;
    hwaddr ha;
    int prot;
    uint64_t addr = env->pc;
    int tc_index = TC_INDEX(addr);
    TLBCache* tc = env->tc_fetch + tc_index;
    uint64_t page_addr = addr & TARGET_PAGE_MASK;
    if (likely(page_addr == tc->va)) {
        ha = (addr & (TARGET_PAGE_SIZE - 1)) | tc->pa;
    } else {
        int mmu_idx = FIELD_EX64(env->CSR_CRMD, CSR_CRMD, PLV) == 0 ? MMU_KERNEL_IDX : MMU_USER_IDX;
        check_get_physical_address(env, &ha, &prot, addr, MMU_INST_FETCH, mmu_idx);
        // fprintf(stderr, "va:%lx,pa:%lx\n", addr, ha);
        tc->va = page_addr;
        tc->pa = ha & TARGET_PAGE_MASK;
    }
    insn = ram_lduw(ha);
    *ic = cpu_get_ic(env, insn);
    return insn;
#endif
}

int val;

int exec_env(CPULoongArchState *env) {
    INSCache* ic;
    current_env = env;
    CPUState* cs = env_cpu(env);
    while (1) {
        if (sigsetjmp(env_cpu(env)->jmp_env, 0) == 0) {
            uint32_t insn;
            while(1) {

#if defined (CONFIG_CLI)
                handle_debug_cli(env);
#endif

#if defined (CONFIG_DIFF)
                if (singlestep == 0) {
                    return 0;
                }
#endif

#if !defined (CONFIG_USER_ONLY)
#if !defined (CONFIG_DIFF)
                loongarch_cpu_check_irq(env);
#endif
                if (unlikely(loongarch_cpu_has_irq(env))) {
                    cs->exception_index = EXCCODE_INT;
                    loongarch_cpu_do_interrupt(cs);
                }
#endif

#if defined (CONFIG_GDB)
                if (gdbserver_has_message) {
                    return 1;
                }
                for (int i = 0; i < GDB_FETCCH_BREAKPOINT_NUM; i++) {
                    if (env->pc == gdb_fetch_breakpoint[i]) {
                        fprintf(stderr, "hit breakpoint %lx\n", env->pc);
                        return 2;
                    }
                }
#endif

                if (unlikely(qemu_loglevel_mask(CPU_LOG_EXEC))) {
                    qemu_log("pc:%lx\n", env->pc);
                }
                if (unlikely(qemu_loglevel_mask(CPU_LOG_TB_CPU))) {
                    show_register(env);
                    if (unlikely(qemu_loglevel_mask(CPU_LOG_TB_FPU))) {
                        show_register_fpr(env);
                    }
                }
                insn = fetch(env, &ic);
#ifdef CONFIG_DIFF
                env->insn = insn;
                env->prev_pc = env->pc;
#endif
#if defined(CONFIG_PLUGIN)
            if (plugin_ops && plugin_ops->emu_insn_before) {
                plugin_ops->emu_insn_before(env, env->pc, insn);
            }
#endif
                int r = interpreter(env, insn, ic);
                if(unlikely(!r)) {
                    qemu_log("ill instruction, pc:%lx insn:%08x\n", env->pc, insn);
                }

                // need update after fetch and exec so exception would not cause singlestep and icount change
#if defined (CONFIG_DIFF) || defined (CONFIG_CLI)
                -- singlestep;
#endif
                env->icount ++;
                PERF_INC(COUNTER_INST);

            }
        } else {
            loongarch_cpu_do_interrupt(cs);
            env->ecount ++;
        }
    }
}

G_NORETURN void cpu_loop_exit(CPUState *cpu)
{
    /* Undo the setting in cpu_tb_exec.  */
    cpu->neg.can_do_io = true;
    /* Undo any setting in generated code.  */
    // qemu_plugin_disable_mem_helpers(cpu);
    siglongjmp(cpu->jmp_env, 1);
}

void G_NORETURN do_raise_exception(CPULoongArchState *env,
                                   uint32_t exception,
                                   uintptr_t pc)
{
    CPUState *cs = env_cpu(env);
    cpu_clear_tc(env);

    qemu_log_mask(CPU_LOG_INT, "%s: %d (%s)\n",
                  __func__,
                  exception,
                  loongarch_exception_name(exception));
    cs->exception_index = exception;

    cpu_loop_exit(cs);
    // cpu_loop_exit_restore(cs, pc);
}

int qemu_loglevel;
FILE* logfile;
void handle_logfile(const char* filename) {
    logfile = fopen(optarg, "w");
    if (!logfile) {
        fprintf(stderr, "can not open logfile %s\n", filename);
        laemu_exit(EXIT_FAILURE);
    }
}

/* define log items */
typedef struct QEMULogItem {
    int mask;
    const char *name;
    const char *help;
} QEMULogItem;

const QEMULogItem qemu_log_items[] = {
    { CPU_LOG_TB_OUT_ASM, "out_asm",
      "show generated host assembly code for each compiled TB" },
    { CPU_LOG_TB_IN_ASM, "in_asm",
      "show target assembly code for each compiled TB" },
    { CPU_LOG_TB_OP, "op",
      "show micro ops for each compiled TB" },
    { CPU_LOG_TB_OP_OPT, "op_opt",
      "show micro ops after optimization" },
    { CPU_LOG_TB_OP_IND, "op_ind",
      "show micro ops before indirect lowering" },
    { CPU_LOG_INT, "int",
      "show interrupts/exceptions in short format" },
    { CPU_LOG_EXEC, "exec",
      "show trace before each executed TB (lots of logs)" },
    { CPU_LOG_TB_CPU, "cpu",
      "show CPU registers before entering a TB (lots of logs)" },
    { CPU_LOG_TB_FPU, "fpu",
      "include FPU registers in the 'cpu' logging" },
    { CPU_LOG_MMU, "mmu",
      "log MMU-related activities" },
    { CPU_LOG_PCALL, "pcall",
      "x86 only: show protected mode far calls/returns/exceptions" },
    { CPU_LOG_RESET, "cpu_reset",
      "show CPU state before CPU resets" },
    { LOG_UNIMP, "unimp",
      "log unimplemented functionality" },
    { LOG_GUEST_ERROR, "guest_errors",
      "log when the guest OS does something invalid (eg accessing a\n"
      "non-existent register)" },
    { CPU_LOG_PAGE, "page",
      "dump pages at beginning of user mode emulation" },
    { CPU_LOG_TB_NOCHAIN, "nochain",
      "do not chain compiled TBs so that \"exec\" and \"cpu\" show\n"
      "complete traces" },
    { CPU_LOG_PLUGIN, "plugin", "output from TCG plugins"},
    { LOG_STRACE, "strace",
      "log every user-mode syscall, its input, and its result" },
    { LOG_PER_THREAD, "tid",
      "open a separate log file per thread; filename must contain '%d'" },
    { CPU_LOG_TB_VPU, "vpu",
      "include VPU registers in the 'cpu' logging" },
    { CPU_LOG_TIMER, "timer",
      "log timer amd timer csr read/write" },
    { CPU_LOG_PTW, "ptw",
      "log Page Table Walker" },
    { 0, NULL, NULL },
};

void handle_logmask(const char* str) {
    const QEMULogItem *item;
    const char *start = str, *p;
    while (1) {
        p = strchr(start, ',');
        if (!p) {
            p = start + strlen(start);
        }
        for (item = qemu_log_items; item->mask != 0; item++) {
            if (strncmp(start, item->name, p - start) == 0) {
                qemu_loglevel |= item->mask;
                break;
            }
        }
        if (item->mask == 0) {
            fprintf(stderr, "unable to prase %s\n", start);
            laemu_exit(EXIT_FAILURE);
        }
        if (*p) {
            start = p + 1;
        } else {
            break;
        }
    };
}

int check_level;
typedef struct CheckItem {
    int mask;
    const char *name;
    const char *help;
} CheckItem;

const CheckItem check_items[] = {
    { CPU_CHECK_TLB_MHIT, "tlb_mhit",
      "check tlb multi-hit when refill tlb" },
    { 0, NULL, NULL },
};

void handle_checkmask(const char* str) {
    const CheckItem *item;
    const char *start = str, *p;
    while (1) {
        p = strchr(start, ',');
        if (!p) {
            p = start + strlen(start);
        }
        for (item = check_items; item->mask != 0; item++) {
            if (strncmp(start, item->name, p - start) == 0) {
                check_level |= item->mask;
                break;
            }
        }
        if (item->mask == 0) {
            fprintf(stderr, "unable to prase %s\n", start);
            laemu_exit(EXIT_FAILURE);
        }
        if (*p) {
            start = p + 1;
        } else {
            break;
        }
    };
}

#if !defined(CONFIG_USER_ONLY)
void do_io_st(hwaddr ha, uint64_t data, int size) {
    switch (ha)
    {
    case UART_BASE ... UART_END:
        if (serial_plus) {
            serial_plus_ioport_write(ss, ha - UART_BASE, data, size);
        } else {
            serial_ioport_write(NULL, ha - UART_BASE, data, size);
        }
        break;
    case 0x1fe002e0:
            fprintf(stderr, "%c", (char)(data));
            fflush(stdout);
        break;

    case 0x100d0014:
        fprintf(stderr,"lxy: %s:%d %s poweroff@100d0014 data:%x\n",__FILE__, __LINE__, __FUNCTION__, (int)data);
        if ((data & 0x3c00) == 0x3c00) {
            dump_exec_info(current_env, stderr);
#if defined(CONFIG_PERF)
            perf_report(current_env, stderr);
#endif
            laemu_exit(0);
        }
        break;
    default:
        fprintf(stderr, "do_io_st, pc:%lx, addr:%lx, data:%lx, size:%d\n", current_env->pc, ha, data, size);
        // lsassert(0);
    }
}
uint64_t do_io_ld(hwaddr ha, int size) {
    uint64_t data = 'x';
    switch (ha)
    {
    case UART_BASE ... UART_END:
        if (serial_plus) {
            data = serial_plus_ioport_read(ss, ha - UART_BASE, size);
        } else {
            data = serial_ioport_read(NULL, ha - UART_BASE, size);
        }
        break;
    case 0x1fe00120:
            data = 'a';
        break;
    case 0x100d0014:
        data = 0;
        break;
    default:
        fprintf(stderr, "do_io_ld, addr:%lx, size:%d\n", ha, size);
        break;
    }
    return data;
}

void loongarch_cpu_check_irq(CPULoongArchState *env) {
    if (determined) {
        env->timer_counter -= (env->CSR_TCFG & CONSTANT_TIMER_ENABLE);
        if (env->timer_counter == 0) {
            env->timer_counter = INT64_MAX;
            loongarch_cpu_set_irq(env, IRQ_TIMER, 1);
            if (FIELD_EX64(env->CSR_TCFG, CSR_TCFG, PERIODIC)) {
                env->timer_counter = (env->CSR_TCFG & CONSTANT_TIMER_TICK_MASK) / TIME_SCALE;
            } else {
                env->CSR_TCFG = FIELD_DP64(env->CSR_TCFG, CSR_TCFG, EN, 0);
            }
        }
    } else {
        if (unlikely(env->timer_int)) {
            env->timer_int = false;
            loongarch_cpu_set_irq(env, IRQ_TIMER, 1);
            if (FIELD_EX64(env->CSR_TCFG, CSR_TCFG, PERIODIC)) {
                cpu_settimer(env, env->CSR_TCFG & CONSTANT_TIMER_TICK_MASK);
            } else {
                env->CSR_TCFG = FIELD_DP64(env->CSR_TCFG, CSR_TCFG, EN, 0);
            }
        }
    }
    // always false when disable serial_plus
    if (unlikely(serial_timer_int)) {
        serial_timer_int = false;
        serial_check_io(ss);
    }
}

bool loongarch_cpu_has_irq(CPULoongArchState *env) {
    return FIELD_EX64(env->CSR_CRMD, CSR_CRMD, IE) && (FIELD_EX64(env->CSR_ESTAT, CSR_ESTAT, IS) & FIELD_EX64(env->CSR_ECFG, CSR_ECFG, LIE));
}
#endif

#ifndef CONFIG_DIFF

struct option long_options[] = {
    {"ckpt-mem", required_argument, 0, 0},
    {"ckpt-cpu", required_argument, 0, 0},
    {0, 0 ,0 ,0}
};

int main(int argc, char** argv, char **envp) {
    logfile = stderr;
    if (argc < 2) {
        usage();
    }
    int c;
    int long_option_idx = 0;
    while ((c = getopt_long(argc, argv, "+m:nk:d:c:D:gzwsp:", long_options, &long_option_idx)) != -1) {
        switch (c) {
            case 'm':
                ram_size = atol(optarg) << 30;
                break;
            case 'n':
                new_abi = true;
                break;
            case 'k':
                kernel_filename = optarg;
                break;
            case 'd':
                handle_logmask(optarg);
                break;
            case 'c':
                handle_checkmask(optarg);
                break;
            case 'D':
                handle_logfile(optarg);
                break;
            case 'g':
#if !defined (CONFIG_GDB)
                fprintf(stderr, "please make GDB=1\n");
                laemu_exit(0);
#endif
                gdbserver = 1;
                break;
            case 'z':
                determined = 1;
                break;
            case 'w':
                hw_ptw = 1;
                break;
            case 's':
                serial_plus = 1;
                break;
            case 'p':{
#if defined(CONFIG_PLUGIN)
                char * p = strchr(optarg, ',');
                if (!p) {
                    strncpy(plugin_name, optarg, PATH_MAX);
                } else {
                    strncpy(plugin_name, optarg, p - optarg);
                    strncpy(plugin_arg, p + 1, PATH_MAX);
                }
                printf("plugin_name:%s\n", plugin_name);
                printf("plugin_arg:%s\n", plugin_arg);
#else
                fprintf(stderr, "please make PLUGIN=1\n");
                laemu_exit(0);
#endif
            }
                break;
            case 0: // deal long options
#if !defined (CONFIG_USER_ONLY)
                if (strcmp(long_options[long_option_idx].name, "ckpt-mem") == 0) {
                    ckpt_mem_filename = optarg;
                } else if (strcmp(long_options[long_option_idx].name, "ckpt-cpu") == 0) {
                    ckpt_cpu_filename = optarg;
                } else {
                    usage();
                    return 1;
                }
#endif
                break;
            case '?':
                usage();
                return 1;
            default:
                abort();
        }
    }
    setup_signal();
    // fprintf(stderr, "optind %d \n", optind);
    // for (int i = optind; i < argc; i++) {
    //     fprintf(stderr, "%s\n", argv[i]);
    // }

    // check the combination of input workloads
    if (kernel_filename != NULL && (ckpt_mem_filename != NULL || ckpt_cpu_filename != NULL)) {
        fprintf(stderr, "cannot specify -k and --ckpt-mem/--ckpt-cpu at same time\n");
        return 1;
    }
    if (!kernel_filename) {
        if (!ckpt_mem_filename && !ckpt_cpu_filename) {
            fprintf(stderr, "need specify -k or --ckpt-mem/--ckpt-cpu\n");
            return 1;
        }
        if (!ckpt_mem_filename || !ckpt_cpu_filename) {
            fprintf(stderr, "need specify --ckpt-mem and --ckpt-cpu at same time\n");
            return 1;
        }
    }

#ifndef CONFIG_USER_ONLY
    ram = alloc_ram(ram_size);
    qemu_log("pid:%d, ram_size:%lx kernel_filename:%s\n", getpid(), ram_size, kernel_filename);
#endif
    uint64_t entry_addr;
#if defined(CONFIG_USER_ONLY)
    TARGET_PAGE_SIZE = getpagesize();
    TARGET_PAGE_BITS = ctz64(TARGET_PAGE_SIZE);
    TARGET_PAGE_MASK = ((target_ulong)-1 << TARGET_PAGE_BITS);

    qemu_log_mask(CPU_LOG_PAGE, "TARGET_PAGE_SIZE:%lx TARGET_PAGE_BITS:%ld TARGET_PAGE_MASK:%lx\n", TARGET_PAGE_SIZE, TARGET_PAGE_BITS, TARGET_PAGE_MASK);

    kernel_filename = argv[optind];
    if(!kernel_filename) {
        usage();
        laemu_exit(EXIT_FAILURE);
    }
    load_elf_user(kernel_filename, &entry_addr);
    target_set_brk(info.brk);
    exec_path = kernel_filename;
    if (realpath(exec_path, real_exec_path)) {
        exec_path = real_exec_path;
    }
#else
    if (kernel_filename && !is_directory(kernel_filename)) {
        load_elf(kernel_filename, &entry_addr);
    }

#if !defined (CONFIG_CLI) && !defined (CONFIG_PLUGIN)
    // set no echo
    struct termios term;
    tcgetattr(STDIN_FILENO, &term);

    term.c_lflag &= ~ECHO;
    term.c_lflag &= ~ICANON;
    term.c_lflag &= ~ISIG;
    tcsetattr(STDIN_FILENO, 0, &term);

    fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK);
#endif

    timer_t timerid;
    struct sigevent sev;
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGRTMIN;
    sev.sigev_value.sival_ptr = &timerid;
    lsassert (timer_create(CLOCK_MONOTONIC, &sev, &timerid) == 0);

#endif
    qemu_log_mask(CPU_LOG_PAGE, "entry_addr:%lx\n", entry_addr);

    LoongArchCPU* cpu = aligned_alloc(64, sizeof(LoongArchCPU));
    memset(cpu, 0, sizeof(LoongArchCPU));
    CPUState *cs = CPU(cpu);
    CPULoongArchState* env = &cpu->env;
    cs->env = env;
    cpu_reset(cs);
    loongarch_core_initfn(env);
    cpu_clear_tc(env);
    env->timer_counter = INT64_MAX;
#ifndef CONFIG_USER_ONLY
    env->timerid = timerid;
    if (serial_plus) {
        qemu_irq irq = qemu_allocate_irq(loongarch_cpu_set_irq, (void*)env, 7);
        ss = simple_serial_init(0x1fe001e0, irq, 115200);

        struct sigevent sev;
        sev.sigev_notify = SIGEV_SIGNAL;
        sev.sigev_signo = SIGRTMIN + 1;
        sev.sigev_value.sival_ptr = &serial_timerid;
        lsassert (timer_create(CLOCK_MONOTONIC, &sev, &serial_timerid) == 0);

        struct sigaction sa;
        memset(&sa, 0, sizeof(struct sigaction));
        sigemptyset(&sa.sa_mask);
        sa.sa_sigaction = sigaction_entry_serial_timer;
        sa.sa_flags     = SA_SIGINFO;
        lsassert (sigaction(SIGRTMIN + 1, &sa, NULL) == 0);

        struct itimerspec its;
        its.it_value.tv_sec = 0;
        its.it_value.tv_nsec = 5000000;
        its.it_interval.tv_sec = 0;
        its.it_interval.tv_nsec = 5000000;
        lsassert(timer_settime(serial_timerid, 0, &its, NULL) == 0);
    }
#endif
    env->pc = entry_addr;

    if (!kernel_filename) {
        restore_checkpoint_qemu_format(env, ckpt_mem_filename, ckpt_cpu_filename);
        entry_addr = env->pc;
    } else if (is_directory(kernel_filename)) {
        restore_checkpoint(env, kernel_filename);
        entry_addr = env->pc;
    }


#ifdef CONFIG_CLI
    // stall program at begin in cli mode
    set_fetch_breakpoint(0, entry_addr);

    if (determined == 0) {
        determined = 1;
        fprintf(stderr, "warn:auto enable -z (Determined events) option in cli mode\n");
    }
#endif

#if defined(CONFIG_USER_ONLY)
    target_ulong sp = user_setup_stack();
    int guest_argc = argc - optind;
    char** guest_argv = argv + optind;
    size_t* guest_argv_len = (size_t*)malloc(sizeof(size_t) * guest_argc);
    target_ulong* guest_argv_addr = (target_ulong*)malloc(sizeof(target_ulong) * guest_argc);
    for (int i = 0; i < guest_argc; i++) {
        guest_argv_len[i] = strlen(guest_argv[i]);
        sp -= (guest_argv_len[i] + 1);
        memcpy((void*)(sp), guest_argv[i], guest_argv_len[i]);
        guest_argv_addr[i] = sp;
        // fprintf(stderr, "setup argv %s %lx %ld\n", guest_argv[i], sp, guest_argv_len[i]);
    }

    int guest_envc = 0;
    while (envp[guest_envc]) {guest_envc ++;}
    // fprintf(stderr, "guest_envc %d\n", guest_envc);
    char** guest_envv = envp;
    size_t* guest_envv_len = (size_t*)malloc(sizeof(size_t) * guest_envc);
    target_ulong* guest_envv_addr = (target_ulong*)malloc(sizeof(target_ulong) * guest_envc);
    for (int i = 0; i < guest_envc; i++) {
        guest_envv_len[i] = strlen(guest_envv[i]);
        sp -= (guest_envv_len[i] + 1);
        memcpy((void*)(sp), guest_envv[i], guest_envv_len[i]);
        guest_envv_addr[i] = sp;
        // fprintf(stderr, "setup envv %s %lx %ld\n", guest_envv[i], sp, guest_envv_addr[i]);
    }
    sp = QEMU_ALIGN_DOWN(sp, 16);

    sp -= 16;
    abi_ulong u_rand_bytes = sp;
    ram_std(sp+0, 0x0011223344556677);
    ram_std(sp+8, 0x8899aabbccddeeff);

    if ((guest_argc + guest_envc) % 2 == 0) {
        sp -=8;
    }

    sp -= 16; ram_std(sp, AT_RANDOM);ram_std(sp+8, u_rand_bytes);
    sp -= 16; ram_std(sp, AT_PHDR);  ram_std(sp+8, (abi_ulong)(info.load_addr + e_phoff));
    sp -= 16; ram_std(sp, AT_PHENT); ram_std(sp+8, (abi_ulong)(sizeof (elf_phdr)));
    sp -= 16; ram_std(sp, AT_PHNUM); ram_std(sp+8, (abi_ulong)(e_phnum));
    sp -= 16; ram_std(sp, AT_PAGESZ);ram_std(sp+8, TARGET_PAGE_SIZE);
    sp -= 16; ram_std(sp, AT_UID);   ram_std(sp+8, (abi_ulong) getuid());
    sp -= 16; ram_std(sp, AT_EUID);  ram_std(sp+8, (abi_ulong) geteuid());
    sp -= 16; ram_std(sp, AT_GID);   ram_std(sp+8, (abi_ulong) getgid());
    sp -= 16; ram_std(sp, AT_EGID);  ram_std(sp+8, (abi_ulong) getegid());

    qemu_log_mask(CPU_LOG_PAGE, "aux addr %lx\n", sp);

    target_ulong guest_arg_size = ( 1 + guest_argc + 1 + guest_envc + 1) * 8;
    sp -= guest_arg_size;
    ram_std(sp, guest_argc);
    for (int i = 0; i < guest_argc; i++) {
        ram_std(sp + 8 + (i * 8), guest_argv_addr[i]);
    }
    for (int i = 0; i < guest_envc; i++) {
        ram_std(sp + (1 + guest_argc + 1) * 8 +(i * 8), guest_envv_addr[i]);
    }
    env->gpr[3] = sp;
    // fprintf(stderr, "guest_sp:%lx\n", sp);
    // for (int i = 0; i < guest_argc + 2; i++) {
    //     fprintf(stderr, "%lx %lx\n", sp, ram_ldud(sp + i *8));
    // }
    qemu_log_mask(CPU_LOG_PAGE, "init sp %lx\n", sp);
#endif
    current_env = env;
#if defined(CONFIG_PLUGIN)
    if (plugin_name[0]) {
        void* plugin_handle = dlopen(plugin_name, RTLD_LAZY);
        if (!plugin_handle) {
            fprintf(stderr, "%s\n", dlerror());
            laemu_exit(EXIT_FAILURE);
        }
        dlerror();

        la_emu_plugin_install_func_t install_func = dlsym(plugin_handle, "la_emu_plugin_install");
        char *error;
        if ((error = dlerror()) != NULL)  {
            fprintf(stderr, "%s\n", error);
            laemu_exit(EXIT_FAILURE);
        }
        plugin_ops = install_func(plugin_arg);
        if (plugin_ops && plugin_ops->emu_start) {
            plugin_ops->emu_start();
        }
    }
#endif
    if (gdbserver) {
#if defined(CONFIG_GDB)
#ifndef CONFIG_USER_ONLY
        if (!hw_ptw) {
            fprintf(stderr, "Enable hw_ptw options -w for better debug\n");
            fprintf(stderr, "Enable hw_ptw options -w for better debug\n");
            fprintf(stderr, "Enable hw_ptw options -w for better debug\n");
            sleep(3);
        }
#endif
        gdbserver_init(1234);
        gdbserver_has_message = 1;
        gdbserver_loop();
#endif
    } else {
        exec_env(env);
    }
    fprintf(stderr, "end from main %s %d\n", __FILE__, __LINE__);
    return 0;
}

#endif
