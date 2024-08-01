#include "common.h"
#include <cstdint>
#include <cstdio>
#include <stdlib.h>
#include <set>

extern "C" {
#include "../plugin.h"
}

using namespace std;

static FILE* bblk_file;

bool begin_after_ibar0x40;
uint64_t last_pc = -1;
uint32_t last_insn;
// simpoint id can not be zero
uint64_t unique_trans_id = 1;
set<pair<uint64_t, uint64_t>> bblks;
pair<uint64_t, uint64_t> cur_bblk;

static inline bool is_br_inst(uint32_t insn) {
    return ((insn >> 26) >= 0x10 && (insn >> 26) <= 0x1b); // beqz - bgeu
}

void my_emu_insn_before(void* env, uint64_t pc, uint32_t insn) {
    if (begin_after_ibar0x40 && last_pc == -1) {
        if (insn == 0x38728040) { // 0x38728040 is ibar 0x40
            last_pc = pc;
            last_insn = insn;
            cur_bblk.first = pc;
        }
        return;
    }

    // O(nlogn) identify the begin and end of a basic block(may split later)
    // last inst is a taken control inst(include trap) or is a not taken cond br
    if (pc != last_pc + 4 || is_br_inst(last_insn)) {
        cur_bblk.second = last_pc;
        lsassert(cur_bblk.first <= cur_bblk.second);
        bblks.insert(cur_bblk);
        cur_bblk.first = pc;
    }
    if (insn == 0x38728041) { // 0x38728041 is ibar 0x41
        cur_bblk.second = pc;
        lsassert(cur_bblk.first <= cur_bblk.second);
        bblks.insert(cur_bblk);
    }
    
    last_pc = pc;
    last_insn = insn;
}

static void split_overlap_bblk() {
    // O(nlogn) decompose overlapping basic blocks(endpc is the same but startpc is different)
    map<uint64_t, pair<uint64_t, uint64_t>> endpc2bblk;
    for (auto iter : bblks) {
        cur_bblk = iter;
        if (endpc2bblk.count(cur_bblk.second)) {
            auto old_bblk = endpc2bblk[cur_bblk.second];
            if (old_bblk.first < cur_bblk.first) {
                // old_bblk include cur_bblk, need split old_bblk
                endpc2bblk.erase(old_bblk.second);
                pair<uint64_t, uint64_t> new_bblk(old_bblk.first, cur_bblk.first - 4);
                endpc2bblk[new_bblk.second] = new_bblk;
                endpc2bblk[cur_bblk.second] = cur_bblk;
            } else if (old_bblk.first > cur_bblk.first) {
                // cur_bblk include old_bblk, need split cur_bblk
                cur_bblk.second = old_bblk.first - 4;
                endpc2bblk[cur_bblk.second] = cur_bblk;
            }
        } else {
            endpc2bblk[cur_bblk.second] = cur_bblk;
        }
    }
    // O(nlogn) decompose overlapping basic blocks(startpc is the same but endpc is different)
    map<uint64_t, pair<uint64_t, uint64_t>> startpc2bblk;
    for (auto iter : endpc2bblk) {
        cur_bblk = iter.second;
        if (startpc2bblk.count(cur_bblk.first)) {
            auto old_bblk = startpc2bblk[cur_bblk.first];
            if (old_bblk.second > cur_bblk.second) {
                // old_bblk include cur_bblk, need split old_bblk
                startpc2bblk.erase(old_bblk.first);
                pair<uint64_t, uint64_t> new_bblk(cur_bblk.second + 4, old_bblk.second);
                startpc2bblk[new_bblk.first] = new_bblk;
                startpc2bblk[cur_bblk.first] = cur_bblk;
            } else if (old_bblk.second < cur_bblk.second) {
                // cur_bblk include old_bblk, need split cur_bblk
                cur_bblk.first = old_bblk.second + 4;
                startpc2bblk[cur_bblk.first] = cur_bblk;
            }
        } else {
            startpc2bblk[cur_bblk.first] = cur_bblk;
        }
    }

    bblks.clear();

    for (auto iter : startpc2bblk) {
        bblks.insert(iter.second);
    }
}

void my_emu_stop() {
    
    split_overlap_bblk();

    for (auto iter : bblks) {
        fprintf(bblk_file, "id:%ld, pc:%lx, bb_insn_num:%ld\n",
            unique_trans_id++, iter.first, ((iter.second - iter.first) / 4) + 1);
    }

    fflush(bblk_file);
    fclose(bblk_file);
}

la_emu_plugin_ops my_op = {
    .emu_stop = my_emu_stop,
    .emu_insn_before = my_emu_insn_before,
};

extern "C" la_emu_plugin_ops* la_emu_plugin_install(const char* arg) {
    string bblk_file_name("bblk.txt");
    if (arg[0]) {
        auto options = split(arg, ",");
        for (auto &option : options) {
            auto sp = split(option, "=");
            if (sp[0] == "bblk") {
                bblk_file_name = sp[1];
            } else if (sp[0] == "ibar0x40") {
                begin_after_ibar0x40 = stol(sp[1]);
            } else {
                printf("unknown option:%s\n", option.c_str());
            }
        }
    }
    bblk_file = fopen_nofail(bblk_file_name.c_str(), "w");
    return &my_op;
}