#ifndef HELPER_H
#define HELPER_H

#include <stdint.h>

#include "cpu.h"

#define HELPER(name) glue(helper_, name)

# define GETPC() \
    ((uintptr_t)__builtin_extract_return_addr(__builtin_return_address(0)))

uint64_t helper_fclass_s(CPULoongArchState *env, uint64_t fj);
uint64_t helper_fclass_d(CPULoongArchState *env, uint64_t fj);

#endif /* HELPER_H */