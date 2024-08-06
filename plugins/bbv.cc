#include "common.h"
#include <cstdint>
#include <cstdio>
#include <stdint.h>
#include <string>
#include <set>

extern "C" {
#include "../plugin.h"
}

using namespace std;

bool begin_after_ibar0x40;
static uint64_t interval_size = 1000000;
static FILE* bbv_file;

struct bbv_info {
    uint64_t id;
    uint64_t start_pc;
    uint64_t icount;
    uint64_t enter_num;
};
typedef struct bbv_info bbv_info;

uint64_t cur_interval_count;
uint64_t interval_num;

map<uint64_t, bbv_info> startpc2bbv;
set<uint64_t> cur_interval_start_pc;

bool begin_collect;

void my_emu_insn_before(void* env, uint64_t pc, uint32_t insn) {
    // begin collect bbv after exec ibar 0x40
    if (begin_after_ibar0x40 && !begin_collect) {
        if (insn == 0x38728040) {
            begin_collect = true;
        } else {
            return;
        }
    }

    if (startpc2bbv.count(pc)) {
        startpc2bbv[pc].enter_num++;
        cur_interval_start_pc.insert(pc);
    }

    cur_interval_count++;

    if (cur_interval_count == interval_size) {
        interval_num++;
        fprintf(bbv_file, "T");
        for (auto start_pc : cur_interval_start_pc) {
            auto cur_bblk = startpc2bbv[start_pc];
            fprintf(bbv_file, ":%ld:%ld ", cur_bblk.id, cur_bblk.enter_num * cur_bblk.icount);
            startpc2bbv[start_pc].enter_num = 0;
        }
        fprintf(bbv_file, "\n");
        cur_interval_start_pc.clear();
        cur_interval_count = 0;
    }
}

void my_emu_stop() {
    if (interval_num < 1000) {
        fprintf(stderr, "warn:only collect %ld interval,A good rule of thumb is\n"
                        "to make sure to use at least 1,000 intervals\n"
                        "in order for the clustering algorithm to be\n"
                        "able to find a good partition of the intervals.\n", interval_num);
    }
    fflush(bbv_file);
}

la_emu_plugin_ops my_op = {
    .emu_stop = my_emu_stop,
    .emu_insn_before = my_emu_insn_before,
};

extern "C" la_emu_plugin_ops* la_emu_plugin_install(const char* arg) {
    string bbv_file_name("bbv.txt");
    string bblk_file_name("bblk.txt");
    if (arg[0]) {
        auto options = split(arg, ",");
        for (auto &option : options) {
            auto sp = split(option, "=");
            if (sp[0] == "interval") {
                interval_size = stol(sp[1]);
            } else if (sp[0] == "bbv") {
                bbv_file_name = sp[1];
            } else if (sp[0] == "bblk") {
                bblk_file_name = sp[1];
            } else if (sp[0] == "ibar0x40") {
                begin_after_ibar0x40 = stol(sp[1]);
            } else {
                printf("unknown option:%s\n", option.c_str());
            }
        }
    }
    bbv_file = fopen_nofail(bbv_file_name.c_str(), "w");
    FILE* bblk_file = fopen_nofail(bblk_file_name.c_str(), "r");
    char buffer[1024];
    while (fgets(buffer, 1024, bblk_file)) {
        bbv_info cur_bblk;
        cur_bblk.enter_num = 0;
        sscanf(buffer, "id:%ld, pc:%lx, bb_insn_num:%ld",
            &cur_bblk.id, &cur_bblk.start_pc, &cur_bblk.icount);
        startpc2bbv[cur_bblk.start_pc] = cur_bblk;
    }
    return &my_op;
}
