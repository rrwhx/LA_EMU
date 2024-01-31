#ifndef __USER_H__
#define __USER_H__

#include "qemu/osdep.h"
#include "cpu.h"

abi_long do_syscall(CPUArchState *cpu_env, int num, abi_long arg1,
                    abi_long arg2, abi_long arg3, abi_long arg4,
                    abi_long arg5, abi_long arg6, abi_long arg7,
                    abi_long arg8);

#endif
