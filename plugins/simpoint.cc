#include "common.h"
#include <cstdint>
#include <cstdio>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <map>

extern "C" {
#include "../plugin.h"
}

using namespace std;

bool begin_after_ibar0x40;
static uint64_t interval_size = 1000000;
map<uint64_t, double> icount2weight;
uint64_t icount;
bool begin_collect;
string ckpt_save_path;

void my_emu_insn_before(void* env, uint64_t pc, uint32_t insn) {
    // begin collect after exec ibar 0x40
    if (begin_after_ibar0x40 && !begin_collect) {
        if (insn == 0x38728040) {
            begin_collect = true;
        } else {
            return;
        }
    }

    if (icount2weight.count(icount)) {
        fprintf(stderr, "simpoint dump checkpoint at icount=%ld,weight=%lf\n", icount, icount2weight[icount]);
        la_emu_save_checkpoint(env, (char*)(ckpt_save_path  + "/" + "w" + to_string(icount2weight[icount])).c_str());
        icount2weight.erase(icount);
        if (icount2weight.empty()) {
            fprintf(stderr, "all checkpoints have been dumped\n");
            fflush(NULL);
            exit(0);
        }
    }

    icount++;
}

la_emu_plugin_ops my_op = {
    .emu_insn_before = my_emu_insn_before,
};

extern "C" la_emu_plugin_ops* la_emu_plugin_install(const char* arg) {
    string simpoints_file_name("simpoints");
    string weights_file_name("weights");
    if (arg[0]) {
        auto options = split(arg, ",");
        for (auto &option : options) {
            auto sp = split(option, "=");
            if (sp[0] == "interval") {
                interval_size = stol(sp[1]);
            } else if (sp[0] == "simpoints") {
                simpoints_file_name = sp[1];
            } else if (sp[0] == "weights") {
                weights_file_name = sp[1];
            } else if (sp[0] == "ibar0x40") {
                begin_after_ibar0x40 = stol(sp[1]);
            } else if (sp[0] == "path") {
                ckpt_save_path = sp[1];
            } else {
                printf("unknown option:%s\n", option.c_str());
            }
        }
    }

    FILE* simpoints_file = fopen_nofail(simpoints_file_name.c_str(), "r");
    FILE* weights_file = fopen_nofail(weights_file_name.c_str(), "r");

    uint64_t interval_num;
    double weight;

    while (fscanf(simpoints_file, "%ld%*f", &interval_num) == 1) {
        lsassert(fscanf(weights_file, "%lf%*f", &weight) == 1);
        if (interval_num == 0) {
            fprintf(stderr, "warn:simpoint select first interval,weight=%lf,just discard.\n", weight);
            if (weight > 0.1) {
                fprintf(stderr, "can not ignore the first interval!\n");
                exit(1);
            }
        } else {
            interval_num -= 1; // warmup inst num : simpoint inst num = 1 : 1
            icount2weight[interval_num * interval_size] = weight;
        }
    }
    fclose(simpoints_file);
    fclose(weights_file);
    return &my_op;
}