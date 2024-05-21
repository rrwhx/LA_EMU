
#include "qemu/osdep.h"
#include "util.h"

#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/mman.h>

#include "sizes.h"
#include "cpu.h"
#include "internals.h"

#define GETBIT(__a, __index) ((__a >> __index) & 1)
#define GETBITS(__a, __index, __len) ((__a >> __index) & ((1 << __len) - 1))

extern const char * const regnames[32];

extern const char * const fregnames[32];

extern const char *const loongarch_r_alias[32];

extern const char *const loongarch_f_alias[32];

#ifndef CONFIG_USER_ONLY
extern char* ram;
#else
// In "USER_ONLY" mode, the guest address is mapped directly to the host
char* ram = 0;
#endif

static int debug_handle_continue(const char* str);
static int debug_handle_quit(const char* str);
static int debug_handle_break(const char* str);
static int debug_handle_delete(const char* str);
static int debug_handle_info(const char* str);
static int debug_handle_singlestep(const char* str);
static int debug_handle_x(const char* str);
static int debug_handle_help(const char* str);

typedef struct debug_cmd {
    char shortcut;
    const char *name;
    int (*func)(const char*);
    const char* desc;
} debug_cmd;
const debug_cmd debugcmds[] = {
    {'c', "continue", debug_handle_continue, "continue run program" },
    {'q', "quit", debug_handle_quit, "exit LA_EMU"},
    {'b', "break", debug_handle_break, "use 'b 0x1c' to set breakpoint at pc=0x1c"},
    {'d', "delete", debug_handle_delete, "use 'd 1' to delete breakpoint 1, use 'd' to delete all breakpoints"},
    {'i', "info", debug_handle_info, "use 'i r' to show all gpr, use 'i fpr' to show all fpr, use 'i csr' to show all csr, use 'i b' to show all breakpoints"},
    {'s', "si", debug_handle_singlestep, "use 's n' to execute n insts, use 's' to execute 1 insts"},
    {'x', "x", debug_handle_x, "use 'x 0x1c' to display the value of guest addr=0x1c, please RTFSC for other formats"},
    {'h', "help", debug_handle_help, "show this info"},
    {0, NULL, NULL},
};

int64_t singlestep = -1;

#define BREAKPOINT_NUM 4
struct FetchBreakpoint {
    bool valid;
    target_long pc;
};
typedef struct FetchBreakpoint FetchBreakpoint;
static int fetch_breakpoint_num = 0;
static FetchBreakpoint fetch_breakpoints[BREAKPOINT_NUM] = {0};

int check_signal = 0;

static void dump_vzoui(int fcsr) {
    fprintf(stderr, "%c%c%c%c%c", GETBIT(fcsr, 4) ? 'V' : '-',  GETBIT(fcsr, 3) ? 'Z' : '-',  GETBIT(fcsr, 2) ? 'O' : '-',  GETBIT(fcsr, 1) ? 'U' : '-',  GETBIT(fcsr, 0) ? 'I' : '-');
}

static void dump_fcsr(int fcsr) {
    int rm = (fcsr >> 8) & 0x3;
    static const char* rm_mode[4] = {
        "RNE",
        "RZ",
        "RP",
        "RM",
    };
    // printf("    Enables:V:%d, Z:%d, O:%d, U:%d, I:%d\n", GETBIT(fcsr, 4), GETBIT(fcsr, 3), GETBIT(fcsr, 2), GETBIT(fcsr, 1), GETBIT(fcsr, 0));
    // printf("    RM     :%d(%s)\n", rm, rm_mode[rm]);
    // printf("    Flags  :V:%d, Z:%d, O:%d, U:%d, I:%d\n", GETBIT(fcsr, 20), GETBIT(fcsr, 19), GETBIT(fcsr, 18), GETBIT(fcsr, 17), GETBIT(fcsr, 16));
    // printf("    Cause  :V:%d, Z:%d, O:%d, U:%d, I:%d\n", GETBIT(fcsr, 28), GETBIT(fcsr, 27), GETBIT(fcsr, 26), GETBIT(fcsr, 25), GETBIT(fcsr, 24));

    fprintf(stderr, "RM:%d(%s)", rm, rm_mode[rm]);
    fprintf(stderr, ",Enables:");dump_vzoui(GETBITS(fcsr, 0, 5));
    fprintf(stderr, ",Flags:");dump_vzoui(GETBITS(fcsr, 16, 5));
    fprintf(stderr, ",Cause:");dump_vzoui(GETBITS(fcsr, 24, 5));
    fprintf(stderr, "\n");
}

__attribute__((noinline)) void show_register(CPULoongArchState *env) {
    fprintf(stderr, "pc:0x%lx\n", env->pc);
    for (int i = 0; i <32; i++) {
        fprintf(stderr, "r%02d/%-3s:%016lx    ", i, loongarch_r_alias[i], env->gpr[i]);
        if ((i + 1) % 4 == 0) {
            fprintf(stderr, "\n");
        }
    }
}

__attribute__((noinline)) void show_register_fpr(CPULoongArchState *env) {
    for (int i = 0; i <32; i++) {
        fprintf(stderr, "f%02d   {f = 0x%08x, d = 0x%016lx} {f = %.6f\t, d = %.12f\t}\n",
            i, env->fpr[i].vreg.W[0], env->fpr[i].vreg.D[0], *(float*)&env->fpr[i], *(double*)&env->fpr[i]);
    }
    for (int i = 0; i <8; i++) {
        fprintf(stderr, "fcc%d:%d ", i, env->cf[i]);
    }
    fprintf(stderr, "\n");
    fprintf(stderr, "fcsr:%08x\n", env->fcsr0);
    dump_fcsr(env->fcsr0);
}

static void show_csr(CPULoongArchState *env) {
    fprintf(stderr,"CSR_CRMD\t:0x%-16lx ",env->CSR_CRMD);
    fprintf(stderr,"CSR_PRMD\t:0x%-16lx ",env->CSR_PRMD);
    fprintf(stderr,"CSR_EUEN\t:0x%-16lx ",env->CSR_EUEN);
    fprintf(stderr,"CSR_MISC\t:0x%-16lx ",env->CSR_MISC);
    fprintf(stderr, "\n");
    fprintf(stderr,"CSR_ECFG\t:0x%-16lx ",env->CSR_ECFG);
    fprintf(stderr,"CSR_ESTAT\t:0x%-16lx ",env->CSR_ESTAT);
    fprintf(stderr,"CSR_ERA\t:0x%-16lx ",env->CSR_ERA);
    fprintf(stderr,"CSR_BADV\t:0x%-16lx ",env->CSR_BADV);
    fprintf(stderr, "\n");
    fprintf(stderr,"CSR_BADI\t:0x%-16lx ",env->CSR_BADI);
    fprintf(stderr,"CSR_EENTRY\t:0x%-16lx ",env->CSR_EENTRY);
    fprintf(stderr,"CSR_TLBIDX\t:0x%-16lx ",env->CSR_TLBIDX);
    fprintf(stderr,"CSR_TLBEHI\t:0x%-16lx ",env->CSR_TLBEHI);
    fprintf(stderr, "\n");
    fprintf(stderr,"CSR_TLBELO0\t:0x%-16lx ",env->CSR_TLBELO0);
    fprintf(stderr,"CSR_TLBELO1\t:0x%-16lx ",env->CSR_TLBELO1);
    fprintf(stderr,"CSR_ASID\t:0x%-16lx ",env->CSR_ASID);
    fprintf(stderr,"CSR_PGDL\t:0x%-16lx ",env->CSR_PGDL);
    fprintf(stderr, "\n");
    fprintf(stderr,"CSR_PGDH\t:0x%-16lx ",env->CSR_PGDH);
    fprintf(stderr,"CSR_PGD\t:0x%-16lx ",env->CSR_PGD);
    fprintf(stderr,"CSR_PWCL\t:0x%-16lx ",env->CSR_PWCL);
    fprintf(stderr,"CSR_PWCH\t:0x%-16lx ",env->CSR_PWCH);
    fprintf(stderr, "\n");
    fprintf(stderr,"CSR_STLBPS\t:0x%-16lx ",env->CSR_STLBPS);
    fprintf(stderr,"CSR_RVACFG\t:0x%-16lx ",env->CSR_RVACFG);
    fprintf(stderr,"CSR_CPUID\t:0x%-16lx ",env->CSR_CPUID);
    fprintf(stderr,"CSR_PRCFG1\t:0x%-16lx ",env->CSR_PRCFG1);
    fprintf(stderr, "\n");
    fprintf(stderr,"CSR_PRCFG2\t:0x%-16lx ",env->CSR_PRCFG2);
    fprintf(stderr,"CSR_PRCFG3\t:0x%-16lx ",env->CSR_PRCFG3);
    fprintf(stderr,"CSR_TID\t:0x%-16lx ",env->CSR_TID);
    fprintf(stderr,"CSR_TCFG\t:0x%-16lx ",env->CSR_TCFG);
    fprintf(stderr, "\n");
    fprintf(stderr,"CSR_TVAL\t:0x%-16lx ",env->CSR_TVAL);
    fprintf(stderr,"CSR_CNTC\t:0x%-16lx ",env->CSR_CNTC);
    fprintf(stderr,"CSR_TICLR\t:0x%-16lx ",env->CSR_TICLR);
    fprintf(stderr,"CSR_LLBCTL\t:0x%-16lx ",env->CSR_LLBCTL);
    fprintf(stderr, "\n");
    fprintf(stderr,"CSR_IMPCTL1\t:0x%-16lx ",env->CSR_IMPCTL1);
    fprintf(stderr,"CSR_IMPCTL2\t:0x%-16lx ",env->CSR_IMPCTL2);
    fprintf(stderr,"CSR_TLBRENTRY:0x%-16lx ",env->CSR_TLBRENTRY);
    fprintf(stderr,"CSR_TLBRBADV:0x%-16lx ",env->CSR_TLBRBADV);
    fprintf(stderr, "\n");
    fprintf(stderr,"CSR_TLBRERA\t:0x%-16lx ",env->CSR_TLBRERA);
    fprintf(stderr,"CSR_TLBRSAVE:0x%-16lx ",env->CSR_TLBRSAVE);
    fprintf(stderr,"CSR_TLBRELO0:0x%-16lx ",env->CSR_TLBRELO0);
    fprintf(stderr,"CSR_TLBRELO1:0x%-16lx ",env->CSR_TLBRELO1);
    fprintf(stderr, "\n");
    fprintf(stderr,"CSR_TLBREHI\t:0x%-16lx ",env->CSR_TLBREHI);
    fprintf(stderr,"CSR_TLBRPRMD:0x%-16lx ",env->CSR_TLBRPRMD);
    fprintf(stderr,"CSR_MERRCTL\t:0x%-16lx ",env->CSR_MERRCTL);
    fprintf(stderr,"CSR_MERRINFO1:0x%-16lx ",env->CSR_MERRINFO1);
    fprintf(stderr, "\n");
    fprintf(stderr,"CSR_MERRINFO2\t:0x%-16lx ",env->CSR_MERRINFO2);
    fprintf(stderr,"CSR_MERRENTRY:0x%-16lx ",env->CSR_MERRENTRY);
    fprintf(stderr,"CSR_MERRERA:0x%-16lx ",env->CSR_MERRERA);
    fprintf(stderr,"CSR_MERRSAVE:0x%-16lx ",env->CSR_MERRSAVE);
    fprintf(stderr, "\n");
    fprintf(stderr,"CSR_CTAG\t:0x%-16lx ",env->CSR_CTAG);
    fprintf(stderr, "\n");
    fprintf(stderr, "CSR_DMW[0]\t:0x%-16lx ",env->CSR_DMW[0]);
    fprintf(stderr, "CSR_DMW[1]\t:0x%-16lx ",env->CSR_DMW[1]);
    fprintf(stderr, "CSR_DMW[2]\t:0x%-16lx ",env->CSR_DMW[2]);
    fprintf(stderr, "CSR_DMW[3]\t:0x%-16lx ",env->CSR_DMW[3]);
    fprintf(stderr, "\n");
}

static int debug_handle_continue(const char* str) {
    return 1;
}
static int debug_handle_quit(const char* str) {
    exit(EXIT_SUCCESS);
    return 0;
}

static int debug_handle_break(const char* str) {
    target_long pc;
    int r = sscanf(str, "%*s%lx", &pc);
    if (r != 1) {
        return -1;
    }

    if (fetch_breakpoint_num == BREAKPOINT_NUM) {
        fprintf(stderr, "Breakpoint full, delete some or increase BREAKPOINT_NUM\n");
        return 0;
    }

    fetch_breakpoint_num++;
    for (int i = 0; i < BREAKPOINT_NUM; i++) {
        if (!fetch_breakpoints[i].valid) {
            fetch_breakpoints[i].valid = true;
            fetch_breakpoints[i].pc = pc;
            fprintf(stderr, "set Breakpoint %d at 0x%lx\n", i, pc);
            break;
        }
    }
    return 0;
}

static int debug_handle_delete(const char* str) {
    int idx;
    int r = sscanf(str, "%*s%d", &idx);
    if (r != 1) {
        fprintf(stderr, "delete all breakpoints\n");
        memset((void*)fetch_breakpoints, 0, sizeof(FetchBreakpoint) * BREAKPOINT_NUM);
        fetch_breakpoint_num = 0;
        return 0;
    }
    if (fetch_breakpoints[idx].valid) {
        fprintf(stderr, "delete Breakpoint %d at 0x%lx\n", idx, fetch_breakpoints[idx].pc);
        fetch_breakpoints[idx].valid = 0;
        fetch_breakpoint_num--;
    }
    return 0;
}

static void show_breakpoints()
{
    for(int i = 0; i < BREAKPOINT_NUM; i++) {
        if (fetch_breakpoints[i].valid) {
            fprintf(stderr, "breakpoint %d at 0x%lx\n", i, fetch_breakpoints[i].pc);
        }
    }
}

void set_fetch_breakpoint(int idx, target_long pc)
{
    lsassert(idx < BREAKPOINT_NUM);
    lsassert(fetch_breakpoint_num < BREAKPOINT_NUM);

    fetch_breakpoints[idx].pc = pc;
    fetch_breakpoints[idx].valid = true;
    fetch_breakpoint_num++;
}

static int debug_handle_info(const char* str) {
    char buf[1024];
    int r = sscanf(str, "%*s%s", buf);
    if (r != 1) {
        return -1;
    }
    if (strcmp(buf, "r") == 0 || strcmp(buf, "gpr") == 0) {
        show_register(current_env);
    } else if (strcmp(buf, "fpr") == 0) {
        show_register_fpr(current_env);
    } else if (strcmp(buf, "csr") == 0) {
        show_csr(current_env);
    } else if (strcmp(buf, "b") == 0) {
        show_breakpoints();
    } else {
        return -1;
    }
    return 0;
}

static int debug_handle_singlestep(const char* str) {
    int r = sscanf(str, "%*s%lx", &singlestep);
    if (r != 1) {
        singlestep = 1;
    }
    return 1;
}

static int debug_handle_help(const char* str) {
    const debug_cmd* item;
    for (item = debugcmds; item->name != NULL; item++) {
        fprintf(stderr, "%s\t shortcut:%c\t desc:%s\n",
            item->name, item->shortcut, item->desc);
    }
    fprintf(stderr, "One more thing : Ctrl-C can stall the program\n");
    return 0;
}

void format_printf_b(void* addr, char format) {
    target_long host_addr = (target_ulong)addr + (target_long)ram;

    switch (format)
    {
        case 'o': fprintf(stderr, "%p 0x%x\n", addr, *(uint8_t*)host_addr); break; // octal
        case 'x': fprintf(stderr, "%p 0x%x\n", addr, *(uint8_t*)host_addr); break; // hexadecimal
        case 'd': fprintf(stderr, "%p 0x%d\n", addr, *(uint8_t*)host_addr); break; // decimal
        case 'u': fprintf(stderr, "%p 0x%x\n", addr, *(uint8_t*)host_addr); break; // unsigned decimal
        case 't': fprintf(stderr, "%p 0x%x\n", addr, *(uint8_t*)host_addr); break; // binary
        case 'f': fprintf(stderr, "%p 0x%x\n", addr, *(uint8_t*)host_addr); break; // floating point
        case 'a': fprintf(stderr, "%p 0x%x\n", addr, *(uint8_t*)host_addr); break; // address
        case 'c': fprintf(stderr, "%p %c\n", addr, *(uint8_t*)host_addr); break; // char
        case 's': fprintf(stderr, "%p %s\n", addr, (char*)host_addr); break; // string
        case 'i': fprintf(stderr, "%p 0x%x\n", addr, *(uint8_t*)host_addr); break; // instruction
        default:
            fprintf(stderr, "unknown format:%c\n", format);
    }
}
void format_printf_h(void* addr, char format) {
    target_long host_addr = (target_ulong)addr + (target_long)ram;

    switch (format)
    {
        case 'o': fprintf(stderr, "%p 0x%x\n", addr, *(uint16_t*)host_addr); break; // octal
        case 'x': fprintf(stderr, "%p 0x%x\n", addr, *(uint16_t*)host_addr); break; // hexadecimal
        case 'd': fprintf(stderr, "%p 0x%d\n", addr, *(uint16_t*)host_addr); break; // decimal
        case 'u': fprintf(stderr, "%p 0x%x\n", addr, *(uint16_t*)host_addr); break; // unsigned decimal
        case 't': fprintf(stderr, "%p 0x%x\n", addr, *(uint16_t*)host_addr); break; // binary
        case 'f': fprintf(stderr, "%p 0x%x\n", addr, *(uint16_t*)host_addr); break; // floating point
        case 'a': fprintf(stderr, "%p 0x%x\n", addr, *(uint16_t*)host_addr); break; // address
        case 'c': fprintf(stderr, "%p 0x%x\n", addr, *(uint16_t*)host_addr); break; // char
        case 's': fprintf(stderr, "%p %s\n", addr, (char*)host_addr); break; // string
        case 'i': fprintf(stderr, "%p 0x%x\n", addr, *(uint16_t*)host_addr); break; // instruction
        default:
            fprintf(stderr, "unknown format:%c\n", format);
    }
}
void format_printf_w(void* addr, char format) {
    target_long host_addr = (target_ulong)addr + (target_long)ram;

    switch (format)
    {
        case 'o': fprintf(stderr, "%p 0x%x\n", addr, *(uint32_t*)host_addr); break; // octal
        case 'x': fprintf(stderr, "%p 0x%x\n", addr, *(uint32_t*)host_addr); break; // hexadecimal
        case 'd': fprintf(stderr, "%p 0x%d\n", addr, *(uint32_t*)host_addr); break; // decimal
        case 'u': fprintf(stderr, "%p 0x%x\n", addr, *(uint32_t*)host_addr); break; // unsigned decimal
        case 't': fprintf(stderr, "%p 0x%x\n", addr, *(uint32_t*)host_addr); break; // binary
        case 'f': fprintf(stderr, "%p %f\n", addr, *(float*)host_addr); break; // floating point
        case 'a': fprintf(stderr, "%p 0x%x\n", addr, *(uint32_t*)host_addr); break; // address
        case 'c': fprintf(stderr, "%p 0x%x\n", addr, *(uint32_t*)host_addr); break; // char
        case 's': fprintf(stderr, "%p %s\n", addr, (char*)host_addr); break; // string
        case 'i': fprintf(stderr, "%p 0x%x\n", addr, *(uint32_t*)host_addr); break; // instruction
        default:
            fprintf(stderr, "unknown format:%c\n", format);
    }
}
void format_printf_g(void* addr, char format) {
    target_long host_addr = (target_ulong)addr + (target_long)ram;

    switch (format)
    {
        case 'o': fprintf(stderr, "%p 0x%lx\n", addr, *(uint64_t*)host_addr); break; // octal
        case 'x': fprintf(stderr, "%p 0x%ld\n", addr, *(uint64_t*)host_addr); break; // hexadecimal
        case 'd': fprintf(stderr, "%p 0x%lx\n", addr, *(uint64_t*)host_addr); break; // decimal
        case 'u': fprintf(stderr, "%p 0x%lx\n", addr, *(uint64_t*)host_addr); break; // unsigned decimal
        case 't': fprintf(stderr, "%p 0x%lx\n", addr, *(uint64_t*)host_addr); break; // binary
        case 'f': fprintf(stderr, "%p %f\n", addr, *(double*)host_addr); break; // floating point
        case 'a': fprintf(stderr, "%p 0x%lx\n", addr, *(uint64_t*)host_addr); break; // address
        case 'c': fprintf(stderr, "%p 0x%lx\n", addr, *(uint64_t*)host_addr); break; // char
        case 's': fprintf(stderr, "%p %s\n", addr, (char*)host_addr); break; // string
        case 'i': fprintf(stderr, "%p 0x%lx\n", addr, *(uint64_t*)host_addr); break; // instruction
        default:
            fprintf(stderr, "unknown format:%c\n", format);
    }
}

int format_printf(target_ulong addr, int index, char format, char size) {
    switch (size)
    {
        case 'b': format_printf_b((void*)(addr + index * 1), format); break;
        case 'h': format_printf_h((void*)(addr + index * 2), format); break;
        case 'w': format_printf_w((void*)(addr + index * 4), format); break;
        case 'g': format_printf_g((void*)(addr + index * 8), format); break;
        default:
            fprintf(stderr, "unknown size:%c\n", size);
    }
    return 0;
}

bool is_format(char c) {
    switch (c)
    {
        case 'o': return true;
        case 'x': return true;
        case 'd': return true;
        case 'u': return true;
        case 't': return true;
        case 'f': return true;
        case 'a': return true;
        case 'c': return true;
        case 's': return true;
        case 'i': return true;
        default:
            return false;
    }
    return false;
}

bool is_size(char c) {
    switch (c)
    {
        case 'b': return true;
        case 'h': return true;
        case 'w': return true;
        case 'g': return true;
        default:
            return false;
    }
    return false;
}

static int debug_handle_x(const char* str) {
    char format = 'x';
    char size = 'w';
    int len = 1;
    target_ulong addr;
    const char* end = str + 1;
    if (str[1] == '/') {
        end = str + 2;
        if (isdigit(str[2])) {
            len = strtol(str + 2, (char**)&end, 0);
            fprintf(stderr, "str %p %p %d\n", str + 2, end, len);
        }
        while (!isspace(*end)) {
            if (is_size(*end)) {
                size = *end;
            } else if (is_format(*end)) {
                format = *end;
            } else {
                fprintf(stderr, "unknown size or format:%c\n", *end);
            }
            ++ end;
        }
    }
    int r = sscanf(end, "%lx", &addr);
    if (r != 1) {
        fprintf(stderr, "can not prase %s\n", str);
        return 0;
    }
    // fprintf(stderr, "addr:%lx len:%d size:%c format:%c\n", addr, len, size, format);
    for (int i = 0; i < len; i++) {
        format_printf(addr, i, format, size);
    }

    return 0;
}

static inline bool is_sep(char c) {
    return isspace(c) || c == '/';
}

static void handle_debug(void) {
    do {
        char* line_buff = NULL;
        size_t line_len;
        fprintf(stderr, "(debug)");fflush(stderr);
        ssize_t r = getline(&line_buff, &line_len, stdin);
        if (r < 0) {
            fprintf(stderr, "\nquit\n");
            exit(EXIT_SUCCESS);
        } else if(r > 1) {
            const debug_cmd* item;
            for (item = debugcmds; item->name != NULL; item++) {
                size_t name_len = strlen(item->name);
                if ((line_buff[0] == item->shortcut && is_sep(line_buff[1])) || (strncmp(line_buff, item->name, name_len) == 0 && is_sep(line_buff[name_len]))) {
                    break;
                }
            }
            if (item->func) {
                int r = item->func(line_buff);
                if (r < 0) {
                    fprintf(stderr, "cannot prase %s\n", line_buff);
                } else if (r > 0) {
                    fprintf(stderr, "Continuing.\n");
                    break;
                }
            } else {
                fprintf(stderr, "cannot prase %s\n", line_buff);
            }
        }
    } while (1);
}

void handle_debug_cli(CPULoongArchState *env)
{
    if (check_signal) {
        check_signal = 0;
        fprintf(stderr, "Program received signal SIGINT, Interrupt.");
        fprintf(stderr, "pc:%lx\n", env->pc);
        handle_debug();
    }

    for (int i = 0; i < BREAKPOINT_NUM; i++) {
        if (env->pc == fetch_breakpoints[i].pc && fetch_breakpoints[i].valid) {
            fprintf(stderr, "hit Breakpoint %d at pc:0x%lx\n", i, env->pc);
            handle_debug();
            break;
        }
    }
    if (singlestep == 0) {
        fprintf(stderr, "singlestep pc:0x%lx\n", env->pc);
        handle_debug();
    }
}