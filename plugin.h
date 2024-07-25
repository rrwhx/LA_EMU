#ifndef PLUGIN_H
#define PLUGIN_H

#include <inttypes.h>

typedef struct la_emu_plugin_ops {
    void (*emu_start)(void);
    void (*emu_stop)(void);
    void (*emu_insn_before)(void* env, uint64_t pc, uint32_t insn);
    void (*emu_execption)(void* env, int ecode);
} la_emu_plugin_ops;



typedef la_emu_plugin_ops* (*la_emu_plugin_install_func_t)(const char *);



#endif /* PLUGIN_H */
