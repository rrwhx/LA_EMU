#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>

extern "C" {
#include "../plugin.h"
}

using namespace std;

static uint64_t interval_size = 1000000;
static FILE* bbv_file;

// simpoint id can not be zero
uint64_t unique_trans_id = 1;

uint64_t last_id;
uint64_t last_pc = -1;
uint64_t cur_block_count;

uint64_t cur_interval_count;

unordered_map<uint64_t, uint64_t> pc2id;
unordered_map<uint64_t, uint64_t> interval_id_cnt;



static inline FILE* fopen_nofail(const char *__restrict __filename, const char *__restrict __modes) {
    FILE* f = fopen(__filename, __modes);
    if (!f) {
        perror(__filename);
        abort();
    }
    return f;
}

void my_emu_insn_before(void* env, uint64_t pc, uint32_t insn) {
    if (pc != last_pc + 4) {
        interval_id_cnt[last_id] += cur_block_count;

        cur_block_count = 0;
        last_pc = pc;
        if(!pc2id.count(pc)) {
            pc2id[pc] = unique_trans_id;
            last_id = unique_trans_id;
            ++ unique_trans_id;
        } else {
            last_id = pc2id[pc];
        }
    }

    ++ cur_block_count;
    ++ cur_interval_count;

    if (cur_interval_count == interval_size) {
        fprintf(bbv_file, "T");
        for (auto &[k, v] : interval_id_cnt) {
            if (k != 0) {
                fprintf(bbv_file, ":%ld:%ld ", k, v);
            }
        }
        fprintf(bbv_file, "\n");
        fflush(bbv_file);

        interval_id_cnt.clear();
        cur_interval_count = 0;
        cur_block_count = 0;
    }
}

// for string delimiter
std::vector<std::string> split(std::string s, std::string delimiter) {
    size_t pos_start = 0, pos_end, delim_len = delimiter.length();
    std::string token;
    std::vector<std::string> res;

    while ((pos_end = s.find(delimiter, pos_start)) != std::string::npos) {
        token = s.substr (pos_start, pos_end - pos_start);
        pos_start = pos_end + delim_len;
        res.push_back (token);
    }

    res.push_back (s.substr (pos_start));
    return move(res);
}

la_emu_plugin_ops my_op = {
    .emu_insn_before = my_emu_insn_before,
};

extern "C" la_emu_plugin_ops* la_emu_plugin_install(const char* arg) {
    string bbv_file_name("result.bbv");
    if (arg[0]) {
        auto options = split(arg, ",");
        for (auto &option : options) {
            auto sp = split(option, "=");
            if (sp[0] == "size") {
                interval_size = stol(sp[1]);
            } else if (sp[0] == "name") {
                bbv_file_name = sp[1];
            } else {
                printf("unknoen option:%s\n", option.c_str());
            }
        }
    }
    bbv_file = fopen_nofail(bbv_file_name.c_str(), "w");
    return &my_op;
}
