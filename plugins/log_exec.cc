#include <iostream>

extern "C" {
#include "../plugin.h"
}


void my_emu_insn_before(void* env, uint64_t pc, uint32_t insn) {
    printf("pc:%016lx insn:%09x\n", pc, insn);
}

la_emu_plugin_ops my_op = {
    .emu_insn_before = my_emu_insn_before,
};

extern "C" la_emu_plugin_ops* la_emu_plugin_install(const char* arg) {
    return &my_op;
}
