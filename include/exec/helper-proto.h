#ifndef HELPER_H
#define HELPER_H

#include <stdint.h>

#define HELPER(name) glue(helper_, name)

# define GETPC() \
    ((uintptr_t)__builtin_extract_return_addr(__builtin_return_address(0)))

#endif /* HELPER_H */